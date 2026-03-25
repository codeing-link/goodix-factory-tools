# 第二阶段：解耦与重构设计

## 目标

将 Qt/C++ 上位机重构为：
- **纯 C 后端服务**（Linux 环境，适合嵌入式 + 服务器）
- **Web UI 前端**（替代 Qt GUI）
- 保留通信协议和核心业务逻辑

---

## 1. 模块拆分设计

### 最终系统模块图

```
┌─────────────────────────────────────────────────────┐
│                    Web 前端 (HTML/JS)                │
└───────────────────────┬─────────────────────────────┘
                        │ HTTP REST + WebSocket
┌───────────────────────▼─────────────────────────────┐
│              api 层（HTTP Server / WebSocket）        │
│        提供 RESTful 接口 + 实时事件推送              │
└───────┬──────────────────────────┬──────────────────┘
        │                          │
┌───────▼───────┐        ┌─────────▼────────┐
│  service 层   │        │   service 层      │
│（设备/配置管理）│        │（数据采集/处理）   │
└───────┬───────┘        └─────────┬────────┘
        │                          │
┌───────▼──────────────────────────▼────────┐
│              protocol 层                  │
│  GH协议打包/解包/解压缩/命令构造           │
└───────────────────────┬───────────────────┘
                        │
┌───────────────────────▼───────────────────┐
│              transport 层                 │
│  串口收发 / BLE 收发（POSIX/抽象）         │
└───────────────────────┬───────────────────┘
                        │
┌───────────────────────▼───────────────────┐
│              platform 层                  │
│  串口驱动(termios) / 定时器 / 线程         │
└───────────────────────────────────────────┘
        ↑ 贯穿所有层
┌───────────────────────────────────────────┐
│              utils 层                     │
│  日志、FIFO、配置文件(JSON/YAML)、环形缓冲  │
└───────────────────────────────────────────┘
```

### 各模块职责

| 模块 | 目录 | 职责 | 依赖 |
|------|------|------|------|
| protocol | src/protocol/ | GH协议帧打包/解包/CRC8/差分解压 | utils |
| transport | src/transport/ | 串口/BLE字节流收发，帧同步 | platform |
| service | src/service/ | 设备状态管理，命令调度，数据路由 | protocol, transport |
| api | src/api/ | HTTP服务器、WebSocket、JSON序列化 | service |
| platform | src/platform/ | POSIX串口termios、定时器、线程 | 系统API |
| utils | src/utils/ | 日志、环形缓冲区、配置、FIFO | 无 |

---

## 2. Qt 依赖剥离策略

### 2.1 QObject → 普通C结构体

**原来（Qt）：**
```cpp
class ConfigManager : public QObject {
    Q_OBJECT
signals:
    void commonConfigChanged(const CommonConfig& config);
};
```

**替换（纯C）：**
```c
/* 回调函数类型定义 */
typedef void (*on_config_changed_cb)(const common_config_t* config, void* user_data);

/* 配置管理器结构体 */
typedef struct {
    char config_path[256];
    common_config_t   common_cfg;
    serial_config_t   serial_cfg;
    ble_config_t      ble_cfg;
    on_config_changed_cb on_common_changed;
    void* user_data;
} config_manager_t;
```

### 2.2 signal/slot → 回调函数

**原来（Qt）：**
```cpp
connect(serial, &QSerialPort::readyRead, this, &MainWindow::handleSerialReadyRead);
connect(this, &MainWindow::reciveData, cardiffRPCTab, &CardiffBTabWiget::onReciveData);
```

**替换（纯C回调）：**
```c
/* 定义传输层回调 */
typedef struct {
    /* 收到完整帧时的回调 */
    void (*on_frame_received)(const uint8_t* frame, uint16_t len, void* ctx);
    /* 发送完成回调 */
    void (*on_send_complete)(bool success, void* ctx);
    void* ctx;
} transport_callbacks_t;

/* 注册回调 */
transport_register_callbacks(&g_transport, &callbacks);
```

### 2.3 QString → char* / 固定长度字符串

| Qt类型 | C替代方案 | 说明 |
|--------|-----------|------|
| `QString` | `char buf[256]` | 固定长度字符串，够用就行 |
| `QByteArray` | `uint8_t* + size_t len` | 二进制数组 |
| `QString::fromHex()` | 手写hex解析 | 简单循环实现 |
| `QString::number()` | `snprintf()` | C标准库 |

### 2.4 QTimer → POSIX定时器 / select超时

**原来（Qt）：**
```cpp
QTimer *frameTimer = new QTimer(this);
frameTimer->setSingleShot(true);
frameTimer->start(100);
connect(frameTimer, &QTimer::timeout, this, &MainWindow::onFrameTimeout);
```

**替换（POSIX select）：**
```c
/* 在 transport 接收线程中使用 select() 实现超时 */
struct timeval tv;
tv.tv_sec  = 0;
tv.tv_usec = 100 * 1000; /* 100ms */

fd_set rfds;
FD_ZERO(&rfds);
FD_SET(serial_fd, &rfds);

int ret = select(serial_fd + 1, &rfds, NULL, NULL, &tv);
if (ret == 0) {
    /* 超时，触发帧超时处理 */
    transport_on_frame_timeout(ctx);
}
```

### 2.5 QSerialPort → POSIX termios

**原来（Qt）：**
```cpp
serial->setPortName("/dev/ttyUSB0");
serial->setBaudRate(400000);
serial->setDataBits(QSerialPort::Data8);
serial->open(QIODevice::ReadWrite);
```

**替换（POSIX termios）：**
```c
int serial_open(const char* port, int baud_rate) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    
    struct termios tty;
    tcgetattr(fd, &tty);
    
    /* 设置波特率（自定义波特率需要 BOTHER） */
    cfsetispeed(&tty, B115200); /* 标准波特率 */
    cfsetospeed(&tty, B115200);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; /* 8位数据 */
    tty.c_cflag &= ~PARENB;  /* 无校验 */
    tty.c_cflag &= ~CSTOPB;  /* 1位停止位 */
    tty.c_cflag |= CREAD | CLOCAL;
    tty.c_lflag = 0;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}
```

### 2.6 std::vector / std::map → C数组 / 静态哈希表

| C++类型 | C替代方案 |
|--------|-----------|
| `std::vector<Frame>` | 固定大小数组 + 计数器，或单链表 |
| `std::map<uint32_t, FuncInfo>` | 静态查找表（已有 `StaticMapimp.c`） |
| `std::thread` | `pthread_t` 或 `_Thread_local` |
| `std::this_thread::sleep_for` | `usleep()` / `nanosleep()` |
| `std::chrono` | `clock_gettime(CLOCK_MONOTONIC)` |

---

## 3. 协议层可直接迁移的部分

以下代码**几乎无Qt依赖**，可以直接提取并转为纯C：

| 文件 | 迁移难度 | 说明 |
|------|---------|------|
| `gh_zip_parser.cpp` | ⭐低 | 仅用std::vector/map，算法纯C可写 |
| `gh_zip_command_api.cpp` | ⭐⭐低 | CRC8、协议打包，纯字节操作 |
| `gh_zip_command_send.cpp` | ⭐低 | 命令构造，字节操作 |
| `gh_fifo.cpp/.h` | ⭐低 | FIFO管理，替换std::vector即可 |
| `lib/cardiff_rpc/src/CardiffRPCCore.c` | ⭐低 | **已是纯C**，直接复用 |
| `lib/cardiff_rpc/src/CardiffPackage.c` | ⭐低 | **已是纯C**，直接复用 |
| `lib/cardiff_a/inc/*.h` | ⭐低 | **纯C头文件**，直接复用 |

---

## 4. 迁移的主要障碍

| 模块 | Qt依赖程度 | 迁移策略 |
|------|-----------|---------|
| MainWindow | 极重 | **完全删除**，功能拆到 api/service 层 |
| BLEManager | 极重 | 短期保留Qt BLE，或替换为 linux bluez API |
| ConfigManager | 中 | 改用 cJSON 或 libconfig 替代 YAML+Qt |
| SerialPort | 重 | 替换为 POSIX termios |
| LogBuffer | 轻 | 替换为简单文件写入或 syslog |
| QTimer | 中 | 替换为 select() 超时或 POSIX timer |
