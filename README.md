# goodix-factory-tools

基于 **Mongoose HTTP/WebSocket + 纯 C 后端 + HTML/JS 前端**分离架构，用于 GH3x2x / GH3036 生物传感器的工厂测试、调试与数据采样。

---

## 架构概览

```
浏览器 (index.html)
    │  HTTP REST API + WebSocket
    ▼
gh_backend (纯C可执行)
    ├── api/        Mongoose HTTP+WS 服务器
    ├── service/    业务逻辑层（命令调度、状态管理）
    ├── protocol/   Cardiff RPC 帧构建/解析 + Chelsea A 数据解码
    └── transport/  POSIX 串口收发线程
```

---

## 最新更新：Chelsea A 协议完整实现

### 背景

原始代码使用 UProtocol 格式（`AA 11 55 01 CMD LEN PAYLOAD CRC8`）与设备通信，该格式适用于 Cardiff A。
当设备运行 **Chelsea A** 固件时，通信协议切换为 **Cardiff RPC**，帧格式和数据编码方式完全不同。

---

### 1. Cardiff RPC 协议层（新增）

**新文件：** `src/protocol/gh_rpc.h` / `src/protocol/gh_rpc.c`

Chelsea A 通信使用 Cardiff RPC 二进制 RPC 协议，帧格式如下：

```
[AA][11][len][TypeKEY][key_len?][key...][com_id?][TypeData][val...][CRC_SUM]

TypeKEY (1 byte):  pack_type[1:0] | is_array[2] | width[5:3] | secure[6] | fin[7]
TypeData (1 byte): pack_type[1:0] | is_array[2] | width[5:3] | end[6]   | split[7]

CRC = sum(frame[3] .. frame[2+len]) mod 256  （简单字节累加，非多项式 CRC8）
```

- **sall 帧**（secure=1）：期望设备响应，携带 `com_id` 作为请求追踪 ID
- **send 帧**（secure=0）：单向发送，无需响应

核心 API：

| 函数 | 说明 |
|------|------|
| `gh_rpc_build_frame()` | 构建完整 Cardiff RPC 帧（含 AA11 头和 CRC） |
| `gh_rpc_pack_u8/u16/u32/i32()` | 序列化标量参数 |
| `gh_rpc_pack_u16_array()` | 序列化 u16 数组参数（支持 >255 元素分包） |
| `gh_rpc_parse_frame()` | 解析收到的帧（验证 CRC、提取 key + params 指针） |
| `gh_rpc_extract_u8_array()` | 从 params 中提取 u8* 数组（设备传感数据用） |
| `gh_rpc_extract_u16_array()` | 从 params 中提取 u16* 数组（寄存器读响应用） |

#### 帧构建示例（GH3X_GetVersion）

```
发送: AA 11 14 DE 0F 47 48 33 58 5F 47 65 74 56 65 72 73 69 6F 6E 00 59 01 C6
      ──── ── ──────────────── GH3X_GetVersion ──────────── ── ─── ── ──
      AA11  len  TypeKEY  key_len  "GH3X_GetVersion"(15)  com_id  u8=1  CRC
```

---

### 2. 命令层切换（Service 层改动）

**文件：** `src/service/gh_service.c`

所有命令均切换为 Chelsea A / Cardiff RPC 实现，协议默认启用（`use_chelsea_a_parser = true`）：

| 操作 | Cardiff RPC Key | 帧类型 | 参数格式 |
|------|----------------|--------|---------|
| 查询版本 | `GH3X_GetVersion` | sall | `<u8>` type=1 |
| 芯片控制 | `GH3X_ChipCtrl` | send | `<u8>` ctrl_val |
| 开始/停止采样 | `GH3X_SwFunctionCmd` | send | `<u32><u8>` func_mask, ctrl |
| 工作模式设置 | `GH3X_SwFunctionCmd` | send | `<u32><u8>` func_mask, mode |
| 寄存器读 | `GH3X_RegsReadCmd` | sall | `<u16><d32>` addr, count |
| 寄存器写 | `GH3X_RegsListWriteCmd` | send | `<u16*>` addr/val 交替对 |
| 配置下发 | `download_config` + `GH3X_RegsListWriteCmd` | send | `<u8>` + `<u16*>` |

> **TypeData 类型码参考**
> - `u8`  非末尾=`0x19`，末尾=`0x59`
> - `u16` 非末尾=`0x21`，末尾=`0x61`
> - `u32` 非末尾=`0x29`，末尾=`0x69`
> - `d32`（有符号）非末尾=`0x2A`，末尾=`0x6A`（pack_type=2=SIGNED）
> - `u16*` 末尾=`0x65`，分包=`0xE5`

---

### 3. 接收路径修复（RX Path）

**文件：** `src/service/gh_service.c` — `s_on_transport_frame()`

原代码直接将带 Cardiff RPC 头的原始帧传入 `gh_protocol_process()`，导致传感数据无法解析。现在的处理流程：

```
收到帧 → gh_rpc_parse_frame() 验证CRC + 提取key
           │
           ├── key == "G"               → gh_rpc_extract_u8_array() 剥离RPC包头
           │                              → gh_protocol_process() 解压Chelsea A传感数据
           │
           ├── key == "GH3X_GetVersion" → 扫描params提取u8*字符串
           │                              → on_log("[VERSION] GH(M)3036_SDK_V0.2.0.1_DEBUG...")
           │
           ├── key == "GH3X_RegsReadCmd"→ gh_rpc_extract_u16_array() 提取寄存器值
           │                              → on_log("[REG_VAL] 0x4B4A ...")
           │
           └── 其他key                  → on_log("[RPC] RX key=...")
```

> **注意**：传感数据（key="G"）使用 zigzag 差分压缩，需要 `gh_protocol_process()` 解压。
> 版本字符串和寄存器值是明文 ASCII / 直接二进制，**无需解压**。

---

### 4. 串口调试窗口（Serial Debug）

**文件：** `src/api/gh_http_server.h/.c`，`web/frontend/index.html`

在 Web 界面底部新增串口收发实时监控窗口：

- **TX 日志**：每次发送命令时，以 `[TX] AA 11 ...` 格式推送十六进制字节
- **RX 日志**：每次收到设备帧时，以 `[RX] AA 11 ...` 格式推送十六进制字节
- **过滤器**：可独立勾选显示/隐藏 TX / RX 条目
- **自动滚动** + **一键清除**
- 通过 WebSocket `{"type":"log","dir":"tx/rx/info","text":"..."}` 实时推送

实现路径：
`gh_transport_send()` / `s_rx_thread()` → `log_cb()` → `gh_api_push_log()` → WebSocket 广播

---

### 5. 响应结果回显

经 WebSocket 推送后，前端实时更新以下 UI 元素：

| 推送前缀 | 前端动作 |
|---------|---------|
| `[VERSION] <str>` | 更新"版本信息"文本框 + 写入操作日志 |
| `[REG_VAL] 0xVAL ...` | 更新"读寄存器"结果栏 + 写入操作日志 |

---

### 6. Bug 修复记录

| 问题 | 原因 | 修复 |
|------|------|------|
| 点击"查询版本"无反应 | 发送 UProtocol 格式而非 Cardiff RPC | 实现 `gh_rpc_build_frame()`，替换全部 TX 路径 |
| RX 传感数据解析失败 | 直接将 RPC 帧头传入 `gh_protocol_process()` | 先用 `gh_rpc_parse_frame()` 剥离 RPC 头，再传裸数据 |
| 前端 API 响应检查失败 | `if (data.sent)` 检查错误字段 | 统一改为 `if (data.code === 0)` |
| 寄存器读命令字节错误 | `d32` TypeData=`0x68`（pack_type=0） | 修正为 `0x6A`（pack_type=2=SIGNED） |
| 寄存器读结果不显示 | 无 "GH3X_RegsReadCmd" 响应解析器 | 新增响应处理 + `[REG_VAL]` WebSocket 推送 |

---

## 构建与运行

### 依赖
- CMake ≥ 3.14
- GCC / Clang（C11）
- pthread

### 编译
```bash
cd refactor_output
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j4
```

### 运行
```bash
# 模拟器模式（无需硬件）
./gh_backend

# 连接真实设备
./gh_backend --port /dev/ttyUSB0

# 指定自定义波特率
./gh_backend --port /dev/ttyUSB0 --baud 400000
```

打开浏览器访问 `http://localhost:8080`。

### 操作流程
1. 在"串口设置"中选择端口，点击"连接"
2. 点击"获取版本"验证通信（调试窗口可见 TX/RX 字节）
3. 在"读寄存器"中输入地址（十六进制），点击"读取"
4. 导入 `.config` 配置文件并批量下发寄存器

---

## 目录结构

```
refactor_output/
├── CMakeLists.txt
├── main.c                          # 入口：服务初始化、log 回调
├── src/
│   ├── api/
│   │   ├── gh_http_server.h/.c     # Mongoose HTTP+WS 服务器、log 队列
│   ├── protocol/
│   │   ├── gh_rpc.h/.c             # Cardiff RPC 帧构建/解析（新增）
│   │   ├── gh_protocol.h/.c        # UProtocol 解析器（Cardiff A，保留备用）
│   │   └── chelsea_a/              # Chelsea A 传感数据解压（gh_protocol_process）
│   ├── service/
│   │   └── gh_service.h/.c         # 业务层：命令调度 + RX 路由
│   └── transport/
│       └── gh_transport.h/.c       # POSIX 串口 + AA11 帧检测 + TX/RX log
├── web/
│   └── frontend/
│       └── index.html              # 单文件前端（无构建工具）
├── third_party/
│   └── mongoose/                   # 自动下载 v7.14
└── configs/                        # 示例 .config 寄存器配置文件
```

---

详细开发文档：[docs/README.md](docs/README.md)
