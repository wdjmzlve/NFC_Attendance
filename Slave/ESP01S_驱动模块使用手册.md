# ESP01S WiFi 驱动模块使用手册

## 1. 驱动概述

ESP01S 驱动是 ESP-01S WiFi 模块的上层应用驱动，基于 UartDrv 通用串口驱动构建。驱动采用"单例实例 + 状态机"架构，封装 ESP-01S 模块的完整工作流程：AT 通信、WiFi 连接、NTP 网络授时、TCP 连接和透传模式数据收发。

| 项目 | 说明 |
|------|------|
| 文件 | `esp01s.c`、`esp01s.h` |
| 依赖 | UartDrv 串口驱动、STM32 HAL RTC |
| 架构 | 单例模式 + 状态机 |
| NTP 授时 | AT SNTP 优先，失败自动回退 UDP NTP |

### 连接流程

`ESP01S_Start()` 完成以下步骤：

1. 退出透传模式（防止上次遗留）
2. AT 通信测试
3. 关闭回显和残留连接
4. 配置单连接模式（CIPMUX=0，透传前提）
5. 查询模块信息和当前连接状态
6. 连接 WiFi 热点
7. NTP 授时（可选）
8. 连接 TCP 服务器
9. 开启透传模式

---

## 2. 模块架构

```
┌─────────────────────────────────────────────────┐
│                  用户应用层                     │
│    ESP01S_SendStr()   ESP01S_GetDateTime()      │
│    ESP01S_RegisterDataCb()                      │
└────────────────────┬────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────┐
│              ESP01S 驱动层（单例）              │
│                                                 │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐    │
│  │ 状态机    │  │ AT解析器  │  │ NTP授时   │    │
│  │ IDLE      │  │ OK/WIFI   │  │ AT SNTP   │    │
│  │ →AT_OK    │  │ TCP/      │  │ UDP NTP   │    │
│  │ →WIFI     │  │ CWSAP/    │  │ RTC校准   │    │
│  │ →TCP      │  │ CIPSNTPTIME│ │ 时间推算  │    │
│  │ →透传     │  │           │  │           │    │
│  └───────────┘  └───────────┘  └───────────┘    │
│                     │                           │
│            ESP01S_RxCallback                    │
│            (串口接收回调)                       │
└────────────────────┬────────────────────────────┘
                     │ 依赖
┌────────────────────▼────────────────────────────┐
│              UartDrv 串口驱动层                 │
│         (提供串口收发和回调分发)                │
└─────────────────────────────────────────────────┘
```

**核心设计模式**：
- **单例模式**：驱动内部只有一个 `s_esp01s` 实例，全局共享。
- **状态机**：通过 `ESP01S_State_t` 枚举管理连接生命周期。
- **双模式 NTP**：AT SNTP 优先，失败自动回退到 UDP NTP。

---

## 3. 核心数据结构

### ESP01S_State_t — 连接状态

```c
typedef enum {
    ESP01S_STATE_IDLE = 0,          // 空闲，未初始化
    ESP01S_STATE_AT_OK,             // AT通信正常
    ESP01S_STATE_WIFI_CONNECTING,   // 正在连接WiFi
    ESP01S_STATE_WIFI_CONNECTED,    // WiFi已连接
    ESP01S_STATE_TCP_CONNECTING,    // 正在连接TCP服务器
    ESP01S_STATE_TCP_CONNECTED,     // TCP已连接
    ESP01S_STATE_TRANSPARENT,       // 透传模式
} ESP01S_State_t;
```

状态转移图：

```
IDLE ──AT测试──→ AT_OK ──连WiFi──→ WIFI_CONNECTING ──OK──→ WIFI_CONNECTED
                                                         │
                                          NTP授时(可选)  │
                                                         ▼
                                              TCP_CONNECTING ──CONNECT──→ TCP_CONNECTED
                                                                             │
                                                              CIPMODE=1+CIPSEND
                                                                             ▼
                                                                        TRANSPARENT
                                                                             │
                                                              ESP01S_ExitTransparent()
                                                                             ▼
                                                                        TCP_CONNECTED
```

### ESP01S_Config_t — 配置结构

```c
typedef struct {
    char     ssid[32];              // WiFi热点名称
    char     password[64];          // WiFi密码
    char     tcpServerIP[48];       // TCP服务器IP地址
    uint16_t tcpPort;               // TCP服务器端口号
    char     ntpServer[48];         // NTP服务器域名(空字符串=禁用)
    int8_t   ntpTimezone;           // 时区(中国=8)
} ESP01S_Config_t;
```

### ESP01S_NtpTime_t — NTP 时间结构

```c
typedef struct {
    uint16_t year;      // 年 (2024~)
    uint8_t  month;     // 月 (1~12)
    uint8_t  day;       // 日 (1~31)
    uint8_t  hour;      // 时 (0~23, 已含时区偏移)
    uint8_t  minute;    // 分 (0~59)
    uint8_t  second;    // 秒 (0~59)
    uint8_t  weekday;   // 星期 (1=Mon ~ 7=Sun)
} ESP01S_NtpTime_t;
```

### DT_Format_t — 时间格式枚举

```c
typedef enum {
    DT_DATE = 0,        // "2026-04-12"
    DT_TIME,            // "14:30:45"
    DT_ALL,             // "2026-04-12 14:30:45"
} DT_Format_t;
```

### ESP01S_DataCallback_t — 透传数据回调

```c
typedef void (*ESP01S_DataCallback_t)(const uint8_t *pData, uint16_t len, void *pUserCtx);
```

---

## 4. API 参考

### 初始化与启停

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `ESP01S_Init(UartDrv_t *pUartDrv)` | 初始化驱动（使用默认配置） | 无 |
| `ESP01S_Start(void)` | 启动完整流程：AT→WiFi→NTP→TCP→透传 | 0:成功, -1:AT失败, -2:WiFi失败, -3:TCP失败 |
| `ESP01S_ExitTransparent(void)` | 退出透传模式 | 无 |

### 配置 API

| 函数 | 说明 |
|------|------|
| `ESP01S_SetConfig(const ESP01S_Config_t *pConfig)` | 一次性设置全部配置 |
| `ESP01S_GetConfig(void)` | 获取当前配置（只读） |
| `ESP01S_SetWiFi(const char *ssid, const char *password)` | 设置 WiFi 参数 |
| `ESP01S_SetTcpServer(const char *ip, uint16_t port)` | 设置 TCP 服务器参数 |
| `ESP01S_SetNtpServer(const char *server, int8_t timezone)` | 设置 NTP 服务器参数 |

> **注意**：修改配置后需重新调用 `ESP01S_Start()` 才会生效。

### 数据收发

| 函数 | 说明 |
|------|------|
| `ESP01S_SendATCmd(const char *cmd, uint32_t waitMs)` | 发送 AT 指令并等待 |
| `ESP01S_SendStr(const char *str)` | 透传模式发送字符串 |
| `ESP01S_SendData(const uint8_t *pData, uint16_t len)` | 透传模式发送二进制数据 |
| `ESP01S_RegisterDataCb(ESP01S_DataCallback_t pCb, void *pUserCtx)` | 注册透传数据接收回调 |

### 状态查询

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `ESP01S_GetState(void)` | 获取当前连接状态 | `ESP01S_State_t` |
| `ESP01S_IsWiFiConnected(void)` | 检查 WiFi 是否已连接 | 1:已连接, 0:未连接 |
| `ESP01S_IsNtpSynced(void)` | 检查 NTP 是否已同步 | 1:已同步, 0:未同步 |

### NTP 授时

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `ESP01S_SyncNtpTime(void)` | 同步 NTP 网络时间 | 无 |
| `ESP01S_GetNtpTime(ESP01S_NtpTime_t *pTime)` | 获取 NTP 授时结果 | 0:有效, -1:无效 |
| `ESP01S_SetRtcFromNtp(RTC_HandleTypeDef *pRtc)` | 将 NTP 时间写入 RTC | 0:成功, -1:无效, -2:写入失败 |

### 时间获取

| 函数 | 说明 | 返回值 |
|------|------|--------|
| `ESP01S_GetDateTime(RTC_HandleTypeDef *pRtc, DT_Format_t format, char *pBuf, uint16_t bufLen)` | 获取当前时间字符串 | 0:成功, -1:无时间源 |

时间来源优先级：RTC（最精确）> NTP+Tick 推算（有轻微累积误差）

### FreeRTOS 扩展

| 函数 | 说明 |
|------|------|
| `ESP01S_RegisterRxQueue(osMessageQueueId_t queue)` | 注册透传数据接收队列 |
| `ESP01S_DispatchTransparentData(const uint8_t *pData, uint16_t len)` | 在任务上下文中分发透传数据到用户回调 |

---

## 5. 使用示例

### 最简使用 — 使用默认配置

```c
#include "uart_drv.h"
#include "esp01s.h"

UartDrv_t g_uart1Drv;   // 调试串口
UartDrv_t g_uart4Drv;   // ESP01S串口

/* 透传数据接收回调 */
void Esp01s_DataCb(const uint8_t *pData, uint16_t len, void *pUserCtx)
{
    /* 处理TCP服务器发来的数据 */
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART1_UART_Init();
    MX_UART4_Init();
    MX_RTC_Init();

    /* 1. 初始化串口驱动 */
    UartDrv_Init(&g_uart1Drv, &huart1);
    UartDrv_Init(&g_uart4Drv, &huart4);

    /* 2. 设置调试输出口 */
    UartDrv_SetDebugPort(&g_uart1Drv);
    printf("系统启动\r\n");

    /* 3. 初始化并启动ESP01S */
    ESP01S_Init(&g_uart4Drv);
    ESP01S_RegisterDataCb(Esp01s_DataCb, NULL);

    int ret = ESP01S_Start();
    if (ret != 0) {
        printf("ESP01S启动失败: %d\r\n", ret);
    }

    /* 4. 将NTP时间写入RTC */
    ESP01S_SetRtcFromNtp(&hrtc);

    /* 5. 透传模式下发送数据 */
    ESP01S_SendStr("Hello Server!\r\n");

    /* 6. 获取当前时间 */
    char timeBuf[24];
    if (ESP01S_GetDateTime(&hrtc, DT_ALL, timeBuf, sizeof(timeBuf)) == 0) {
        printf("当前时间: %s\r\n", timeBuf);
    }

    while (1) { }
}
```

### 运行时修改配置

```c
/* 方式1: 分项修改 */
ESP01S_SetWiFi("MyWiFi", "12345678");
ESP01S_SetTcpServer("192.168.1.100", 8080);
ESP01S_SetNtpServer("pool.ntp.org", 8);
ESP01S_Start();

/* 方式2: 一次性设置全部配置 */
ESP01S_Config_t cfg = {
    .ssid        = "MyWiFi",
    .password    = "12345678",
    .tcpServerIP = "192.168.1.100",
    .tcpPort     = 8080,
    .ntpServer   = "ntp.aliyun.com",
    .ntpTimezone = 8,
};
ESP01S_SetConfig(&cfg);
ESP01S_Start();

/* 方式3: 直接修改esp01s.c中的s_defaultConfig(推荐) */
```

### 查询连接状态

```c
ESP01S_State_t state = ESP01S_GetState();
switch (state) {
case ESP01S_STATE_IDLE:           printf("未初始化\r\n"); break;
case ESP01S_STATE_AT_OK:          printf("AT通信正常\r\n"); break;
case ESP01S_STATE_WIFI_CONNECTED: printf("WiFi已连接\r\n"); break;
case ESP01S_STATE_TCP_CONNECTED:  printf("TCP已连接\r\n"); break;
case ESP01S_STATE_TRANSPARENT:    printf("透传模式\r\n"); break;
default:                          printf("连接中...\r\n"); break;
}
```

### 退出透传模式并重新配置

```c
ESP01S_ExitTransparent();
ESP01S_SetTcpServer("192.168.1.200", 9090);
ESP01S_Start();
```

---

## 6. NTP 授时详解

### 双模式自动回退

```
ESP01S_SyncNtpTime()
        │
        ▼
  AT+CIPSNTPCFG (配置时区和服务器)
  AT+CIPSNTPTIME? (查询时间)
        │
        ▼
  年份 > 2000? ──是──→ AT SNTP成功
        │
       否
        │
        ▼
  关闭已有连接
  AT+CIPSTART="UDP","ntp.aliyun.com",123
  AT+CIPSEND=48 → 发送NTP请求包
  等待3秒(回调中解析+IPD响应)
  关闭UDP连接 → UDP NTP成功/失败
```

### 时间延迟补偿

NTP 授时成功时记录 `ntpTick`（HAL_GetTick 值）和 `ntpUnixTimestamp`（UTC 时间戳），后续通过差值推算当前时间，消除 TCP 连接等操作的延迟误差。

### NTP 服务器推荐

| 服务器 | 说明 |
|--------|------|
| `ntp.aliyun.com` | 阿里云 NTP（默认，国内推荐） |
| `cn.ntp.org.cn` | 中国 NTP |
| `pool.ntp.org` | 全球 NTP 池 |

---

## 7. 透传模式说明

透传模式下，STM32 通过串口发送的数据直接传输到 TCP 服务器，无需 AT 指令封装。

**前提条件**：CIPMUX=0（单连接）、CIPMODE=1（透传模式）、TCP 已连接。

**退出时序**：`+++` 前后各需 500ms 静默，且不能加 `\r\n`（否则会被当作透传数据发送）。

## 9. NFCAttend 工程实际应用

NFCAttend 考勤系统使用 ESP01S 实现 WiFi 连接、NTP 校时和 TCP 透传数据上报。

### 初始化流程（freertos.c → StartDefaultTask）

```c
static UartDrv_t g_uart6Drv;  // USART6 驱动实例

/* USART6: ESP01S (WiFi模块) */
UartDrv_Init(&g_uart6Drv, &huart6);
UartDrv_StartRecv(&g_uart6Drv);

/* 初始化协议模块 */
PROTOCOL_Init();

/* 启动 ESP01S WiFi 连接 */
ESP01S_Init(&g_uart6Drv);
RECORD_UPLOAD_Init();

int esp_ret = ESP01S_Start();
if (esp_ret == 0 || esp_ret == -3) {
    /* 成功或仅TCP失败(服务器未启动时豁免), WiFi和NTP可用 */
    /* NTP校时成功后写入RTC */
    if (ESP01S_IsNtpSynced()) {
        ESP01S_SetRtcFromNtp(&hrtc);
    }
    /* 标记天气模块可以查询了 */
    Weather_SetReady();
} else {
    printf("[ESP01S] Start failed: %d\r\n", esp_ret);
}
```

### 天气查询（weather.c）

NFCAttend 使用 `ESP01S_QueryWeather()` 查询心知天气日报：

```c
/* 临时退出透传 → 关闭TCP → UDP查询天气 → 恢复TCP透传 */
int ret = ESP01S_QueryWeather(apiKey, "hangzhou", "zh-Hans", "c",
    city, sizeof(city),
    textDay, sizeof(textDay),
    high, sizeof(high),
    textNight, sizeof(textNight),
    low, sizeof(low),
    precip, sizeof(precip));
```

### 考勤记录上传（record_upload.c）

NFCAttend 通过 ESP01S 透传模式将考勤记录上传到服务器：

```c
/* 通过透传发送考勤记录 */
ESP01S_SendStr("{\"type\":\"record\",\"uid\":12345,\"time\":\"2026-06-26 08:30:00\"}\n");
```

> **设计要点**：NFCAttend 对 ESP01S_Start() 返回 -3（TCP 连接失败）做了豁免处理，因为默认 TCP 服务器可能未启动，但 WiFi 和 NTP 功能仍可使用，天气查询功能也可用（内部会切换连接模式）。

---

## 8. 注意事项与常见问题

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| AT 通信失败 | 接线错误/波特率不匹配/模块未上电 | 检查 TX/RX 交叉连接，确认波特率一致 |
| WiFi 连接失败 | SSID 或密码错误/信号弱 | 检查配置，确保 2.4GHz 频段 |
| TCP 连接失败 | 服务器未启动/IP 错误/防火墙 | 确认服务器监听状态和 IP 可达性 |
| NTP 授时失败 | 无法访问 NTP 服务器/DNS 解析失败 | 更换 NTP 服务器或检查网络 |
| 透传发送无效 | 未进入透传模式 | 检查 `ESP01S_GetState()` 是否为 `TRANSPARENT` |
| 退出透传失败 | +++时序不对 | 确保前后各 500ms 静默，不加换行符 |
| RTC 时间不对 | 未调用 `ESP01S_SetRtcFromNtp` | NTP 授时后调用此函数写入 RTC |
| CIPSEND 报错 | CIPMODE 仍为 1（上次遗留） | `ESP01S_Start()` 已自动处理 |

**CIPMODE 状态持久性**：`AT+CIPMODE=1` 设置在 TCP 连接断开后仍然保持，下次启动必须先 `AT+CIPMODE=0` 清除。`ESP01S_Start()` 已自动处理此问题。

**配置修改方式**：
- 方式1：直接修改 `esp01s.c` 中的 `s_defaultConfig`（最简单）
- 方式2：调用 `ESP01S_SetConfig()` 一次性覆盖全部配置
- 方式3：调用 `ESP01S_SetWiFi/SetTcpServer/SetNtpServer` 分项修改
- 修改后需重新调用 `ESP01S_Start()` 才会生效
