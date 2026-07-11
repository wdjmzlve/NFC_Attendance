# MIDI 蜂鸣器音乐播放驱动使用手册

## 1. 驱动概述

MIDI 驱动利用 STM32 定时器的 PWM 功能驱动蜂鸣器，通过改变 PWM 频率产生不同音高，通过占空比调节音量。驱动支持音乐数据播放（含循环、变速、移调）和单音播放，内置 C 大调三八度音阶频率表，提供便捷的音符宏定义。

| 项目 | 说明 |
|------|------|
| 文件 | `midi.c`、`midi.h` |
| 依赖 | STM32 HAL 定时器（PWM 输出） |
| 默认 BPM | `MIDI_DEFAULT_BPM`（90） |
| 音符范围 | 0-24（0=休止符，1-7=低音，8-15=中音，16-23=高音） |

### 硬件原理

- 定时器工作在 PWM 输出模式，自动重装载值（ARR）决定 PWM 频率（即音高）
- 比较寄存器（CCR）设置占空比（即音量）
- 定时器更新中断用于控制单个音符的持续时长

---

## 2. 模块架构

```
┌─────────────────────────────────────────────────┐
│                  用户应用层                     │
│   MIDI_Play() / MIDI_Pause() / MIDI_SetVolume() │
└────────────────────┬────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────┐
│              MIDI 驱动层（单例）                │
│                                                 │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐    │
│  │ 播放状态机 │  │ 音符解析  │  │ PWM控制   │    │
│  │ STOPPED   │  │ 音符→频率 │  │ ARR/CCR   │    │
│  │ PLAYING   │  │ 时长计算  │  │ Start/Stop│    │
│  │ PAUSED    │  │           │  │           │    │
│  └───────────┘  └───────────┘  └───────────┘    │
│         │                            │          │
│    MIDI_Tick()               MIDI_TimerCallback │
│    (主循环调用)               (定时器中断调用)   │
└────────────────────┬────────────────────────────┘
                     │ HAL TIM PWM
┌────────────────────▼────────────────────────────┐
│           STM32 HAL / 硬件定时器                │
│         HAL_TIM_PWM_Start_IT / Stop_IT          │
└─────────────────────────────────────────────────┘
```

**核心工作流程**：
1. **初始化**：配置定时器 PWM 模式，初始化驱动状态
2. **播放控制**：通过状态机管理播放/暂停/停止
3. **节拍处理**：主循环调用 `MIDI_Tick()` 处理音符时序
4. **中断回调**：定时器更新中断调用 `MIDI_TimerCallback()` 控制音符时长

---

## 3. 核心数据结构

### MIDI_StateTypeDef — 播放状态

```c
typedef enum {
    MIDI_STATE_STOPPED = 0,  // 停止
    MIDI_STATE_PLAYING = 1,  // 播放中
    MIDI_STATE_PAUSED  = 2,  // 暂停
} MIDI_StateTypeDef;
```

### MIDI_HandleTypeDef — 内部句柄（驱动私有）

```c
typedef struct {
    TIM_HandleTypeDef *htim;   // PWM定时器句柄
    uint32_t channel;          // PWM通道
    uint8_t tune;              // 当前音符
    uint8_t key;               // 八度倍率
    uint8_t vol;               // 音量(0-100)
    int8_t shift;              // 移调半音数
    uint32_t cnt;              // 剩余周期计数
    MIDI_StateTypeDef state;   // 播放状态
    uint8_t loop;              // 循环标志
    uint16_t midi_idx;         // 当前播放索引
    uint32_t play_time;        // 播放时长(秒,0=无限)
    uint32_t tune_tick;        // 当前音符结束时刻
    uint32_t end_tick;         // 播放结束时刻
    uint16_t bpm;              // 当前BPM
    uint16_t base_bpm;         // 音乐基准BPM
    uint32_t *midi_data;       // 音乐数据指针
    uint16_t midi_len;         // 音乐数据长度
} MIDI_HandleTypeDef;
```

### 音乐数据编码格式

音乐数据存储在 `uint32_t` 数组中，编码格式为：

```
数据 = 时长(ms) * 100 + 音符编号
```

- 音符编号 0 = 休止符
- 音符编号 1-7 = 低音 C-B
- 音符编号 8-15 = 中音 C-B
- 音符编号 16-23 = 高音 C-B

### 音符宏定义

| 宏前缀 | 说明 | 后缀 | 时值 |
|--------|------|------|------|
| `L` | 低音八度 | `1_4` | 十六分音符 |
| `M` | 中音八度 | `1_2` | 八分音符 |
| `H` | 高音八度 | `3_4` | 附点八分音符 |
| | | `1` | 四分音符 |
| | | `3_2` | 附点四分音符 |
| | | `2` | 二分音符 |
| | | `3` | 附点二分音符 |
| | | `4` | 全音符 |

宏参数 X：0=休止符，1=C(Do)，2=D(Re)，3=E(Mi)，4=F(Fa)，5=G(Sol)，6=A(La)，7=B(Si)

**示例**：`M1(3)` = 中音四分音符 E(Mi)，`H1_2(5)` = 高音八分音符 G(Sol)

---

## 4. API 参考

### 初始化与配置

| 函数 | 说明 |
|------|------|
| `MIDI_Init(TIM_HandleTypeDef *htim, uint32_t channel)` | 初始化驱动，绑定 PWM 定时器和通道 |
| `MIDI_SetMusic(uint32_t *data, uint16_t len, uint16_t base_bpm)` | 设置音乐数据数组、长度和基准 BPM |

### 播放控制

| 函数 | 说明 |
|------|------|
| `MIDI_Play(void)` | 开始或继续播放 |
| `MIDI_Pause(void)` | 暂停播放 |
| `MIDI_Restart(void)` | 重新开始播放 |
| `MIDI_Stop(void)` | 停止播放 |

### 参数设置

| 函数 | 说明 |
|------|------|
| `MIDI_SetDuration(uint32_t seconds)` | 设置播放时长（秒，0=无限） |
| `MIDI_SetLoop(uint8_t enable)` | 设置循环播放（1=循环） |
| `MIDI_SetVolume(uint8_t vol)` | 设置音量（0-100） |
| `MIDI_SetBPM(uint16_t bpm)` | 设置播放速度 |
| `MIDI_SetShift(int8_t shift)` | 设置移调（正数升调，负数降调） |

### 参数获取

| 函数 | 返回值 |
|------|--------|
| `MIDI_GetVolume(void)` | 当前音量 |
| `MIDI_GetBPM(void)` | 当前 BPM |
| `MIDI_GetShift(void)` | 当前移调值 |
| `MIDI_GetState(void)` | 当前播放状态 |

### 核心处理函数

| 函数 | 说明 |
|------|------|
| `MIDI_Beep(uint8_t tune, uint16_t time)` | 播放单音（tune=音符编号，time=时长ms） |
| `MIDI_Tick(void)` | 节拍处理，**必须在主循环中周期调用** |
| `MIDI_TimerCallback(TIM_HandleTypeDef *htim)` | 定时器中断回调，**必须在定时器更新中断中调用** |

---

## 5. 使用示例

### 基础使用

```c
#include "midi.h"

/* 1. 定义音乐数据 */
uint32_t my_music[] = {
    M1(1), M1(2), M1(3), M1(4), M1(5), M1(6), M1(7), M2(1),
    L4(0)   // 结束休止符
};

/* 2. 初始化 */
MIDI_Init(&htim1, TIM_CHANNEL_1);
MIDI_SetMusic(my_music, ARRAY_SIZE(my_music), 90);
MIDI_SetDuration(0);   // 无限播放
MIDI_SetLoop(1);       // 循环播放

/* 3. 主循环中调用 */
while (1) {
    MIDI_Tick();
    HAL_Delay(5);
}

/* 4. 定时器中断回调中调用 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    MIDI_TimerCallback(htim);
}
```

### 按键控制示例

```c
uint32_t MIDI_Music[] = {
    M1(3), M1_2(3), M1_2(5), M1(2), M1_2(2), M1_4(2), M1_4(3),
    M1_2(1), L1_2(7), M1_2(1), M1_4(2), M1_4(3), M2(3),
    L4(0)
};

MIDI_Init(&htim1, TIM_CHANNEL_1);
MIDI_SetMusic(MIDI_Music, ARRAY_SIZE(MIDI_Music), 90);
MIDI_SetDuration(10);
MIDI_Restart();

/* 按键控制 */
switch (key) {
    case K1_Pin:
        if (MIDI_GetVolume() < 100)
            MIDI_SetVolume(MIDI_GetVolume() + 10);
        break;
    case K2_Pin:
        if (MIDI_GetState() == MIDI_STATE_PLAYING) MIDI_Pause();
        else MIDI_Play();
        break;
    case K3_Pin:
        MIDI_Restart();
        break;
    case K4_Pin:
        if (MIDI_GetVolume() > 0)
            MIDI_SetVolume(MIDI_GetVolume() - 10);
        break;
}
```

---

## 7. NFCAttend 工程实际应用

NFCAttend 考勤系统**未使用** MIDI 驱动，而是使用更简洁的 `beep.c` 驱动实现刷卡提示音。

### 与 MIDI 驱动的区别

| 特性 | MIDI 驱动 (midi.c) | Beep 驱动 (beep.c) |
|------|-------------------|-------------------|
| 功能 | 音乐播放（多音符序列） | 单音提示（非阻塞） |
| 依赖 | PWM + 定时器中断 | PWM 仅主定时器 |
| 音符表 | 三八度 C 大调音阶 | 预定义频率常量 |
| 使用场景 | 播放音乐/旋律 | 刷卡成功/失败提示音 |

### NFCAttend 中的蜂鸣器用法（beep.c）

```c
/* 刷卡成功: C5 100ms */
Beep_Start(BEEP_C5, 100);

/* 刷卡失败: A4 200ms */
Beep_Start(BEEP_A4, 200);

/* 重复刷卡: C4 50ms */
Beep_Start(BEEP_C4, 50);
```

> **设计考量**：NFCAttend 选择 Beep 驱动而非 MIDI 驱动是因为考勤系统中只需短促的单音提示，MIDI 驱动的音乐播放功能对考勤场景而言过于复杂。

---

## 6. 注意事项

1. **定时器配置**：确保定时器已配置为 PWM 输出模式并正确连接蜂鸣器。
2. **中断回调**：必须在 `HAL_TIM_PeriodElapsedCallback()` 中调用 `MIDI_TimerCallback()`。
3. **主循环调用**：`MIDI_Tick()` 必须在主循环中周期调用，建议间隔不超过 10ms。
4. **音乐数据结尾**：音符数组最后建议添加休止符（如 `L4(0)`）确保音乐完整结束。
5. **音量范围**：音量 0-100，过大音量可能导致蜂鸣器失真。
6. **BPM 范围**：建议 60-200，过快或过慢可能影响播放效果。
7. **音乐数据编码**：宏定义基于 `MIDI_DEFAULT_BPM` 计算，修改默认 BPM 后需重新编译。
