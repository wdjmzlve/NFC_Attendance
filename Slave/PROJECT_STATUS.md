# NFC Attendance System - Project Status & Handoff Document

> Generated: 2026-07-10
> Git branch: main | Last commit: 50565ac "增加考勤打卡功能"

---

## 1. Project Overview

| Item | Detail |
|------|--------|
| MCU | STM32F407VGT6 (Cortex-M4, 168MHz) |
| RTOS | FreeRTOS (CMSIS-RTOS v2) |
| Compiler | Keil MDK AC5 (ARMCC v5.06) |
| IDE | Keil uVision 5 |
| Language | C99 standard |
| Git | Yes, at `E:\KeilProject\NFC\NFC_Attendance\Slave` |

---

## 2. Hardware Pin Mapping

| Module | Interface | Pins |
|--------|-----------|------|
| OLED (SSD1306) | I2C1 | SCL=PB6, SDA=PB7 |
| RC522 (NFC) | SPI2 + CS | CS=PC1 (software) |
| W25Q128 (Flash) | SPI1 + CS | CS=PC4 (software) |
| USART1 (Debug/Issuing) | UART | TX=PA9, RX=PA10 @115200 |
| USART6 (ESP01S WiFi) | UART | TX=PC6, RX=PC7 @115200 |
| Keys x6 | GPIO Input | PE1(KEY1)~PE6(KEY6) |
| LEDs x8 | GPIO Output | PE8(LED1)~PE15(LED8) |
| Buzzer (MIDI PWM) | TIM3 CH1 → PB4 AF2 | PWM via midi.c |
| RTC | Internal (LSE) | 32.768kHz |

---

## 3. Project File Structure

```
Slave/
├── Core/
│   ├── Inc/
│   │   ├── app_tasks.h         # Task prototypes, CardInfo_t, KeyMsg_t, queue handles
│   │   ├── nfc_storage.h       # DeviceConfig_t, AttendanceRecord_t, LRU cache, storage APIs
│   │   ├── tim.h               # TIM1/TIM3 handles
│   │   ├── gpio.h / main.h / rtc.h / spi.h / i2c.h / usart.h
│   │   ├── FreeRTOSConfig.h / stm32f4xx_hal_conf.h / stm32f4xx_it.h
│   │   └── gui.h               # u8g2 GUI wrappers
│   └── Src/
│       ├── app_tasks.c          # ★ MAIN BUSINESS LOGIC (2048 lines)
│       │                        #    Task_Display, Task_CardRead, Task_KeyScan, Task_Serial
│       ├── nfc_storage.c        # ★ Flash storage implementation (471 lines)
│       ├── freertos.c           # Task creation + mutex/semaphore/queue init (193 lines)
│       ├── main.c               # Entry point, HAL init, MX_TIM3_Init
│       ├── stm32f4xx_it.c       # ISR handlers: TIM3, TIM7, USART1, USART6
│       ├── tim.c                # TIM1 base + TIM3 PWM init with NVIC
│       ├── usart.c              # USART1+USART6 init (115200)
│       ├── gpio.c / spi.c / i2c.c / rtc.c
│       ├── gui.c / system_stm32f4xx.c / stm32f4xx_hal_msp.c
│       └── stm32f4xx_hal_timebase_tim.c
├── Hardware/
│   ├── inc/
│   │   ├── esp01s.h             # ESP01S WiFi driver (single-instance, state machine)
│   │   ├── uart_drv.h           # Generic UART driver (idle-line IRQ + callback dispatch)
│   │   ├── rc522.h / w25q128.h / oled.h / midi.h / key.h / led.h
│   │   ├── bsp_rtc.h / ds18b20.h / delay_us.h
│   └── src/
│       ├── esp01s.c             # ★ ESP01S full implementation (1521 lines)
│       │                        #    AT→WiFi→NTP→TCP→Transparent pipeline
│       │                        #    Weather query (Seniverse API), DNS fallback
│       ├── uart_drv.c           # UartDrv + printf redirect (MDK+GCC), FloatPrint
│       ├── rc522.c / rc522_platform_stm32.c / w25q128.c / oled.c / midi.c
│       ├── key.c / led.c / bsp_rtc.c / ds18b20.c / delay_us.c
├── Drivers/                     # STM32F4 HAL + CMSIS (standard CubeMX)
├── Middlewares/
│   ├── FreeRTOS/                # CMSIS-RTOS v2
│   └── u8g2/                    # OLED graphics library
├── MDK-ARM/                     # Keil project files
│   └── Slave.uvprojx            # AC5 compiler, STM32F407VETx
├── nfc_design_p1-20.md          # PDF extract: course design doc pages 1-20
├── nfc_guidance_p1-20.md        # PDF extract: design guidance pages 1-20 (Phase 1+2)
├── nfc_guidance_p21-40.md       # PDF extract: design guidance pages 21-40 (Phase 3)
├── CLAUDE.md                    # Project AI instructions
├── *_驱动模块使用手册.md          # Hardware driver manuals (ESP01S, MIDI, UartDrv, W25Qxx)
└── PROJECT_STATUS.md            # ← THIS FILE
```

---

## 4. Git History

```
50565ac 增加考勤打卡功能        ← HEAD (Phase 2 complete)
ce623c3 1.0.5
8959a0e 正确接收上位机指令
38954b5 1.0.4
aad2fd8 1.0.3
80be529 添加串口解析
9657e91 1.0.2
6dee558 1.0.1
f54f29c 1.0.0
4ea4f51 实现读取card
f5bfe56 移植
381a22a 初始化
```

**Uncommitted changes:** Only `.gitignore` modified (1 line). All Phase 2 code is committed.

---

## 5. Phase Completion Status

### Phase 1: Basic Card Reading & Display — COMPLETE

| Feature | Status | Notes |
|---------|--------|-------|
| RC522 init + card polling | Done | SPI2, 200ms cycle, 3 retries |
| Card read + checksum verification | Done | Account header (16 bytes), checksum = sum of first 14 bytes |
| Card type识别 (normal/image/admin) | Done | CARD_TYPE_NORMAL=0, IMAGE=1, ADMIN=2 |
| OLED display state machine | Done | SPLASH→CLOCK→SETTING→CARD→ADMIN_INFO→ADMIN_SETTING |
| Clock UI (YYYY-MM-DD HH:MM:SS) | Done | RTC BCD read, weekday calculation |
| Time setting UI | Done | MODE cycle fields, UP/DOWN adjust, SAVE save |
| Card result display | Done | Name/Dept/Avatar bitmaps, event text, 3s timeout |
| Anti-repeat card detection | Done | 2 consecutive misses = card left |
| LED feedback (green/red/yellow) | Done | GPIO PE8~PE15 |
| Buzzer feedback (MIDI PWM) | Done | TIM3 CH1→PB4, non-blocking, cnt-based stop |
| Image card avatar reading | Done | Sectors 1~8 (avatar), 9~15 (name/dept), M1 16B blocks |

### Phase 2: Attendance Logic & Storage — COMPLETE

| Feature | Status | Notes |
|---------|--------|-------|
| `nfc_storage.h` + `nfc_storage.c` | Done | 471 lines, all APIs implemented |
| W25Q128 Flash layout | Done | Sector 0: config primary, 1: config backup, 2: record header, 3+: records |
| DeviceConfig_t (16 bytes) | Done | magic "NFCA" + dev_id + att_mode + time_offset + checksum |
| AttendanceRecord_t (32 bytes) | Done | seq+uid+id_num+card_type+DateTime(7)+event+status+dev_id+time_offset+duration |
| Config dual-backup save/load | Done | Backup written first, then primary; fallback on corruption |
| Record append (circular, 32B per record) | Done | Erase-on-demand per 4KB sector (128 records/sector), header synced each write |
| Record read by index | Done | Handles wrap-around correctly |
| Reverse scan (FindLastByUID) | Done | Walks backwards from latest record |
| LRU cache (8 entries) | Done | UID→last_event lookup, LRU eviction |
| Three attendance modes | Done | Entry / Exit / Both (default: Both) |
| Attendance判定 logic | Done | See section 6 below |
| Duration calculation | Done | Cross-day support: (day_diff × 86400) + time_diff |
| Admin card → admin mode | Done | Admin card刷卡→OLED admin UI |
| Admin settings UI | Done | dev_id (UP/DOWN), att_mode (Entry/Exit/Both), SAVE→Flash |
| Admin 120s timeout | Done | Auto-exit to clock |
| Serial command: ISSUE/IMGA/IMGN/IMGD/UPDATEIMG/READ/CLEAR | Done | RC522 mutex-protected |
| Serial command: LIST:ALL / LIST:N | Done | Record enumeration with format: `NR=...;SEQ=...,UID=...,...` |
| LED/buzzer feedback state machine | Done | FB_EVT_VALID_ENTRY/EXIT/INVALID/DUP/ADMIN |
| Compile AC5 0 error 0 warning | Done | On commit 50565ac and later |

### Phase 3: Network Communication — NOT STARTED

| Feature | Status | Notes |
|---------|--------|-------|
| ESP01S WiFi driver | Library ready | Hardware/esp01s.c 1521 lines, fully implemented |
| UartDrv serial driver | Library ready | Hardware/uart_drv.c 464 lines, idle-IRQ + callback dispatch |
| USART6 HAL config | Done | TX=PC6, RX=PC7, 115200, NVIC enabled (usart.c) |
| `Task_Network` creation | NOT DONE | Need new FreeRTOS task |
| WiFi connection flow | NOT DONE | `ESP01S_Start()` is blocking ~15s, needs own task |
| NTP time sync + RTC calibration | NOT DONE | `ESP01S_SyncNtpTime()` + `ESP01S_SetRtcFromNtp()` ready |
| Record upload over TCP | NOT DONE | Protocol: one line per record, server ACKs "OK\n" |
| Weather query (Seniverse) | NOT DONE | `ESP01S_QueryWeather()` ready |
| Weather display on clock UI | NOT DONE | Scroll text at bottom of clock page |
| Auto-reconnect | NOT DONE | 60s periodic check |

---

## 6. Key Data Structures

### AttendanceRecord_t (32 bytes, packed)
```
Offset  Field        Size  Description
0       seq          4     Record sequence number
4       uid[4]       4     Card UID
8       id_num       4     Student/employee ID
12      card_type    1     0=normal, 1=image, 2=admin
13      year         2     e.g. 2026
15      month        1     1-12
16      day          1     1-31
17      hour         1     0-23
18      minute       1     0-59
19      second       1     0-59
20      event        1     ATT_EVENT_ENTRY(0) or ATT_EVENT_EXIT(1)
21      status       1     NORMAL(0)/DUP(1)/NO_ENTRY(2)/UNKNOWN(3)
22      dev_id       2     Device ID
24      time_offset  4     Server time - local RTC (seconds)
28      duration     4     Duration in seconds (exit events)
Total: 32
```

### DeviceConfig_t (16 bytes, packed)
```
Offset  Field        Size  Description
0       magic[4]     4     "NFCA" (0x4143464E LE)
4       dev_id       2     Device ID (1-65535, default 1)
6       att_mode     1     0=Entry, 1=Exit, 2=Both (default)
7       time_offset  4     Time offset from server (seconds)
11      reserved[3]  3     Reserved
14      checksum     2     Checksum of first 14 bytes
Total: 16
```

### Flash Layout
```
Sector  Addr Range       Size   Content
0       0x000000-0x000FFF  4KB  Config primary (16B used)
1       0x001000-0x001FFF  4KB  Config backup (mirror)
2       0x002000-0x002FFF  4KB  Record header (32B: magic, write_offset, total_count, upload_offset)
3..4095 0x003000-0xFFFFFF  ~16MB Attendance records (32B each, 128/sector, circular)
```

---

## 7. FreeRTOS Tasks & IPC

| Task | Priority | Period | Stack | File | Description |
|------|----------|--------|-------|------|-------------|
| StartDefaultTask | Normal | 1s idle | 128×4 | freertos.c:166 | Init W25QXX, NFC_Storage, MIDI, then sleep |
| Task_Display | Normal | 100ms | 256×4 | app_tasks.c | OLED UI state machine, clock, card display, admin |
| Task_KeyScan | Normal | 20ms | 128×4 | app_tasks.c | GPIO key polling, short/long press detection |
| Task_CardRead | Normal | 300ms | 256×4 | app_tasks.c | RC522 poll, read, verify, attendance logic |
| Task_Serial | Normal | 10ms | 256×4 | app_tasks.c | Serial command dispatch (ISSUE/READ/CLEAR/LIST/IMG) |

| IPC Object | Type | Direction/Purpose |
|------------|------|-------------------|
| cardQueueHandle | osMessageQueue(4, CardInfo_t) | Task_CardRead → Task_Display |
| keyQueueHandle | osMessageQueue(8, KeyMsg_t) | Task_KeyScan → Task_Display |
| s_cmdSem | osSemaphore | Serial ISR → Task_Serial |
| rc522MutexHandle | osMutex | Task_CardRead ↔ Task_Serial (RC522 access) |
| storageMutexHandle | osMutex | Task_CardRead ↔ Task_Serial (W25Q128 access) |

---

## 8. Attendance判定 Logic (Implemented)

```
刷卡成功 → 验证校验和 → 判断卡类型
  ├── 管理员卡 → FB_EVT_ADMIN → 发送队列 (跳过考勤)
  └── 普通/图像卡 → 查LRU缓存
        ├── LRU命中 → 获取 last_event
        └── LRU未命中 → NFC_Storage_FindLastByUID() 倒扫Flash
              ├── 找到 → last_event = rec.event
              └── 未找到 → 视为新卡

根据 att_mode 判定:
  ATT_MODE_ENTRY (入口模式):
    last==ENTRY → STATUS_DUP (重复)
    else       → EVENT_ENTRY (入场)
    
  ATT_MODE_EXIT (出口模式):
    last==ENTRY → EVENT_EXIT + duration (离场, 计算时长)
    else       → STATUS_NO_ENTRY (无效: 无入场记录)
    
  ATT_MODE_BOTH (出入模式, 默认):
    last==ENTRY → EVENT_EXIT + duration (离场)
    else       → EVENT_ENTRY (入场)

写入Flash → 更新LRU → 设置Feedback → 发送队列
```

---

## 9. Time Setting Bug Fix (Not Yet Compiled)

**Problem:** Entering time setting forces seconds to 0; adjusting seconds has no effect.

**Root Cause:** Three `dt.second = 0;` lines in `app_tasks.c`:
1. Line 1467-1468: When entering DISP_MODE_SETTING
2. Line 1475-1478: When MODE key cycles past FIELD_COUNT
3. Line 1510-1512: When pressing SAVE key

**Fix Applied (in working tree):** All three `dt.second = 0;` lines removed from `app_tasks.c`.

**Need to:** Compile with AC5 to verify 0 errors 0 warnings.

**Compile command:**
```
D:\keil5\ARM\ARMCC\bin\armcc.exe --cpu Cortex-M4.fp -c Core/Src/app_tasks.c -I Core/Inc -I Drivers/STM32F4xx_HAL_Driver/Inc -I Drivers/CMSIS/Device/ST/STM32F4xx/Include -I Drivers/CMSIS/Include -I Middlewares/Third_Party/FreeRTOS/Source/include -I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I Hardware/Inc -D STM32F407xx -D USE_HAL_DRIVER
```
(Use full build via Keil IDE for proper verification.)

---

## 10. Phase 3 Implementation Plan (Not Started)

### 10.1 Files to Create
None — all functionality added to existing files.

### 10.2 Files to Modify

#### A. `Core/Src/app_tasks.c` — Add Task_Network

```c
// New task (alongside existing Task_Serial, Task_CardRead, etc.)
// Location: after Task_Serial (line ~1235)

// Weather cache structure
typedef struct {
    char city[16];
    char textDay[32];
    char high[8];
    char textNight[32];
    char low[8];
    char precip[8];
    uint8_t valid;         // 1=valid data
    uint32_t queryTick;    // last query HAL_GetTick()
} WeatherInfo_t;

static WeatherInfo_t g_weather;

// UART driver instance for ESP01S
static UartDrv_t g_espDrv;

void Task_Network(void *argument) {
    // ---- One-time init ----
    UartDrv_Init(&g_espDrv, &huart6);
    ESP01S_Init(&g_espDrv);
    // ESP01S_Start() is blocking ~15s, call once
    int ret = ESP01S_Start();
    if (ret == 0) {
        // NTP→RTC calibration
        ESP01S_SetRtcFromNtp(&hrtc);
    }

    uint32_t reconnectTimer = 0;
    uint32_t weatherTimer  = 0;

    for (;;) {
        ESP01S_State_t st = ESP01S_GetState();

        if (st == ESP01S_STATE_TRANSPARENT) {
            // ---- Upload pending records ----
            upload_pending_records();
            
            // ---- Weather query (every 30 min) ----
            if (HAL_GetTick() - weatherTimer > 1800000) {
                query_weather_once();
                weatherTimer = HAL_GetTick();
            }
        } else if (st == ESP01S_STATE_IDLE) {
            // Disconnected — retry after delay
            reconnectTimer++;
            if (reconnectTimer >= 60) {
                ESP01S_Start();
                if (ESP01S_GetState() >= ESP01S_STATE_WIFI_CONNECTED) {
                    ESP01S_SetRtcFromNtp(&hrtc);
                }
                reconnectTimer = 0;
            }
        }
        // else: connecting, wait for state machine

        osDelay(1000);
    }
}
```

#### B. `Core/Inc/app_tasks.h` — Add prototype + extern

```c
// Add after Task_Serial prototype:
void Task_Network(void *argument);

// Add weather info struct
typedef struct {
    char city[16];
    char textDay[32];
    char high[8];
    char textNight[32];
    char low[8];
    char precip[8];
    uint8_t valid;
    uint32_t queryTick;
} WeatherInfo_t;
extern WeatherInfo_t g_weather;
```

#### C. `Core/Src/freertos.c` — Add Task_Network thread

```c
// In RTOS_THREADS section, add:
{
    static const osThreadAttr_t networkTask_attr = {
        .name = "Task_Network",
        .stack_size = 512 * 4,       /* ESP01S_Start uses printf, needs more stack */
        .priority = osPriorityLow,   /* Low priority, 1s cycle */
    };
    osThreadNew(Task_Network, NULL, &networkTask_attr);
}

// Include:
#include "uart_drv.h"
#include "esp01s.h"
```

### 10.3 Record Upload Protocol Design

透传模式下，逐条上传未同步记录：
```
Send: ATT:SEQ=123,UID=AABBCCDD,SID=2021001,EVT=E,STS=N,DT=2026-07-10 14:30:00,DUR=0,DEV=1,OFS=0\n
Recv: OK\n

Send: ATT:SEQ=124,UID=AABBCCDD,SID=2021001,EVT=X,STS=N,DT=2026-07-10 18:00:00,DUR=12600,DEV=1,OFS=0\n
Recv: OK\n
```

上传逻辑：
1. 读取 `upload_offset` (记录区头部中)
2. 如果 `upload_offset == write_offset` → 所有记录已同步
3. 否则：读取 upload_offset 处的一条记录 → 格式化→ 发送 → 等ACK(超时5s)
4. 收到OK → 推进 upload_offset → 写回Flash头部
5. 如果上传失败 → 保持 upload_offset → 下次重试

### 10.4 Weather Query Integration

`ESP01S_QueryWeather()` 已完整实现：
- 心知天气 API: `/v3/weather/daily.json`
- 自动处理透传退出→HTTP请求→透传恢复
- DNS失败时自动回退到AT+CIPDOMAIN解析
- 结果存入 `WeatherInfo_t`

Task_Display 时钟界面底部滚动显示:
```
"杭州 晴 25℃~32℃ 降雨0%"
```
文字过长时自动水平滚动。

### 10.5 Design Constraints

1. **ESP01S_Start() 是阻塞式的** (~15s): 必须在独立低优先级任务中，不能阻塞UI
2. **天气查询会临时退出透传**: 期间暂停上传，查询完自动恢复TCP+透传
3. **单TCP连接**: ESP8266 CIPMUX=0，只能同时连一个服务器，上传和天气无法并行
4. **upload_offset 每次上传成功后写Flash**: 保证断点续传
5. **Task_Network 栈要大**: ESP01S_Start 内有 printf + snprintf + JSON 解析，给 512×4 bytes
6. **ESP01S 供电**: 启动电流 ~300mA，注意USB供电不足

---

## 11. Key Function Call Graph

```
main()
  ├── HAL_Init()
  ├── SystemClock_Config()
  ├── MX_GPIO_Init() / MX_I2C1_Init() / MX_RTC_Init() / MX_SPI1_Init()
  ├── MX_USART6_UART_Init() / MX_TIM1_Init() / MX_USART1_UART_Init()
  ├── MX_TIM3_Init()              ← PWM for buzzer
  ├── Serial_Cmd_Init()           ← UartDrv + ISR callback for USART1
  ├── osKernelInitialize()
  ├── MX_FREERTOS_Init()
  │     ├── osMutexNew(rc522Mutex, storageMutex)
  │     ├── osSemaphoreNew(s_cmdSem)
  │     ├── osMessageQueueNew(cardQueue, keyQueue)
  │     ├── osThreadNew(Task_Display, Task_KeyScan, Task_CardRead, Task_Serial)
  │     └── osThreadNew(StartDefaultTask)  ← last
  └── osKernelStart()

StartDefaultTask()
  ├── W25QXX_Init()
  ├── NFC_Storage_Init()          ← Load config, validate header
  ├── MIDI_Init(&htim3, CH1)      ← PWM buzzer
  └── MIDI_SetVolume(80U)

Task_CardRead() [300ms cycle]
  ├── rc522_request() → rc522_anticoll() → rc522_select()
  ├── rc522_auth(PICC_AUTHENT1A, 0) → rc522_read(block1)
  ├── checksum verification
  ├── Attendance判定 (see section 8)
  ├── NFC_Storage_AddRecord()     ← mutex-protected
  ├── LRU_Update()
  └── osMessageQueuePut(cardQueue)

Task_Display() [100ms cycle]
  ├── osMessageQueueGet(keyQueue, 0) → handle keys
  ├── osMessageQueueGet(cardQueue, 0) → handle card
  ├── State machine: SPLASH / CLOCK / SETTING / CARD / ADMIN_INFO / ADMIN_SETTING
  ├── LED/buzzer feedback state machine (non-blocking timer-driven)
  └── u8g2 OLED rendering

Task_Serial() [信号量驱动]
  ├── osSemaphoreAcquire(s_cmdSem)
  ├── ISSUE: / IMGA: / IMGN: / IMGD: / UPDATEIMG / READ / CLEAR: / LIST:
  └── send_response() via UartDrv_SendStr

Task_KeyScan() [20ms cycle]
  └── Poll GPIO PE1~PE6 → debounce → osMessageQueuePut(keyQueue)
```

---

## 12. ESP01S Driver API Quick Reference

```c
// Init (blocking ~15s, WiFi+NTP+TCP+Transparent)
ESP01S_Init(&g_espDrv);
int ret = ESP01S_Start();          // 0=success, -1=AT fail, -2=WiFi fail, -3=TCP fail

// State query
ESP01S_State_t st = ESP01S_GetState();  // IDLE→AT_OK→WIFI_CONNECTED→TCP_CONNECTED→TRANSPARENT
uint8_t ok = ESP01S_IsWiFiConnected();
uint8_t ok = ESP01S_IsNtpSynced();

// Data send (transparent mode only)
ESP01S_SendStr("hello\n");
ESP01S_SendData(binaryData, len);

// NTP → RTC
ESP01S_SetRtcFromNtp(&hrtc);      // 0=success

// Time get (RTC first, NTP fallback)
ESP01S_GetDateTime(&hrtc, DT_ALL, buf, 24);

// Receive: register callback or queue
ESP01S_RegisterDataCb(myCallback, ctx);
ESP01S_RegisterRxQueue(myQueue);

// Weather (exits transparent, queries HTTP, restores transparent)
ESP01S_QueryWeather(apiKey, "hangzhou", "zh-Hans", "c",
                    city, 16, textDay, 32, high, 8,
                    textNight, 32, low, 8, precip, 8);

// Exit transparent (to send AT commands)
ESP01S_ExitTransparent();

// Config
ESP01S_SetWiFi("ssid", "pass");
ESP01S_SetTcpServer("192.168.1.1", 8081);
ESP01S_SetNtpServer("ntp.aliyun.com", 8);
```

---

## 13. Known Issues / Gotchas

1. **Seconds always 0 bug**: Time setting code had `dt.second=0` in 3 places. Lines removed but NOT yet compiled. Need AC5 compile verification.

2. **USART1 NVIC is in USART6 MspInit**: In `usart.c:138`, `USART1_IRQn` priority/enable is inside the `USART6` branch — this is a CubeMX bug. Currently USART1 interrupt still works because it's also enabled elsewhere, but should be moved to the USART1 branch.

3. **ESP01S_Start blocking**: The driver's `ESP01S_Start()` uses `osDelay()` (blocking). Must be called from a dedicated task, never from main or init task.

4. **ESP01S Single TCP**: CIPMUX=0, only one connection. Weather query temporarily drops TCP connection, so record upload pauses during weather queries.

5. **Weather API Key**: Seniverse API key is hardcoded in `Task_Network`. Need to either make it configurable or use a fixed key.

6. **Record upload format**: The server must reply `OK\n` for each uploaded record. If server replies anything else or times out, the record is not marked as uploaded and will be retried.

7. **Stack sizing**: `Task_Network` stack needs to be large (512×4 or more) because ESP01S driver uses `snprintf` for JSON parsing and printf for debug output.

---

## 14. Compile & Verify

### Quick single-file check:
```bash
D:\keil5\ARM\ARMCC\bin\armcc.exe --cpu Cortex-M4.fp -c Core/Src/nfc_storage.c -I Core/Inc -I Drivers/STM32F4xx_HAL_Driver/Inc -I Drivers/CMSIS/Device/ST/STM32F4xx/Include -I Drivers/CMSIS/Include -I Middlewares/Third_Party/FreeRTOS/Source/include -I Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I Hardware/Inc -D STM32F407xx -D USE_HAL_DRIVER
```

### Full build:
Open `MDK-ARM\Slave.uvprojx` in Keil uVision → Build (F7). Target: 0 Error(s), 0 Warning(s).

Last known build output (commit 50565ac):
```
Program Size: Code=43120 RO-data=206716 RW-data=216 ZI-data=24248
```

---

## 15. How to Continue

### If resuming Phase 3 implementation:

1. Read this document and `nfc_guidance_p21-40.md` (pages 21-40, section 4.1-4.3)
2. Read the existing `Hardware/inc/esp01s.h` for the complete API
3. Implement in this order:
   a. Add `Task_Network` to `app_tasks.c` (WiFi connect + NTP sync first)
   b. Add `Task_Network` thread creation in `freertos.c`
   c. Compile & verify 0 errors
   d. Add record upload logic
   e. Add weather query + display
   f. Compile & verify 0 errors
   g. Hardware test: verify NTP time sync, upload records, check weather display

### If debugging existing features:

- **Time setting seconds bug**: The `dt.second = 0` lines are already removed. Rebuild and test.
- **Buzzer issues**: Check `stm32f4xx_it.c` has TIM3_IRQHandler, `tim.c` HAL_TIM_PWM_MspInit has NVIC enable.
- **Storage issues**: Check `nfc_storage.c`, verify W25Q128 init success.

### Important file locations for quick reference:

| What | File | Approx Line |
|------|------|-------------|
| Attendance判定 | app_tasks.c | 980-1090 |
| Display state machine | app_tasks.c | 1400-2050 |
| Time setting | app_tasks.c | 1455-1600 |
| Admin mode | app_tasks.c | 1492-1600 |
| Serial LIST command | app_tasks.c | 1178-1228 |
| Feedback state machine | app_tasks.c | 1870-1980 |
| Record write | nfc_storage.c | 293-330 |
| Config save/load | nfc_storage.c | 70-170 |
| Task creation | freertos.c | 72-157 |
| ESP01S Start flow | esp01s.c | 176-274 |
| ESP01S NTP sync | esp01s.c | 464-543 |
| Weather query | esp01s.c | 733-906 |
