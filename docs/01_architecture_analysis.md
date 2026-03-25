# 第一阶段：代码框架分析报告

## 项目概述

本项目是一个基于 Qt6 实现的嵌入式设备上位机（GH Protocol App），主要用于与 GoodixHealth（GH）系列生物传感芯片（如 GH3x2x）通信，支持串口和 BLE（蓝牙）两种通信方式。

---

## 1. 总体架构

### 模块划分

```
gh_protocol_app/
├── mainUI/           → GUI 层（主窗口）
├── subUI/            → GUI 层（子对话框）
│   ├── about/        → 关于对话框
│   ├── help/         → 帮助对话框
│   └── cardiffb/     → CardiffB 协议控制 Tab
├── utils/            → 工具层
│   ├── ble/          → BLE 管理（BLEManager / BLEDialog）
│   ├── cfg/          → 配置管理（ConfigManager，YAML + Qt 信号）
│   ├── csv_manager/  → CSV 数据文件管理
│   ├── expand/       → 扩展工具
│   ├── log/          → 日志缓冲（LogBuffer）
│   └── serial/       → 串口监听辅助（SerialMonitor）
└── lib/              → 业务/协议库
    ├── cardiff_a/    → CardiffA 协议解析层（核心）
    │   ├── inc/      → 设备驱动头文件（gh_drv.h、gh_uprotocol.h 等）
    │   └── gh_protocol_parser/  → 协议解析与命令发送实现
    ├── cardiff_rpc/  → CardiffRPC 远程调用层（C语言实现为主）
    ├── chelsea_a/    → ChelseaA 协议（备用）
    ├── PlotManager/  → 数据绘图管理
    ├── DetachableTab/→ 可拖拽 Tab 组件
    └── yaml-cpp/     → YAML 解析库
```

### 模块职责

| 模块 | 层级 | 职责 |
|------|------|------|
| mainUI | GUI层 | 主窗口、串口连接控制、BLE连接、数据显示、模式切换 |
| subUI/cardiffb | GUI层+业务层 | CardiffB 协议操作面板，发送指令并展示结果 |
| utils/cfg | 工具层 | YAML配置文件读写，通过Qt信号通知配置变更 |
| utils/ble | 工具层 | BLE扫描/连接/收发，封装Qt蓝牙API |
| utils/serial | 工具层 | 串口端口列表刷新和端口名提取 |
| utils/log | 工具层 | 异步日志写文件缓冲区 |
| lib/cardiff_a/inc | 协议层 | 硬件驱动寄存器定义、协议常量宏（纯C头文件） |
| lib/cardiff_a/gh_protocol_parser | 协议层+业务层 | 核心：协议数据打包/解包、命令发送、数据解压缩 |
| lib/cardiff_rpc | 业务层 | CardiffRPC远程调用框架（纯C实现）|
| lib/PlotManager | GUI层 | 数据图形化绘制 |

### 模块调用关系

```
[Serial Port / BLE]
        ↓ 原始字节流
[MainWindow] → 分帧(AA11帧头) → emit reciveData()
        ↓
[CardiffBTabWidget] → onReciveData()
        ↓
[GhZipParser::ParseCommand()]
        ↓ 根据 CmdKey
    ├── ParseRawdataPacket()     → 解析原始数据帧
    ├── ParseCompressedEven/OddRawdataPacket() → 解析压缩帧
    ├── ParseNewProtocolRawdataPacket() → 新协议帧
    └── ParseUploadZipDataToMaster()→ 含多帧ZIP数据
        ↓
    [GhFifoManager] → 存储解析后的帧数据
        ↓
    [PlotManager] → 绘图显示

[CardiffBTabWidget] → sendData() → emit sendData(QByteArray)
        ↓
[MainWindow::sendData()] → serial->write() / bleManager->sendData()
```

---

## 2. 核心类/文件说明

### 2.1 mainUI/mainwindow.h/.cpp

| 项目 | 说明 |
|------|------|
| 功能 | 主窗口，统筹协调所有子模块 |
| 输入 | 串口数据 (readyRead)、BLE数据回调、用户UI操作 |
| 输出 | 分帧后的数据通过 `reciveData` 信号传递给业务层 |
| Qt依赖 | 极重：QMainWindow、QSerialPort、QBluetoothUuid、QTimer、各种Qt控件 |
| 业务逻辑 | 包含：帧分割逻辑(AA11)、超时定时器(100ms)、模式切换、串口收发 |

**关键逻辑：handleSerialReadyRead()**
- 将到达数据追加到 `recvBuffer`
- 按 `0xAA11` 帧头分帧
- 完整帧通过 `emit reciveData()` 发送给业务层
- 残余数据启动 100ms 帧超时定时器

### 2.2 utils/cfg/ConfigManager.h/.cpp

| 项目 | 说明 |
|------|------|
| 功能 | YAML格式配置文件读写，管理串口/BLE/通用配置 |
| 输入 | YAML文件路径 |
| 输出 | Qt信号：commonConfigChanged / serialConfigChanged / bleConfigChanged |
| Qt依赖 | QObject、QString、QTimer（定时自动保存）、QBluetoothUuid |
| 业务逻辑 | 配置变更后定时自动保存 |

### 2.3 lib/cardiff_a/gh_protocol_parser/GhZipParser（核心）

| 项目 | 说明 |
|------|------|
| 功能 | 协议核心：命令打包/解包，数据解压，FIFO管理 |
| 输入 | `ParseCommand(uint8_t* packet, uint8_t length)` |
| 输出 | 解析后的 `STGh3x2xFrameInfoParser` 存入 FIFO |
| Qt依赖 | **无Qt依赖**（使用 std::vector、std::map、std::thread） |
| C++依赖 | std::vector、std::map、std::thread、std::this_thread::sleep_for |
| 业务逻辑 | 包含完整协议解析、压缩解压、命令发送逻辑 |

> ⭐ **此类是最接近纯C可迁移的核心模块，Qt耦合度为零**

### 2.4 lib/cardiff_rpc/（CardiffRPC子系统）

| 项目 | 说明 |
|------|------|
| 功能 | 远程过程调用框架（类似gRPC，但为C实现） |
| 输入 | 注册的RPC函数 |
| 输出 | 序列化后的调用包 |
| Qt依赖 | `CardiffRPCCall.cpp` 有极少量 C++ 包装，核心 `.c` 文件无Qt |
| 业务逻辑 | Slab内存管理、静态Map、包构建、RPC调用分发 |

### 2.5 utils/ble/BLEManager

| 项目 | 说明 |
|------|------|
| 功能 | BLE设备扫描、连接、UUID收发 |
| 输入 | UUID配置、用户操作 |
| 输出 | `dataReceived(QByteArray)` 信号、`logMessage(QString)` 信号 |
| Qt依赖 | **极重**：QBluetoothDeviceDiscoveryAgent、QLowEnergyController 等 |
| 迁移难度 | **高**，需替换为平台 BLE API 或移除 |

---

## 3. 通信协议分析（重点）

### 3.1 协议帧结构（UProtocol格式）

```
帧起始标志（外层）: 0xAA 0x11（由MainWindow在串口层检测分帧）

UProtocol内层包格式:
 ┌────────┬─────────┬─────┬────────┬───────────────────────┬──────┐
 │ FIXED  │  VER    │ CMD │  LEN   │      PAYLOAD          │ CRC8 │
 │  0x55  │  0x01   │ XX  │  XX    │  N bytes (LEN个字节)  │  XX  │
 └────────┴─────────┴─────┴────────┴───────────────────────┴──────┘
  1 Byte     1 Byte  1B    1B          LEN Bytes           1 Byte

常量定义（来自 gh_uprotocol.h）:
  GH3X2X_UPROTOCOL_FIXED_HEADER = 0x55
  GH3X2X_UPROTOCOL_VERSION      = 0x01
  GH3X2X_UPROTOCOL_PACKET_HEADER_LEN = 4
  GH3X2X_UPROTOCOL_PACKET_LEN_MAX    = 255
```

### 3.2 命令类型（CmdKey枚举）

| 命令码 | 名称 | 方向 | 说明 |
|--------|------|------|------|
| 0x01 | OperationAck | 上行 | 操作响应/应答 |
| 0x02 | DeviceStatusQuery | 下行 | 查询设备状态 |
| 0x03 | RegisterReadWrite | 双向 | 读写寄存器 |
| 0x04 | ConfigDataDownload | 下行 | 配置数据下发 |
| 0x08 | RawdataPacket | 上行 | 原始数据包 |
| 0x09 | CompressedEvenRawdataPacket | 上行 | 压缩偶数帧 |
| 0x0A | CompressedOddRawdataPacket | 上行 | 压缩奇数帧 |
| 0x0B | NewProtocolRawdataPacket | 上行 | 新协议原始数据包 |
| 0x0C | StartHbd | 下行 | 启动/停止HBD采样 |
| 0x10 | SlaveWorkModeSetting | 下行 | 工作模式设置 |
| 0x12 | CardiffFifoThresholdSetting | 下行 | FIFO阈值设置 |
| 0x19 | GetEvkVersion | 双向 | 获取EVK版本 |
| 0x1F | DriverConfigDownload | 下行 | 驱动配置下发 |
| 0x2C | FunctionInfoUpdate | 上行 | 功能信息更新 |

### 3.3 打包流程（UprotocolPacketFormat）

```
输入: cmd(1B) + payload(N B)
  ↓
组装 Header:
  outPacket[0] = 0x55 (FIXED)
  outPacket[1] = 0x01 (VER)
  outPacket[2] = cmd
  outPacket[3] = length (payload长度)
  ↓
拷贝 Payload:
  memcpy(&outPacket[4], payload, length)
  ↓
计算并附加 CRC8:
  outPacket[4+length] = CalculateCrc8(outPacket, 4+length)
  ↓
输出: outPacket, outLength = 4 + length + 1
```

### 3.4 数据解压流程（差分压缩解码）

设备上传的原始数据采用差分4bit压缩：
```
数据结构（每通道）:
  uchDataType (4bit):
    高3位 → 有效4bit分组数 (chNum)
    低1位 → 差值方向 (0=当前≥历史, 1=当前<历史)
  差值数据: chNum+1个4bit码拼成差值
  当前值 = 历史值 ± 差值
```

### 3.5 新协议数据包头解析（GhUnPackPakcageHeader）

```
字节偏移（payload数组索引）:
  UPROTOCOL_FUNCTION_ID_INDEX  → 功能ID（如PPG=0, HR=1, ECG=5...）
  UPROTOCOL_RAWDATA_TYPE_INDEX → 各Sensor使能标志位字段:
    bit[GS_ENABLE]     → G-sensor是否包含
    bit[ALGO_ENABLE]   → 算法结果是否包含
    bit[AGC_ENABLE]    → AGC数据是否包含
    bit[AMBIANCE]      → 环境光数据是否包含
    bit[GS_GYRO]       → 陀螺仪是否包含
    bit[CAP_ENABLE]    → 电容数据是否包含
    bit[TEMP_ENABLE]   → 温度数据是否包含
  UPROTOCOL_PACKAGE_TYPE_INDEX → 包类型字段:
    bit[ZIP_ENABLE]    → 是否压缩
    bit[ODDEVEN_FLAG]  → 奇偶帧标志
    bit[FUNCTION_MODE] → FIFO打包模式
```

### 3.6 通信接口

| 接口 | 类型 | Qt依赖 | 说明 |
|------|------|--------|------|
| 串口 | QSerialPort | **有** | 波特率 400000 默认，8N1 |
| BLE | Qt Bluetooth | **有** | GATT特征值UUID收发 |
| 分帧 | mainwindow.cpp | **有** | 0xAA11帧头+100ms超时 |

> 通信接口层与UI**中度耦合**：分帧逻辑在MainWindow中，与Qt深度绑定，需独立剥离。
