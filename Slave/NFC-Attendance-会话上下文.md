# NFC Attendance 会话上下文

## 项目信息
- **项目路径**: `E:\KeilProject\NFC_Attendance\Slave`
- **芯片**: STM32F407VGT6, 168MHz
- **RTOS**: FreeRTOS (CMSIS-RTOS v2)
- **编译链**: Keil MDK AC5 (armcc)
- **分支**: dev

## 已完成的修改

### 1. KEY4 保存按键 (已完成)
在时间设置界面添加 KEY4(PE4) 作为独立保存按键，无需循环遍历所有字段即可保存退出。
- 文件: `Core/Src/app_tasks.c`
- 涉及6处修改:
  - KEY4 引脚宏定义 (line 56-57)
  - `key_save_prev` 状态变量 (line 983)
  - 主循环按键读取 (line 1001)
  - 保存处理: 上升沿检测 → CalcWeekday → SetDateTime → MarkInitialized → 退出到时钟模式 (line 1050-1057)
  - OLED 提示文字 "KEY4:save MODE:next" (line 1267)
  - 按键状态保存 (line 1321)

### 2. 串口指令任务 (已完成，待实物测试)
通过 USART1 接收上位机指令，处理发卡、图片缓存/写入、读卡、销卡等操作。
- 文件: `Core/Src/app_tasks.c` (lines 130-870) + `Core/Inc/app_tasks.h`
- 新增内容:
  - 图片数据缓存 `g_img_avatar[24][16]`, `g_img_name[10][16]`, `g_img_dept[10][16]`
  - 扇区映射表 (头像扇区1-8, 姓名扇区9-12, 部门扇区12-15)
  - 串口驱动实例 + 行缓冲 + 信号量
  - RC522 互斥量保护 (Task_CardRead 与 Task_Serial 共享)
  - 指令处理: ISSUE, IMGA/IMGN/IMGD, UPDATEIMG, READ, CLEAR
  - `Serial_Cmd_Init()` 初始化函数

### 3. 编译错误修复 (已完成)
`app_tasks.h` 中新增宏定义:
- `IMG_BLOCK_SIZE` = 16
- `IMG_AVATAR_BLOCKS` = 24
- `IMG_NAME_BLOCKS` = 10
- `IMG_DEPT_BLOCKS` = 10

### 4. 按键LED状态机 → IPC 任务间通信重构 (已完成，编译通过)
将 Task_Display 内部轮询按键的状态机逻辑重构为独立 Task_KeyScan + 消息队列模式。

**架构变化:**
- **之前**: Task_Display 每 50ms 循环内直接调用 `HAL_GPIO_ReadPin()` 轮询 4 个按键，在任务内部做消抖、长按检测、状态跳转
- **现在**: Task_KeyScan 独立任务以 20ms 周期轮询按键 → 发送 `KeyMsg_t` 到 `keyQueueHandle` → Task_Display 消费队列消息执行状态切换

**新增内容 (app_tasks.h):**
- `KeyEvtType_t` 枚举: `KEY_EVT_SHORT` (下降沿触发), `KEY_EVT_LONG` (长按自动重复)
- `KeyId_t` 枚举: `KEY_ID_MODE`, `KEY_ID_UP`, `KEY_ID_DOWN`, `KEY_ID_SAVE`
- `KeyMsg_t` 结构体: `key_id` + `evt_type`
- `keyQueueHandle` 外部声明
- `Task_KeyScan()` 函数原型

**新增内容 (app_tasks.c):**
- `Task_KeyScan()`: 轮询 GPIO → 下降沿检测短按 → 600ms 长按触发 + 150ms 重复 → `osMessageQueuePut()` 发送事件
- `keyQueueHandle` 队列句柄定义

**修改内容 (app_tasks.c Task_Display):**
- 删除所有按键轮询变量 (`key_mode_prev/up_prev/down_prev/save_prev`, `key_up_tick/down_tick`, `key_up_repeat/down_repeat`)
- 删除 `HAL_GPIO_ReadPin()` 调用
- 新增 `KeyMsg_t key_msg` + `osMessageQueueGet()` while 循环消费所有待处理事件
- 按键处理逻辑保持不变: MODE 进入/退出设置模式/循环字段; SAVE 保存退出; UP/DOWN 增减字段值

**修改内容 (freertos.c):**
- 新增 `keyQueueHandle = osMessageQueueNew(8, sizeof(KeyMsg_t), NULL)`
- 新增 Task_KeyScan 线程 (栈 512B, osPriorityNormal)

**编译状态: 0 Error, 0 Warning (Keil AC5 V5.06 update 5)**

- [ ] 串口指令实物测试

## 关键文件清单
- `Core/Src/app_tasks.c` — 主应用任务 (Display + KeyScan + CardRead + Serial)
- `Core/Inc/app_tasks.h` — 任务头文件 (队列、结构体、宏)
- `Core/Src/main.c` — 入口 (需调用 Serial_Cmd_Init)
- `Core/Src/freertos.c` — FreeRTOS 任务创建
- `Hardware/inc/bsp_rtc.h` — RTC API
- `Hardware/src/bsp_rtc.c` — RTC 实现
- `Hardware/inc/rc522.h` — RC522 API
- `Hardware/inc/uart_drv.h` — 串口驱动 API
