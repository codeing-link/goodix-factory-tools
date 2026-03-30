/* ============================================================
 * 文件: src/transport/gh_transport.c
 * 功能: 通信层实现（POSIX串口 + 分帧逻辑）
 * 说明: 使用 termios/select/pthread，替换 QSerialPort + QTimer
 *       分帧核心逻辑迁移自 mainwindow.cpp::handleSerialReadyRead
 * ============================================================ */

#include "gh_transport.h"
#include "../port/gh_os_port.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <termios.h>
  #include <sys/ioctl.h>
  #include <sys/select.h>
  #include <sys/time.h>
#ifdef __APPLE__
  #include <IOKit/serial/ioss.h>
#endif
#endif



#ifdef _WIN32
/* ============================================================
 * 内部辅助：设置串口属性（Windows）
 * ============================================================ */
static bool s_configure_serial(HANDLE hComm, const gh_serial_config_t* cfg) {
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(hComm, &dcb)) {
        return false;
    }

    dcb.BaudRate = cfg->baud_rate;
    switch (cfg->data_bits) {
        case 5: dcb.ByteSize = 5; break;
        case 6: dcb.ByteSize = 6; break;
        case 7: dcb.ByteSize = 7; break;
        default: dcb.ByteSize = 8; break;
    }

    if (cfg->parity == 'E') {
        dcb.Parity = EVENPARITY;
        dcb.fParity = TRUE;
    } else if (cfg->parity == 'O') {
        dcb.Parity = ODDPARITY;
        dcb.fParity = TRUE;
    } else {
        dcb.Parity = NOPARITY;
        dcb.fParity = FALSE;
    }

    if (cfg->stop_bits == 2) {
        dcb.StopBits = TWOSTOPBITS;
    } else {
        dcb.StopBits = ONESTOPBIT;
    }

    if (cfg->flow_control) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    } else {
        dcb.fOutxCtsFlow = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
    }

    if (!SetCommState(hComm, &dcb)) {
        return false;
    }

    /* 配置超时，使其像非阻塞 read + select 一样工作
     * MAXDWORD, 0, 0: 立即返回任何当前已接收的数据，没有数据则立即返回空
     * 为了类似 select 的行为，我们在独立线程中处理读 + 睡眠
     */
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hComm, &timeouts);

    return true;
}

#else

/* ============================================================
 * 内部辅助：设置串口属性（termios）
 * ============================================================ */
static bool s_configure_serial(int fd, const gh_serial_config_t* cfg) {
    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("[Transport] tcgetattr failed");
        return false;
    }

    /* === 波特率 ===
     * 标准波特率使用 cfsetspeed；非标准（如400000）需要平台特定处理。
     * Linux 支持通过 BOTHER + struct termios2 设置自定义波特率。
     * 此处仅展示标准路径，实际需用 ioctl(TCSETS2) 设置自定义波特率。
     */
    speed_t baud;
    bool need_custom_baud = false;
    switch (cfg->baud_rate) {
        case 9600:   baud = B9600;   break;
        case 19200:  baud = B19200;  break;
        case 38400:  baud = B38400;  break;
        case 57600:  baud = B57600;  break;
        case 115200: baud = B115200; break;
        case 230400: baud = B230400; break;
#ifdef B400000
        case 400000: baud = B400000; break;
#else
        case 400000:
            /* macOS 等平台通常无 B400000，后续通过 ioctl(IOSSIOSPEED) 设置。 */
            baud = B38400;
            need_custom_baud = true;
            break;
#endif
#ifdef B460800
        case 460800: baud = B460800; break;
#endif
#ifdef B921600
        case 921600: baud = B921600; break;
#endif
        default:
            /* 不支持的标准波特率，尝试 B0 作为 fallback */
            baud = B115200;
            printf("[Transport] Warning: non-standard baud %d, using 115200\n",
                   cfg->baud_rate);
            break;
    }
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    /* 数据位 */
    tty.c_cflag &= ~CSIZE;
    switch (cfg->data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }

    /* 校验位 */
    tty.c_cflag &= ~(PARENB | PARODD);
    if (cfg->parity == 'E') {
        tty.c_cflag |= PARENB;             /* 偶校验 */
    } else if (cfg->parity == 'O') {
        tty.c_cflag |= PARENB | PARODD;    /* 奇校验 */
    }

    /* 停止位 */
    if (cfg->stop_bits == 2) {
        tty.c_cflag |= CSTOPB;
    } else {
        tty.c_cflag &= ~CSTOPB;
    }

    /* 流控 */
    if (cfg->flow_control) {
        tty.c_cflag |= CRTSCTS;
    } else {
        tty.c_cflag &= ~CRTSCTS;
    }

    tty.c_cflag |= CREAD | CLOCAL; /* 使能接收，忽略调制解调器控制 */
    tty.c_lflag = 0;  /* Raw 模式：关闭规范模式、echo 等 */
    tty.c_iflag = 0;  /* 关闭软件流控、奇偶校验处理 */
    tty.c_oflag = 0;  /* 关闭输出处理 */

    /* 非阻塞读：最少读0字节，超时0（配合 select 使用）*/
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("[Transport] tcsetattr failed");
        return false;
    }

#ifdef __APPLE__
    if (need_custom_baud) {
        speed_t custom_baud = (speed_t)cfg->baud_rate;
        if (ioctl(fd, IOSSIOSPEED, &custom_baud) == -1) {
            perror("[Transport] IOSSIOSPEED failed");
            return false;
        }
    }
#else
    (void)need_custom_baud;
#endif
    return true;
}
#endif

/* ============================================================
 * 内部：帧分割逻辑（基于长度字节的精确分帧）
 *
 * 帧格式: [AA][11][len][payload: len bytes][CRC]
 * 总长度 = len + 4
 *
 * 算法：
 *   1. 在缓冲区头部定位 0xAA11
 *   2. 读取 byte[2] 作为 payload 长度，计算总帧长 = len+4
 *   3. 若已收到完整帧字节，立即回调并前进 rx_head
 *   4. 否则等待更多数据（不使用超时推送残帧）
 *
 * 相比"找下一个AA11"的方式，此方法能正确处理负载中含有 AA 11 字节的情况
 * ============================================================ */
static void s_process_rx_buffer(gh_transport_t* t) {
    while (true) {
        uint16_t data_len = (t->rx_tail >= t->rx_head)
                          ? (uint16_t)(t->rx_tail - t->rx_head)
                          : 0U;

        if (data_len < 4U) break; /* 至少需要 AA 11 len CRC = 4 字节 */

        uint8_t *buf = &t->rx_buf[t->rx_head];

        /* 若不是 AA 11，逐字节跳过直到找到帧头 */
        if (buf[0] != GH_FRAME_MARKER0 || buf[1] != GH_FRAME_MARKER1) {
            uint16_t skip = data_len; /* 默认跳过全部 */
            for (uint16_t i = 1U; i + 1U < data_len; i++) {
                if (buf[i] == GH_FRAME_MARKER0 && buf[i + 1U] == GH_FRAME_MARKER1) {
                    skip = i;
                    break;
                }
            }
            t->rx_head += skip;
            if (skip == data_len) break; /* 未找到 AA 11 */
            continue;
        }

        /* 读 payload 长度，计算完整帧字节数 */
        uint16_t payload_len  = (uint16_t)buf[2];
        uint16_t total_len    = payload_len + 4U; /* AA+11+len+payload+CRC */

        /* 防御：单帧过大（大于缓冲区一半），认为同步丢失，跳过 AA */
        if (total_len > (GH_TRANSPORT_RX_BUF_SIZE / 2U)) {
            t->rx_head += 2U;
            continue;
        }

        if (data_len < total_len) {
            /* 帧尚未完整到达，等待 */
            break;
        }

        /* 完整帧：回调上层 */
        if (t->on_frame) {
            t->on_frame(buf, total_len, t->on_frame_ctx);
        }
        t->rx_head += total_len;

        /* 缓冲区紧凑：head 超过半程时搬移数据 */
        if (t->rx_head >= (GH_TRANSPORT_RX_BUF_SIZE / 2U)) {
            uint16_t remaining = t->rx_tail - t->rx_head;
            if (remaining > 0U) {
                memmove(t->rx_buf, &t->rx_buf[t->rx_head], remaining);
            }
            t->rx_head = 0U;
            t->rx_tail = remaining;
        }
    }

    t->last_rx_time_ms    = gh_platform_now_ms();
    t->frame_timeout_armed = (t->rx_tail > t->rx_head);
}

/**
 * @brief 帧超时：丢弃不完整的残余数据（避免积压）
 */
static void s_check_frame_timeout(gh_transport_t* t) {
    if (!t->frame_timeout_armed) return;

    uint32_t now = gh_platform_now_ms();
    if (now - t->last_rx_time_ms < GH_FRAME_TIMEOUT_MS) return;

    t->frame_timeout_armed = false;
    /* 长度模式下无法可靠拼接残帧，直接丢弃 */
    t->rx_head = t->rx_tail = 0U;
}

/* ============================================================
 * 接收线程（替代 Qt 事件循环 + readyRead 信号）
 * ============================================================ */
#ifdef _WIN32
static unsigned __stdcall s_rx_thread(void* arg) {
    gh_transport_t* t = (gh_transport_t*)arg;

    while (t->rx_thread_running) {
        DWORD bytesRead = 0;
        uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
        if (space == 0) {
            memmove(t->rx_buf, &t->rx_buf[t->rx_head], t->rx_tail - t->rx_head);
            t->rx_tail -= t->rx_head;
            t->rx_head = 0;
            space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
        }

        if (ReadFile((HANDLE)t->serial_fd, &t->rx_buf[t->rx_tail], space, &bytesRead, NULL)) {
            if (bytesRead > 0) {
                /* 记录接收的原始字节 */
                if (t->log_cb) {
                    char hex_buf[64 * 3 + 8];
                    int off = 0;
                    off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off, "[RX]");
                    DWORD show = bytesRead > 64 ? 64 : bytesRead;
                    for (DWORD i = 0; i < show; i++) {
                        off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                        " %02X", t->rx_buf[t->rx_tail + i]);
                    }
                    if (bytesRead > 64) {
                        snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                 " ...(%lu bytes)", bytesRead);
                    }
                    t->log_cb(hex_buf, t->log_ctx);
                }
                t->rx_tail += (uint16_t)bytesRead;
                s_process_rx_buffer(t);
            }
        }
        
        s_check_frame_timeout(t);
        GH_SLEEP_MS(50);
    }
    return 0;
}
#else
static void* s_rx_thread(void* arg) {
    gh_transport_t* t = (gh_transport_t*)arg;

    while (t->rx_thread_running) {
        fd_set  rfds;
        struct timeval tv;
        int     ret;

        FD_ZERO(&rfds);
        FD_SET((int)t->serial_fd, &rfds);

        /* 使用 50ms 轮询，比 100ms 帧超时频率高，保证及时检测 */
        tv.tv_sec  = 0;
        tv.tv_usec = 50 * 1000; /* 50ms */

        ret = select((int)t->serial_fd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;  /* 被信号中断，继续 */
            if (t->log_cb) {
                t->log_cb("[Transport] select error", t->log_ctx);
            }
            break;
        }

        if (ret > 0 && FD_ISSET((int)t->serial_fd, &rfds)) {
            /* 有数据可读 */
            uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
            if (space == 0) {
                /* 缓冲区已满，合并或清空 */
                memmove(t->rx_buf, &t->rx_buf[t->rx_head],
                        t->rx_tail - t->rx_head);
                t->rx_tail -= t->rx_head;
                t->rx_head = 0;
                space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
            }

            ssize_t n = read((int)t->serial_fd,
                             &t->rx_buf[t->rx_tail],
                             space);
            if (n > 0) {
                /* 记录接收的原始字节 */
                if (t->log_cb) {
                    char hex_buf[64 * 3 + 8];
                    int off = 0;
                    off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off, "[RX]");
                    ssize_t show = n > 64 ? 64 : n;
                    for (ssize_t i = 0; i < show; i++) {
                        off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                        " %02X", t->rx_buf[t->rx_tail + i]);
                    }
                    if (n > 64) {
                        snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                                 " ...(%zd bytes)", n);
                    }
                    t->log_cb(hex_buf, t->log_ctx);
                }
                t->rx_tail += (uint16_t)n;
                s_process_rx_buffer(t);
            } else if (n == 0) {
                /* 串口关闭或EOF */
                break;
            }
        }

        /* 在每次 select 返回时检查帧超时 */
        s_check_frame_timeout(t);
    }

    return NULL;
}
#endif

/* ============================================================
 * 公开API实现
 * ============================================================ */

void gh_transport_init(gh_transport_t* t,
                       gh_on_frame_cb on_frame, void* frame_ctx,
                       gh_on_connect_cb on_connect, void* conn_ctx,
                       gh_log_cb log_cb, void* log_ctx) {
    if (!t) return;
    memset(t, 0, sizeof(gh_transport_t));
    t->serial_fd      = -1;
    t->on_frame       = on_frame;
    t->on_frame_ctx   = frame_ctx;
    t->on_connect     = on_connect;
    t->on_connect_ctx = conn_ctx;
    t->log_cb         = log_cb;
    t->log_ctx        = log_ctx;
}

bool gh_transport_open_serial(gh_transport_t* t, const gh_serial_config_t* cfg) {
    if (!t || !cfg) return false;
    
#ifdef _WIN32
    if (t->serial_fd != 0 && t->serial_fd != -1) {
        CloseHandle((HANDLE)t->serial_fd);
        t->serial_fd = -1;
    }

    char portName[128];
    snprintf(portName, sizeof(portName), "\\\\.\\%s", cfg->port); // 解决COM10及以上的问题

    HANDLE hComm = CreateFileA(portName,
                               GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING, 0, NULL);

    if (hComm == INVALID_HANDLE_VALUE) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[Transport] Cannot open %s: Error %lu",
                 cfg->port, GetLastError());
        if (t->log_cb) t->log_cb(msg, t->log_ctx);
        return false;
    }

    if (!s_configure_serial(hComm, cfg)) {
        CloseHandle(hComm);
        return false;
    }

    t->serial_fd = (intptr_t)hComm;

#else
    if (t->serial_fd >= 0) {
        close((int)t->serial_fd);
        t->serial_fd = -1;
    }

    /* 打开串口设备，O_NOCTTY 不受终端控制，O_NONBLOCK 非阻塞 */
    int fd = open(cfg->port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[Transport] Cannot open %s: %s",
                 cfg->port, strerror(errno));
        if (t->log_cb) t->log_cb(msg, t->log_ctx);
        return false;
    }

    if (!s_configure_serial(fd, cfg)) {
        close(fd);
        return false;
    }

    t->serial_fd = fd;
#endif

    memcpy(&t->serial_cfg, cfg, sizeof(gh_serial_config_t));
    t->rx_head = 0;
    t->rx_tail = 0;
    t->frame_timeout_armed = false;

    if (t->on_connect) {
        t->on_connect(true, t->on_connect_ctx);
    }

    char msg_log[128];
    snprintf(msg_log, sizeof(msg_log), "[Transport] Opened %s @ %d baud",
             cfg->port, cfg->baud_rate);
    if (t->log_cb) t->log_cb(msg_log, t->log_ctx);
    return true;
}

void gh_transport_close(gh_transport_t* t) {
    if (!t) return;
    t->rx_thread_running = false;
    
#ifdef _WIN32
    if (t->serial_fd != -1 && t->serial_fd != 0) {
        CloseHandle((HANDLE)t->serial_fd);
        t->serial_fd = -1;
    }
#else
    if (t->serial_fd >= 0) {
        close((int)t->serial_fd);
        t->serial_fd = -1;
    }
#endif

    if (t->on_connect) {
        t->on_connect(false, t->on_connect_ctx);
    }
}

bool gh_transport_send(gh_transport_t* t, const uint8_t* data, uint16_t len) {
    if (!t || t->serial_fd == -1 || t->serial_fd == 0 || !data || len == 0) return false;

    /* 记录发送的原始字节（十六进制），通过 log_cb 推送到调试窗口 */
    if (t->log_cb) {
        /* 格式: "[TX] AA 11 55 01 19 01 xx" (最多显示64字节) */
        char hex_buf[64 * 3 + 8];
        int  off = 0;
        off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off, "[TX]");
        uint16_t show = len > 64 ? 64 : len;
        for (uint16_t i = 0; i < show; i++) {
            off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                            " %02X", data[i]);
        }
        if (len > 64) {
            off += snprintf(hex_buf + off, sizeof(hex_buf) - (size_t)off,
                            " ...(%u bytes)", (unsigned)len);
        }
        t->log_cb(hex_buf, t->log_ctx);
    }

#ifdef _WIN32
    DWORD written = 0;
    if (WriteFile((HANDLE)t->serial_fd, data, len, &written, NULL)) {
        return (written == len);
    }
    return false;
#else
    ssize_t written = write((int)t->serial_fd, data, len);
    return (written == (ssize_t)len);
#endif
}

bool gh_transport_is_open(const gh_transport_t* t) {
    return (t && t->serial_fd != -1 && t->serial_fd != 0);
}

void gh_transport_feed(gh_transport_t* t, const uint8_t* data, uint16_t len) {
    if (!t || !data || len == 0) return;
    /* 此函数供不使用线程时调用，手动喂数据 */
    uint16_t space = GH_TRANSPORT_RX_BUF_SIZE - t->rx_tail;
    if (len > space) len = space;
    memcpy(&t->rx_buf[t->rx_tail], data, len);
    t->rx_tail += len;
    s_process_rx_buffer(t);
}

bool gh_transport_start_rx_thread(gh_transport_t* t) {
    gh_thread_t tid;
    t->rx_thread_running = true;
    
#ifdef _WIN32
    tid = (HANDLE)_beginthreadex(NULL, 0, s_rx_thread, t, 0, NULL);
    if (tid == 0) {
        t->rx_thread_running = false;
        return false;
    }
#else
    if (pthread_create(&tid, NULL, s_rx_thread, t) != 0) {
        t->rx_thread_running = false;
        return false;
    }
    pthread_detach(tid); /* 不需要join，自动回收 */
#endif

    return true;
}

void gh_transport_stop_rx_thread(gh_transport_t* t) {
    if (t) t->rx_thread_running = false;
}
