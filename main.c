/* ============================================================
 * 文件: main.c
 * 功能: 后端服务主程序
 *
 * 启动流程:
 *   1. 解析命令行参数
 *   2. 初始化 gh_api（HTTP + WebSocket 服务器）
 *   3. 初始化 gh_service（业务层 + 协议层）
 *   4. 尝试连接串口（若指定了 --port）
 *   5. 若无真实设备或指定 --sim，启动模拟器线程
 *   6. 进入 mongoose 事件循环（阻塞于此）
 *   7. Ctrl+C 后优雅退出
 *
 * 命令行:
 *   ./gh_backend                          → 纯模拟器模式，浏览器打开 http://localhost:8080
 *   ./gh_backend --port /dev/ttyUSB0      → 真实串口（默认115200），失败时自动降级到模拟器
 *   ./gh_backend --port COM3 --baud 115200→ Windows 串口
 *   ./gh_backend --http 9090 --sim        → 强制模拟器，端口9090
 *   ./gh_backend --web /path/to/frontend  → 自定义前端目录
 * ============================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include "src/port/gh_os_port.h"

#include "src/service/gh_service.h"
#include "src/api/gh_http_server.h"

/* ============================================================
 * 全局上下文（单例）
 * ============================================================ */
static gh_service_t  g_service;
static gh_api_t      g_api;
static volatile bool g_running = true;

/* ============================================================
 * 信号处理
 * ============================================================ */
static void s_on_signal(int sig) {
    (void)sig;
    g_running = false;
    gh_api_stop(&g_api);
    printf("\n[Main] Shutting down gracefully...\n");
}

/* ============================================================
 * 服务层回调
 * ============================================================ */
static void s_on_device_state(gh_device_state_t state, void *ctx) {
    (void)ctx;
    const char *names[] = { "DISCONNECTED", "CONNECTED", "SAMPLING", "ERROR" };
    printf("[Main] Device state → %s\n",
           state < 4 ? names[state] : "UNKNOWN");
}

/**
 * @brief 真实设备数据到达 → 推送到 API 层（再由 WebSocket 广播前端）
 * ctx 指向 g_api
 */
static void s_on_data_frame(const gh_data_frame_t *frame, void *ctx) {
    gh_api_t *api = (gh_api_t *)ctx;
    gh_api_push_frame(api, frame);
}

static void s_on_log(const char *msg, void *ctx) {
    gh_api_t *api = (gh_api_t *)ctx;
    printf("%s\n", msg);
    if (api && msg) {
        /* 根据前缀判断方向，推送到前端调试窗口 */
        const char *dir = "info";
        if (msg[0] == '[' && msg[1] == 'T' && msg[2] == 'X' && msg[3] == ']') dir = "tx";
        else if (msg[0] == '[' && msg[1] == 'R' && msg[2] == 'X' && msg[3] == ']') dir = "rx";
        gh_api_push_log(api, msg, dir);
    }
}

/* ============================================================
 * 设备模拟器线程
 * 生成类似 GH3x2x PPG + 心率的模拟数据，以 20 FPS 速率推送
 * ============================================================ */
typedef struct {
    gh_api_t     *api;
    volatile bool *running;
} sim_args_t;

#ifdef _WIN32
static unsigned __stdcall s_simulator_thread(void *arg) {
#else
static void *s_simulator_thread(void *arg) {
#endif
    sim_args_t  *a   = (sim_args_t *)arg;
    double       t   = 0.0;
    uint32_t     cnt = 0;

    printf("[Sim ] Simulator thread started (20 FPS)\n");

    while (*a->running) {
        gh_data_frame_t frame;
        memset(&frame, 0, sizeof(frame));

        /* 功能ID = 1（心率功能）*/
        frame.func_id   = 1;
        frame.frame_cnt = cnt++;

        /* 模拟 PPG 波形：基础值 + 主频 + 呼吸调制 */
        double ppg = 100000.0
                   + sin(t * 2.09)  * 30000.0   /* 主心跳频率 ~2Hz */
                   + sin(t * 0.30)  * 8000.0    /* 呼吸调制 ~0.3Hz */
                   + sin(t * 6.28)  * 2000.0;   /* 高频噪声 */

        frame.raw_data[0] = (uint32_t)(ppg > 0 ? ppg : 0);
        frame.raw_data[1] = (uint32_t)(ppg * 0.85 > 0 ? ppg * 0.85 : 0);
        frame.raw_data[2] = (uint32_t)(ppg * 0.70 > 0 ? ppg * 0.70 : 0);
        frame.raw_data[3] = (uint32_t)(ppg * 0.55 > 0 ? ppg * 0.55 : 0);

        /* 模拟心率：70-80 BPM 缓慢变化 */
        frame.algo_result[0] = (int32_t)(75.0 + sin(t * 0.04) * 5.0);
        frame.algo_result_num = 1;

        /* 模拟 G-sensor：轻微随机抖动 */
        frame.gsensor[0] = (int16_t)(sin(t * 0.1) * 100);
        frame.gsensor[1] = (int16_t)(cos(t * 0.1) * 50);
        frame.gsensor[2] = 1000; /* Z 轴近似重力 */

        /* 时间戳 */
        frame.timestamp_ms = gh_platform_now_ms();

        /* 推送到 API 层 */
        gh_api_push_frame(a->api, &frame);

        t += 0.05;         /* 每帧时间增量 */
        GH_SLEEP_MS(50);   /* 50ms → 20 FPS */
    }

    printf("[Sim ] Simulator thread stopped.\n");
    return 0;
}

/* ============================================================
 * 主程序
 * ============================================================ */
int main(int argc, char *argv[]) {
    /* ----- 默认参数 ----- */
    const char *port    = NULL;    /* NULL = 使用模拟器 */
    int         baud    = 115200;
    int         http    = 8080;
    bool        use_sim = false;
    char        web_root[512];

    /* 默认前端目录：与可执行文件同级的 web/frontend
     * 用户可用 --web 覆盖 */
    snprintf(web_root, sizeof(web_root), "web/frontend");

    /* ----- 解析命令行 ----- */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--port") && i+1 < argc) {
            port = argv[++i];
        } else if (!strcmp(argv[i], "--baud") && i+1 < argc) {
            baud = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--http") && i+1 < argc) {
            http = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--sim")) {
            use_sim = true;
        } else if (!strcmp(argv[i], "--web") && i+1 < argc) {
            strncpy(web_root, argv[++i], sizeof(web_root) - 1);
        } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("GH Protocol Backend\n\n");
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Options:\n");
            printf("  --port <dev>   串口设备 (如 /dev/ttyUSB0 或 COM3)\n");
            printf("  --baud <n>     波特率 (默认: 115200)\n");
            printf("  --http <n>     HTTP 监听端口 (默认: 8080)\n");
            printf("  --sim          强制使用模拟器（不连接真实设备）\n");
            printf("  --web <dir>    前端目录 (默认: web/frontend)\n");
            printf("\n示例:\n");
            printf("  %s                          # 模拟器模式\n", argv[0]);
            printf("  %s --port /dev/ttyUSB0      # 真实串口\n", argv[0]);
            printf("  %s --port COM3 --baud 115200 # Windows\n", argv[0]);
            return 0;
        }
    }

    /* ----- 无串口时自动切换模拟器 ----- */
    if (!port && !use_sim) {
        printf("[Main] --port 未指定，自动启动模拟器模式\n");
        use_sim = true;
    }

    /* ----- 注册信号处理 ----- */
    signal(SIGINT,  s_on_signal);
    signal(SIGTERM, s_on_signal);

    /* ----- 打印启动信息 ----- */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║   GH Protocol Backend Service v1.0      ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║ Mode     : %-30s ║\n", use_sim ? "🎮 Simulator" : "🔌 Serial");
    if (!use_sim)
        printf("║ Port     : %-30s ║\n", port);
    printf("║ HTTP     : http://localhost:%-13d ║\n", http);
    printf("║ Frontend : http://localhost:%d/          ║\n", http);
    printf("║ WebSocket: ws://localhost:%d/ws          ║\n", http);
    printf("║ Web root : %-30s ║\n", web_root);
    printf("╚══════════════════════════════════════════╝\n\n");

    /* ----- 初始化 API 层（HTTP + WebSocket 服务器）----- */
    if (!gh_api_init(&g_api, &g_service, http, web_root)) {
        fprintf(stderr, "[Main] 无法启动 HTTP 服务器（端口 %d 可能被占用）\n", http);
        return 1;
    }

    /* ----- 初始化服务层 ----- */
    gh_service_init(&g_service,
                    s_on_device_state, NULL,
                    s_on_data_frame,   &g_api,   /* 真实数据 → 推送到 API */
                    s_on_log,          &g_api);  /* 日志 → 推送到 API 调试窗口 */

    /* ----- 尝试连接串口 ----- */
    if (port && !use_sim) {
        gh_serial_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        strncpy(cfg.port, port, sizeof(cfg.port) - 1);
        cfg.baud_rate    = baud;
        cfg.data_bits    = 8;
        cfg.parity       = 'N';
        cfg.stop_bits    = 1;
        cfg.flow_control = false;
        gh_service_set_serial_config(&g_service, &cfg);

        if (gh_service_connect_serial(&g_service, port)) {
            printf("[Main] 串口连接成功: %s @ %d\n", port, baud);
        } else {
            printf("[Main] 串口打开失败，降级到模拟器模式\n");
            use_sim = true;
        }
    }

    /* ----- 启动模拟器线程 ----- */
    gh_thread_t sim_tid  = 0;
    sim_args_t  sim_args = { .api = &g_api, .running = &g_running };
    if (use_sim) {
        printf("[Main] 启动设备模拟器...\n");
#ifdef _WIN32
        sim_tid = (HANDLE)_beginthreadex(NULL, 0, s_simulator_thread, &sim_args, 0, NULL);
        if (sim_tid == 0) {
#else
        if (pthread_create(&sim_tid, NULL, s_simulator_thread, &sim_args) != 0) {
#endif
            fprintf(stderr, "[Main] 模拟器线程启动失败\n");
        }
    }

    printf("[Main] 服务器运行中，在浏览器打开:\n");
    printf("[Main]   http://localhost:%d\n\n", http);
    printf("[Main] 按 Ctrl+C 停止\n\n");

    /* ----- 进入 mongoose 事件循环（阻塞）----- */
    gh_api_run(&g_api);

    /* ----- 清理 ----- */
    g_running = false;
    if (sim_tid) {
#ifdef _WIN32
        WaitForSingleObject(sim_tid, INFINITE);
        CloseHandle(sim_tid);
#else
        pthread_join(sim_tid, NULL);
#endif
    }
    if (!use_sim) gh_service_disconnect(&g_service);
    gh_api_destroy(&g_api);

    printf("[Main] 已退出。\n");
    return 0;
}
