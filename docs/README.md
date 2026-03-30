# GH Protocol 重构输出 — 工程说明文档（README）

> 本目录是将原 Qt/C++ 上位机（`gh_protocol_app`）重构为 **纯C后端 + Web前端** 的完整工程化输出。

---

## 目录结构

```
refactor_output/
├── CMakeLists.txt          ← 构建系统（替换原 Qt qmake）
├── main.c                  ← 程序入口
│
├── src/
│   ├── protocol/           ← 协议层（核心，无Qt依赖）
│   │   ├── gh_protocol.h   → 协议数据结构 + 接口声明
│   │   └── gh_protocol.c   → 打包/解包/CRC8/差分解压实现
│   │
│   ├── transport/          ← 通信层（POSIX串口）
│   │   ├── gh_transport.h  → 串口抽象 + 分帧接口
│   │   └── gh_transport.c  → termios驱动 + 接收线程 + 分帧逻辑
│   │
│   ├── service/            ← 业务逻辑层
│   │   ├── gh_service.h    → 设备状态管理 + 命令调度接口
│   │   └── gh_service.c    → 服务层实现（连接各层回调）
│   │
│   └── api/                ← HTTP/WebSocket 接口层
│       └── gh_http_server.c→ RESTful API 框架
│
├── web/
│   └── frontend/
│       └── index.html      ← Web UI 前端（单文件，零依赖）
│
└── docs/
    ├── 01_architecture_analysis.md  ← 第一阶段：代码框架分析
    ├── 02_refactor_design.md        ← 第二阶段：解耦与重构设计
    ├── 03_user_manual.md            ← 用户上手、测试及 API 完全使用手册
    ├── 04_release_notes_2026-03-30.md ← 当前稳定版本修复说明与验收要点
    └── README.md                    ← 本文档
```

---

## 各模块调用流程

```
设备（串口/BLE）
    │ 字节流（波特率115200）
    ▼
[transport层] gh_transport.c
    │ select()非阻塞IO + pthread接收线程
    │ 按0xAA11分帧（100ms超时）
    │ 完整帧 → on_frame_cb()
    ▼
[service层] gh_service.c
    │ s_on_transport_frame()
    │ 去掉0xAA11帧头，送入协议层
    ▼
[protocol层] gh_protocol.c
    │ gh_parse_packet()
    │ CRC8校验 → 分发命令处理
    │ 差分解压 gh_decompress_rawdata()
    │ 解析完成 → on_frame_parsed_cb()
    ▼
[service层] s_on_frame_parsed()
    │ 封装 gh_data_frame_t
    │ 调用 on_data_cb()
    ▼
[api层] gh_api_on_data()
    │ 序列化为JSON
    │ WebSocket广播
    ▼
[Web前端] index.html
    │ WebSocket接收
    │ 实时波形绘制
    │ 指标显示更新
```

---

## HTTP API 接口说明

| 方法 | 路径 | 说明 | 请求体示例 |
|------|------|------|-----------|
| GET | `/api/device/status` | 获取设备状态 | 无 |
| GET | `/api/device/list` | 列出可用串口 | 无 |
| POST | `/api/device/connect` | 连接设备 | `{"port":"/dev/ttyUSB0","baud_rate":115200}` |
| POST | `/api/device/disconnect` | 断开连接 | `{}` |
| POST | `/api/device/start` | 开始/停止采样 | `{"ctrl":0,"mode":0,"func_mask":2}` |
| POST | `/api/device/config` | 下发寄存器配置 | `{"regs":[{"addr":258,"data":1}]}` |
| GET | `/api/device/data` | 获取最新帧（轮询） | 无 |
| WS | `/ws` | 实时数据推送 | — |

### 响应格式（统一）

```json
{
  "code": 0,
  "msg": "ok",
  "data": { ... }
}
```

### WebSocket 推送格式

```json
{
  "type": "data",
  "func_id": 1,
  "frame_cnt": 123,
  "raw": [100000, 80000, 60000, 40000],
  "hr": 72,
  "ts": 1700000000000
}
```

---

## 构建与运行

### 环境要求

- Linux（推荐 Ubuntu 20.04+）或嵌入式 Linux
- GCC 或 Clang（支持C11）
- CMake 3.14+
- POSIX pthread（glibc默认包含）

```bash
# 构建
cd refactor_output
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4

# 运行（替换为实际串口）
./gh_backend --port /dev/ttyUSB0 --baud 115200 --http-port 8080
```

### 接入 HTTP 服务器库

推荐两种方案：

**方案A：mongoose（推荐嵌入式）**
```bash
# 下载 mongoose.c + mongoose.h（单文件，拷入项目）
wget https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.c
wget https://raw.githubusercontent.com/cesanta/mongoose/master/mongoose.h
# 在 gh_http_server.c 中 #include "mongoose.h"
```

**方案B：libmicrohttpd（Linux标准环境）**
```bash
apt install libmicrohttpd-dev
# CMakeLists.txt 中添加:
# find_package(MHD REQUIRED)
# target_link_libraries(gh_backend PRIVATE MHD::MHD)
```

### 访问Web UI

构建并运行后，浏览器访问：
```
http://localhost:8080/
```

---

## 关键数据结构说明

### gh_parser_ctx_t（协议解析器上下文）

替代原 `GhZipParser` C++ 类，将所有成员变量变为 C 结构体字段：

| 字段 | 原C++成员 | 说明 |
|------|----------|------|
| `zip_last_data` | `m_stZipLastData` | 差分解压历史数据 |
| `func_info_map[]` | `m_functionInfoParserMap` | 功能通道映射（std::map→数组）|
| `send_cb` | `virtual send()` | 纯虚函数→回调指针 |
| `on_frame_parsed` | `ParseUploadZipDataToMaster` | 帧解析结果回调 |

### gh_transport_t（通信层上下文）

替代原 `QSerialPort` + `QTimer` + `MainWindow.recvBuffer`：

| 字段 | 原Qt实现 | 说明 |
|------|---------|------|
| `serial_fd` | `QSerialPort* serial` | 文件描述符 |
| `rx_buf[]` | `QByteArray recvBuffer` | 接收缓冲区 |
| `last_rx_time_ms` | `QTimer* frameTimer` | 超时时间戳 |
| `on_frame` | `emit reciveData()` | 帧就绪回调 |

---

## Qt依赖替换对照表

| Qt类/功能 | C替代方案 | 所在文件 |
|-----------|-----------|---------|
| `QSerialPort` | `termios / open() / read()` | `gh_transport.c` |
| `QTimer` | `clock_gettime() + select()超时` | `gh_transport.c` |
| `QByteArray` | `uint8_t[] + len` | 所有C文件 |
| `QString` | `char[N]` | 所有C文件 |
| `QObject + signals/slots` | 回调函数指针 | `gh_service.h` |
| `QMainWindow` | HTTP服务器 | `gh_http_server.c` |
| `QThread` | `pthread_t` | `gh_transport.c` |
| `std::vector` | 固定数组 + 计数器 | `gh_protocol.h` |
| `std::map` | 数组索引 | `gh_parser_ctx_t.func_info_map` |
| `std::this_thread::sleep_for` | `usleep() / nanosleep()` | `gh_transport.c` |
| `yaml-cpp` | cJSON / libconfig | 配置管理（待实现）|

---

## 扩展指南

### 添加新命令（如自定义CMD）

1. 在 `gh_protocol.h` 添加命令码宏 `#define GH_CMD_MY_CMD (0xXX)`
2. 在 `gh_protocol.c` 的 `gh_parse_packet()` switch 中添加 case
3. 在 `gh_protocol.c` 添加 `gh_cmd_my_cmd()` 函数实现
4. 在 `gh_service.h` / `.c` 暴露业务接口 `gh_service_my_cmd()`
5. 在 `gh_http_server.c` 添加对应的 HTTP API 端点

### 支持 BLE 通信

修改 `gh_transport.c`，增加 BLE 后端：

```c
/* 添加 BLE 结构体和接口 */
typedef struct {
    /* bluez D-Bus 句柄 或 HCI socket */
    int ble_fd;
    ...
} gh_ble_transport_t;

bool gh_transport_open_ble(gh_transport_t* t, const char* mac_addr);
```

### 添加数据存储（CSV）

在 `s_on_frame_parsed()` 中追加文件写入：

```c
/* 替换原 csv_manager */
static FILE* s_csv_file = NULL;
void gh_service_set_csv_path(gh_service_t* svc, const char* path) {
    if (s_csv_file) fclose(s_csv_file);
    s_csv_file = fopen(path, "a");
}
```

---

## 第六阶段：迁移执行步骤

```
第1步（1-2天）: 剥离通信层
  ✅ 提取分帧逻辑到 gh_transport.c（已完成）
  ✅ 替换 QSerialPort → termios（已完成）
  □ 测试：使用 nc/socat 模拟串口验证分帧

第2步（2-3天）: 提取业务逻辑
  ✅ 将 GhZipParser → gh_parser_ctx_t（已完成）
  ✅ 将 virtual send() → send_cb 回调（已完成）
  □ 单元测试：用预录数据验证解析器输出

第3步（2-3天）: 替换 Qt
  ✅ ConfigManager → 简单JSON/YAML读写（已规划）
  ✅ BLEManager → 暂停使用或 bluez 替代
  □ 集成测试：完整数据流验证

第4步（3-5天）: 接入 Web
  ✅ HTTP API 设计（已完成）
  ✅ Web UI（已完成）
  □ 接入 mongoose 或 libmicrohttpd
  □ WebSocket 广播实现
  □ 端到端测试
```

---

*文档生成时间: 2026-03-24 | 作者: AI架构分析*
