# UartDrv 通用串口驱动模块使用手册

## 1. 驱动概述

UartDrv 是基于 STM32 HAL 库的通用串口驱动模块，采用"实例注册 + 回调分发"架构，支持多个串口外设同时工作。每个串口外设对应一个 `UartDrv_t` 实例，通过静态注册表管理。接收采用空闲中断（IDLE）模式，适合 AT 指令等变长协议。模块还提供 `printf` 重定向和纯整数浮点数打印功能。

| 项目 | 说明 |
|------|------|
| 文件 | `uart_drv.c`、`uart_drv.h` |
| 依赖 | STM32 HAL 库（`HAL_UARTEx_ReceiveToIdle_IT`） |
| 最大实例数 | `UART_DRV_MAX_INSTANCES`（8） |
| 接收缓冲区 | `UART_DRV_RX_BUF_SIZE`（512 字节） |
| 接收模式 | 空闲中断（IDLE），变长帧自动接收 |

### 功能特性

- **多实例管理**：每个串口外设对应一个 `UartDrv_t` 实例，最多支持 8 个串口同时工作
- **空闲中断接收**：使用 `HAL_UARTEx_ReceiveToIdle_IT`，总线空闲一个字符时间后触发中断，一次性返回整帧数据，无需预知数据长度
- **回调分发**：所有实例的接收事件通过 `HAL_UARTEx_RxEventCallback` 自动分发到对应实例的回调函数
- **自动重启接收**：回调结束后驱动自动重启接收，无需用户手动操作
- **printf 重定向**：`UartDrv_SetDebugPort()` 设置后，`printf` 输出自动重定向到指定串口，同时支持 MDK（ARMCC）和 GCC（CMake）工具链
- **浮点数打印**：`UartDrv_PrintFloat()` 使用纯整数运算，不依赖 `printf %f`，在 GCC newlib-nano 环境下也能正常工作
- **消息队列支持**：FreeRTOS 环境下可注册接收消息队列，将接收事件以数据副本形式入队，消费者可安全使用

---

## 2. 模块架构

```
┌─────────────────────────────────────────────────────┐
│                    用户应用层                       │
│  UartDrv_Send() / UartDrv_SendStr()                 │
│  UartDrv_RegisterRxCb() / printf()                  │
└──────────────────────┬──────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────┐
│                UartDrv 驱动层                       │
│                                                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────┐   │
│  │ 实例注册表    │  │ 接收数据结构  │  │调试端口  │   │
│  │ s_drvInstances│  │ UartDrv_RxData_t│ │s_pDebugPort│   │
│  │ [8]          │  │  rx_buf[512] │  │          │   │
│  └──────┬───────┘  └──────┬───────┘  └──────────┘   │
│         │                 │                         │
│    FindInstance()   RxEventDispatch()   _write()/fputc()│
└─────────┼─────────────────┼─────────────┼───────────┘
          │                 │             │
┌─────────▼─────────────────▼─────────────▼───────────┐
│              STM32 HAL / 硬件                       │
│  HAL_UARTEx_ReceiveToIdle_IT  HAL_UART_Transmit    │
│  HAL_UARTEx_RxEventCallback                        │
└─────────────────────────────────────────────────────┘
```

### 接收数据流

```
串口硬件收到数据 → 总线空闲触发中断
    │
    ▼
HAL_UARTEx_RxEventCallback(huart, Size)
    │
    ▼
UartDrv_RxEventDispatch(huart, Size)
    │  1. 遍历注册表查找 huart 对应的实例
    │  2. 填充 rxData.rx_buf 和 rxData.rx_len
    │  3. 在缓冲区末尾添加 '\0' (方便字符串处理)
    ▼
用户回调函数 pRxCb(pData, pUserCtx)
    │  用户在中断上下文中处理数据
    ▼
驱动自动重启接收 (HAL_UARTEx_ReceiveToIdle_IT)
    └─ 等待下一帧数据
```

---

## 3. 核心数据结构

### UartDrv_RxData_t — 接收数据结构

```c
typedef struct {
    uint16_t rx_len;                        // 本帧接收数据长度(字节数)
    uint8_t  rx_buf[UART_DRV_RX_BUF_SIZE];  // 接收数据缓冲区(回调结束后可能被覆盖)
} UartDrv_RxData_t;
```

> **注意**：`rx_buf` 在回调返回后会被下一帧数据覆盖，回调函数中应尽快处理数据或拷贝到用户自己的缓冲区。

### UartDrv_RxCallback_t — 接收回调函数类型

```c
typedef void (*UartDrv_RxCallback_t)(UartDrv_RxData_t *pData, void *pUserCtx);
```

回调在中断上下文中执行，应尽快返回，不要做耗时操作。

### UartDrv_t — 串口驱动实例结构

```c
typedef struct {
    UART_HandleTypeDef    *pUartHandle;     // HAL UART句柄指针
    UartDrv_RxData_t      rxData;           // 接收数据(中断中填充,回调中读取)
    UartDrv_RxCallback_t  pRxCb;            // 用户注册的接收回调函数
    void                  *pUserCtx;        // 回调用户上下文
    osMessageQueueId_t    rxQueue;          // 接收消息队列句柄(FreeRTOS)
    uint8_t               initialized;      // 初始化标志
} UartDrv_t;
```

### UartDrv_QueueEvent_t — 队列事件结构（FreeRTOS）

```c
typedef struct {
    UartDrv_t   *pDrv;                          // 来源串口驱动实例
    uint16_t     len;                           // 接收数据长度
    uint8_t      data[UART_DRV_QUEUE_DATA_SIZE]; // 数据副本
} UartDrv_QueueEvent_t;
```

> 队列事件中的 `data[]` 是数据副本，消费者可安全使用，不怕被下一帧覆盖。

### 编译配置宏

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `UART_DRV_MAX_INSTANCES` | 8 | 静态注册表最大实例数 |
| `UART_DRV_RX_BUF_SIZE` | 512 | 每个实例的接收缓冲区大小（字节） |
| `UART_DRV_NO_FREERTOS` | 未定义 | 定义后禁用 FreeRTOS 相关功能（队列等） |
| `UART_DRV_NO_DEFAULT_CALLBACK` | 未定义 | 定义后禁用默认中断回调，需自行实现 |
| `UART_DRV_NO_PRINTF_FLOAT` | 未定义 | 定义后禁用 GCC `_printf_float` 引用 |

---

## 4. API 参考

| 函数 | 说明 |
|------|------|
| `void UartDrv_Init(UartDrv_t *pDrv, UART_HandleTypeDef *pUartHandle)` | 初始化串口驱动实例，绑定 UART 句柄并注册到内部表 |
| `void UartDrv_StartRecv(UartDrv_t *pDrv)` | 启动空闲中断接收，此后自动接收并回调 |
| `void UartDrv_Send(UartDrv_t *pDrv, const uint8_t *pData, uint16_t len)` | 阻塞式发送数据 |
| `void UartDrv_SendStr(UartDrv_t *pDrv, const char *str)` | 阻塞式发送字符串 |
| `void UartDrv_RegisterRxCb(UartDrv_t *pDrv, UartDrv_RxCallback_t pCb, void *pUserCtx)` | 注册接收回调函数 |
| `void UartDrv_RegisterRxQueue(UartDrv_t *pDrv, osMessageQueueId_t queue)` | 注册接收消息队列（FreeRTOS） |
| `void UartDrv_SetDebugPort(UartDrv_t *pDrv)` | 设置 printf 调试输出串口，传 NULL 禁用 |
| `void UartDrv_PrintFloat(UartDrv_t *pDrv, float value, uint8_t decimalPlaces)` | 纯整数运算打印浮点数 |
| `void UartDrv_RxEventDispatch(UART_HandleTypeDef *huart, uint16_t Size)` | 接收事件分发（由 HAL 回调自动调用） |
| `void UartDrv_OnRxEvent(UART_HandleTypeDef *huart, uint16_t Size)` | 接收事件弱钩子（用户可重写） |

---

## 5. 使用示例

### 基本使用（回调方式）

```c
#include "uart_drv.h"
#include "usart.h"

static UartDrv_t g_uart1Drv;  // 串口驱动实例

/* 接收回调函数 (在中断上下文中执行) */
static void MyRxCallback(UartDrv_RxData_t *pData, void *pUserCtx)
{
    (void)pUserCtx;
    /* 处理接收到的数据: pData->rx_buf, pData->rx_len */
    /* 注意: 回调中应尽快返回, 不要做耗时操作 */
}

void UartInit(void)
{
    /* 1. 初始化驱动实例 (绑定 huart1) */
    UartDrv_Init(&g_uart1Drv, &huart1);

    /* 2. 注册接收回调 */
    UartDrv_RegisterRxCb(&g_uart1Drv, MyRxCallback, NULL);

    /* 3. 启动接收 */
    UartDrv_StartRecv(&g_uart1Drv);

    /* 4. (可选) 设置为 printf 调试输出口 */
    UartDrv_SetDebugPort(&g_uart1Drv);

    /* 5. 发送数据 */
    UartDrv_SendStr(&g_uart1Drv, "Hello UART!\r\n");
    printf("printf output goes to UART1\r\n");
}
```

### 多串口实例

```c
#include "uart_drv.h"
#include "usart.h"

static UartDrv_t g_uart1Drv;  // USART1: 调试串口
static UartDrv_t g_uart6Drv;  // USART6: WiFi模块

/* USART1 接收回调 */
static void UART1_RxCallback(UartDrv_RxData_t *pData, void *pUserCtx)
{
    /* 处理 PC 发来的数据 */
    printf("[RX1] %.*s\r\n", pData->rx_len, pData->rx_buf);
}

/* USART6 接收回调 */
static void UART6_RxCallback(UartDrv_RxData_t *pData, void *pUserCtx)
{
    /* 处理 WiFi 模块发来的数据 */
}

void MultiUartInit(void)
{
    /* USART1: 调试 + PC通信 */
    UartDrv_Init(&g_uart1Drv, &huart1);
    UartDrv_RegisterRxCb(&g_uart1Drv, UART1_RxCallback, NULL);
    UartDrv_StartRecv(&g_uart1Drv);
    UartDrv_SetDebugPort(&g_uart1Drv);

    /* USART6: WiFi模块 */
    UartDrv_Init(&g_uart6Drv, &huart6);
    UartDrv_RegisterRxCb(&g_uart6Drv, UART6_RxCallback, NULL);
    UartDrv_StartRecv(&g_uart6Drv);
}
```

### 消息队列方式（FreeRTOS）

```c
#include "uart_drv.h"
#include "cmsis_os.h"

static UartDrv_t g_uart1Drv;
osMessageQueueId_t g_uartQueue;

/* 消费者任务 */
void UartConsumerTask(void *arg)
{
    UartDrv_QueueEvent_t evt;
    for (;;) {
        if (osMessageQueueGet(g_uartQueue, &evt, NULL, osWaitForever) == osOK) {
            /* 安全处理数据副本, 不怕被下一帧覆盖 */
            printf("RX: %.*s\r\n", evt.len, evt.data);
        }
    }
}

void UartQueueInit(void)
{
    /* 创建消息队列 (msg_size 必须等于 sizeof(UartDrv_QueueEvent_t)) */
    g_uartQueue = osMessageQueueNew(8, sizeof(UartDrv_QueueEvent_t), NULL);

    UartDrv_Init(&g_uart1Drv, &huart1);
    UartDrv_RegisterRxQueue(&g_uart1Drv, g_uartQueue);
    UartDrv_StartRecv(&g_uart1Drv);
}
```

### 浮点数打印

```c
/* 方式1: printf %f (GCC 已自动链接 _printf_float) */
printf("Temp: %.2f C\r\n", 25.6f);

/* 方式2: UartDrv_PrintFloat (纯整数运算, 无额外依赖) */
UartDrv_PrintFloat(&g_uart1Drv, 25.6f, 2);  // 输出 "25.60"
UartDrv_PrintFloat(&g_uart1Drv, -0.5f, 1);  // 输出 "-0.5"
```

---

## 6. NFCAttend 工程实际应用

NFCAttend 考勤系统使用 UartDrv 驱动两个串口外设：

| 串口 | 用途 | 配置 |
|------|------|------|
| USART1 | PC 通信（发卡协议）+ printf 调试 | 注册接收回调 + SetDebugPort |
| USART6 | ESP01S WiFi 模块通信 | 注册接收回调（由 ESP01S 驱动内部管理） |

### NFCAttend 中的初始化流程（freertos.c → StartDefaultTask）

```c
static UartDrv_t g_uart1Drv;    // USART1驱动实例 (发卡串口)
static UartDrv_t g_uart6Drv;    // USART6驱动实例 (ESP01S)

static uint8_t  s_uart1_rx_buf[256];
static uint16_t s_uart1_rx_len = 0;
static uint8_t  s_uart1_data_ready = 0;

/* USART1接收回调 (中断上下文) */
static void UART1_RxCallback(UartDrv_RxData_t *pData, void *pUserCtx)
{
    (void)pUserCtx;
    uint16_t copy_len = pData->rx_len;
    if (copy_len > sizeof(s_uart1_rx_buf))
        copy_len = sizeof(s_uart1_rx_buf);
    memcpy(s_uart1_rx_buf, pData->rx_buf, copy_len);
    s_uart1_rx_len = copy_len;
    s_uart1_data_ready = 1;  // 通知任务处理
}

void StartDefaultTask(void *argument)
{
    /* USART1: 发卡串口 + printf调试 */
    UartDrv_Init(&g_uart1Drv, &huart1);
    UartDrv_RegisterRxCb(&g_uart1Drv, UART1_RxCallback, NULL);
    UartDrv_StartRecv(&g_uart1Drv);
    UartDrv_SetDebugPort(&g_uart1Drv);  // printf输出到USART1

    /* USART6: ESP01S WiFi */
    UartDrv_Init(&g_uart6Drv, &huart6);
    UartDrv_StartRecv(&g_uart6Drv);

    /* ESP01S驱动内部会注册自己的接收回调 */
    ESP01S_Init(&g_uart6Drv);

    printf("System Init Complete!\r\n");
}
```

### NFCAttend 中的接收处理任务（freertos.c → StartTaskUart）

```c
void StartTaskUart(void *argument)
{
    for(;;)
    {
        /* 检查 USART1 是否有数据 */
        if (s_uart1_data_ready) {
            /* 交给协议层处理发卡指令 */
            PROTOCOL_ProcessData(s_uart1_rx_buf, s_uart1_rx_len);
            s_uart1_data_ready = 0;
        }
        osDelay(10);
    }
}
```

> **设计要点**：中断回调中仅做数据拷贝和标志置位，实际协议解析在任务上下文中完成，避免中断中耗时处理。

---

## 7. 注意事项

- **中断上下文**：接收回调在中断上下文中执行，应尽快返回。建议回调中仅做数据拷贝和标志置位，耗时处理放到任务中。
- **缓冲区覆盖**：`rx_buf` 在回调返回后会被下一帧数据覆盖，回调中应将数据拷贝到用户缓冲区。
- **发送阻塞**：`UartDrv_Send`/`UartDrv_SendStr` 内部调用 `HAL_UART_Transmit`（阻塞），不建议在中断上下文中调用。
- **自动重启**：回调结束后驱动自动重启接收，无需用户手动调用 `UartDrv_StartRecv`。
- **实例上限**：受 `UART_DRV_MAX_INSTANCES`（默认 8）限制，超出时 `UartDrv_Init` 不会注册新实例。
- **printf 重定向**：`UartDrv_SetDebugPort` 同时支持 MDK（重写 `fputc`）和 GCC（重写 `_write`），无需额外配置。GCC 下 `printf %f` 已自动启用。
- **弱钩子函数**：`UartDrv_OnRxEvent` 是弱定义函数，用户可在自己的 `.c` 文件中重写，在分发完成后追加额外处理。
- **HAL 依赖**：调用 `UartDrv_Init` 前需确保 HAL UART 已初始化（`MX_USARTx_UART_Init` 已完成）。
