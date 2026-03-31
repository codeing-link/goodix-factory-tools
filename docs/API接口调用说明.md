# GH Protocol API 接口调用说明

## 1. 基本信息
- 默认服务地址：`http://localhost:8080`
- API 前缀：`/api`
- WebSocket：`ws://localhost:8080/ws`
- 响应统一格式（HTTP 状态码通常为 200）：
```json
{
  "code": 0,
  "msg": "ok",
  "data": {}
}
```
- `code = 0` 表示成功，`code != 0` 表示业务错误。

## 2. 接口总览

### 2.1 设备控制类
- `GET /api/device/status`
- `POST /api/device/connect`
- `POST /api/device/disconnect`
- `POST /api/device/start`
- `POST /api/device/config`
- `POST /api/device/chip_ctrl`
- `POST /api/device/work_mode`
- `GET /api/device/version`
- `POST /api/device/read_reg`
- `POST /api/device/protocol`
- `GET /api/device/data`
- `GET /api/device/csv_rows`

### 2.2 CSV 分析类
- `POST /api/csv/led_metrics`
- `POST /api/csv/noise_metric`

### 2.3 系统信息类
- `GET /api/build_info`
- `GET /api/serial/list`

### 2.4 WebSocket
- `WS /ws`

## 3. 详细调用说明

## 3.1 `GET /api/device/status`
作用：查询当前设备状态、串口、波特率、是否模拟模式。  
请求示例：
```bash
curl -s http://localhost:8080/api/device/status
```
响应示例：
```json
{
  "code": 0,
  "msg": "ok",
  "data": {
    "state": "connected",
    "port": "COM28",
    "baud_rate": 115200,
    "sim_mode": false
  }
}
```

## 3.2 `POST /api/device/connect`
作用：连接串口设备。  
请求体：
- `port`：串口名（如 `COM28`、`/dev/tty.usbserial-310`）
- `baud_rate`：波特率
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/connect \
  -H "Content-Type: application/json" \
  -d '{"port":"COM28","baud_rate":115200}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"state":"connected"}}
```

## 3.3 `POST /api/device/disconnect`
作用：断开设备连接。  
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/disconnect \
  -H "Content-Type: application/json" -d '{}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"state":"disconnected"}}
```

## 3.4 `POST /api/device/start`
作用：开始/停止采样。  
请求体：
- `ctrl`：`0`=开始，`1`=停止
- `mode`：模式值（常用 `0`）
- `func_mask`：功能掩码（当前常用 `0x40`）
- `config_name`：开始采样时可传，决定 CSV 文件名
开始示例：
```bash
curl -s -X POST http://localhost:8080/api/device/start \
  -H "Content-Type: application/json" \
  -d '{"ctrl":0,"mode":0,"func_mask":64,"config_name":"LPCTR_TEST1_100Hz_0327"}'
```
停止示例：
```bash
curl -s -X POST http://localhost:8080/api/device/start \
  -H "Content-Type: application/json" \
  -d '{"ctrl":1,"mode":0,"func_mask":64}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"state":"sampling"}}
```

## 3.5 `POST /api/device/config`
作用：下发寄存器列表。  
请求体：
- `regs`：数组，元素结构 `{ "addr": <int>, "data": <int> }`
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/config \
  -H "Content-Type: application/json" \
  -d '{"regs":[{"addr":22,"data":31},{"addr":23,"data":42}]}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"sent":true,"count":2}}
```

## 3.6 `POST /api/device/chip_ctrl`
作用：发送芯片控制命令。  
请求体：
- `ctrl_val`：控制值（如 `194` 即 `0xC2`）
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/chip_ctrl \
  -H "Content-Type: application/json" \
  -d '{"ctrl_val":194}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"sent":true}}
```

## 3.7 `POST /api/device/work_mode`
作用：设置工作模式。  
请求体：
- `mode`：`0`/`1`
- `func_mask`：功能掩码
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/work_mode \
  -H "Content-Type: application/json" \
  -d '{"mode":1,"func_mask":4294967295}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"sent":true}}
```

## 3.8 `GET /api/device/version`
作用：触发设备版本查询（异步返回到 WebSocket 日志）。  
请求示例：
```bash
curl -s http://localhost:8080/api/device/version
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"sent":true,"msg":"Async request issued"}}
```

## 3.9 `POST /api/device/read_reg`
作用：读取寄存器。  
请求体：
- `addr`：寄存器地址（十进制）
- `count`：读取个数
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/read_reg \
  -H "Content-Type: application/json" \
  -d '{"addr":258,"count":1}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"sent":true}}
```

## 3.10 `POST /api/device/protocol`
作用：切换协议解析器。  
请求体：
- `protocol`：当前支持 `"Chelsea_A"`
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/device/protocol \
  -H "Content-Type: application/json" \
  -d '{"protocol":"Chelsea_A"}'
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"success":true}}
```

## 3.11 `GET /api/device/data`
作用：获取最近一帧数据（轮询备用）。  
请求示例：
```bash
curl -s http://localhost:8080/api/device/data
```
响应示例（有数据时）：
```json
{
  "code": 0,
  "msg": "ok",
  "data": {
    "func_id": 64,
    "frame_cnt": 1234,
    "raw": [0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
    "hr": 0,
    "ts": 1730000000000
  }
}
```

## 3.12 `GET /api/device/csv_rows`
作用：获取当前采样 CSV 已写入行数（不含表头），用于“至少 200 条”判定。  
请求示例：
```bash
curl -s http://localhost:8080/api/device/csv_rows
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"rows":200}}
```

## 3.13 `POST /api/csv/led_metrics`
作用：计算 LPCTR/LPLCTR 的 LED 指标值数组。  
请求体：
- `csv_name`：CSV 文件名（不含路径）
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/csv/led_metrics \
  -H "Content-Type: application/json" \
  -d '{"csv_name":"LPCTR_TEST1_100Hz_0327.csv"}'
```
响应示例：
```json
{
  "code": 0,
  "msg": "ok",
  "data": {
    "csv": "LPCTR_TEST1_100Hz_0327.csv",
    "row_cnt": 200,
    "window_rows": 100,
    "non_zero_ch": 16,
    "start_ch": 8,
    "count": 8,
    "values": [1.12,0.98,1.03,0.95,1.01,0.99,1.05,0.97]
  }
}
```

## 3.14 `POST /api/csv/noise_metric`
作用：计算 BaseNoise/PPGNoise 通道噪声值。  
请求体：
- `csv_name`：CSV 文件名（不含路径）
请求示例：
```bash
curl -s -X POST http://localhost:8080/api/csv/noise_metric \
  -H "Content-Type: application/json" \
  -d '{"csv_name":"PPG_Noise_TEST1_100Hz_0327.csv"}'
```
响应示例：
```json
{
  "code": 0,
  "msg": "ok",
  "data": {
    "csv": "PPG_Noise_TEST1_100Hz_0327.csv",
    "row_cnt": 200,
    "window_rows": 100,
    "non_zero_ch": 4,
    "used_ch": 2,
    "noise_value": 165.42,
    "values": [158.33,172.51]
  }
}
```

## 3.15 `GET /api/build_info`
作用：获取后端版本和编译时间（后端重编译后更新）。  
请求示例：
```bash
curl -s http://localhost:8080/api/build_info
```
响应示例：
```json
{"code":0,"msg":"ok","data":{"backend_version":"V1.00","build_time":"Mar 31 2026 15:21:07"}}
```

## 3.16 `GET /api/serial/list`
作用：列出可选串口（当前为候选列表）。  
请求示例：
```bash
curl -s http://localhost:8080/api/serial/list
```
响应示例：
```json
{"code":0,"msg":"ok","data":["COM1","COM2","COM3","COM4","COM5","COM6"]}
```

## 4. WebSocket `WS /ws`
作用：实时推送状态、数据帧、日志。

连接示例（JavaScript）：
```javascript
const ws = new WebSocket("ws://localhost:8080/ws");
ws.onmessage = (e) => console.log(e.data);
```

消息示例：

1) `hello`（连接欢迎）  
```json
{"type":"hello","state":"connected","sim":false}
```

2) `data`（实时数据）  
```json
{
  "type":"data",
  "func_id":64,
  "frame_cnt":12,
  "raw":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0],
  "algo":[0],
  "hr":0,
  "gsensor":[0,0,512],
  "ts":1730000000123
}
```

3) `log`（调试日志）  
```json
{"type":"log","dir":"rx","text":"[RX] AA 11 ..."}
```

## 5. 常见错误码示例
- `-1 / -2 / -3 ...`：业务层错误（如未连接、参数缺失、发送失败）
- `404`：未知 API 路径（`/api/*` 下未实现）

建议：客户端判定成功请以 `code == 0` 为准，不要只看 HTTP 状态码。
