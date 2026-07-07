/**
  ******************************************************************************
  * @file    midi.h
  * @brief   蜂鸣器播放音乐驱动头文件
  ******************************************************************************
  */

#ifndef __MIDI_H__
#define __MIDI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifndef MIDI_DEFAULT_BPM
#define MIDI_DEFAULT_BPM  90
#endif

/**
  * @brief  计算指定BPM下的四分音符时长（毫秒）
  * @param  bpm: 每分钟节拍数
  * @retval 四分音符时长（毫秒）
  */
#define MIDI_PT(bpm)    ((int)(60 * 100000 / (bpm)) / 400) * 400

/**
  * @brief  低音八度音符时长宏定义
  * X为音符编号：0=休止符,1=C,2=D,3=E,4=F,5=G,6=A,7=B
  * 1_4表示十六分音符，1_2表示八分音符，3_4表示附点八分音符，1表示四分音符，以此类推
  */
#define L1_4(X)   (MIDI_PT(MIDI_DEFAULT_BPM) / 4 + X)
#define M1_4(X)   (L1_4(X) + 8)
#define H1_4(X)   (M1_4(X) + 8)

#define L1_2(X)   (MIDI_PT(MIDI_DEFAULT_BPM) / 2 + X)
#define M1_2(X)   (L1_2(X) + 8)
#define H1_2(X)   (M1_2(X) + 8)

#define L3_4(X)   (MIDI_PT(MIDI_DEFAULT_BPM) * 3 / 4 + X)
#define M3_4(X)   (L3_4(X) + 8)
#define H3_4(X)   (M3_4(X) + 8)

#define L1(X)   (MIDI_PT(MIDI_DEFAULT_BPM) + X)
#define M1(X)   (L1(X) + 8)
#define H1(X)   (M1(X) + 8)

#define L3_2(X)   (MIDI_PT(MIDI_DEFAULT_BPM) * 3 / 2 + X)
#define M3_2(X)   (L3_2(X) + 8)
#define H3_2(X)   (M3_2(X) + 8)

#define L2(X)   (MIDI_PT(MIDI_DEFAULT_BPM) * 2 + X)
#define M2(X)   (L2(X) + 8)
#define H2(X)   (M2(X) + 8)

#define L3(X)   (MIDI_PT(MIDI_DEFAULT_BPM) * 3 + X)
#define M3(X)   (L3(X) + 8)
#define H3(X)   (M3(X) + 8)

#define L4(X)   (MIDI_PT(MIDI_DEFAULT_BPM) * 4 + X)
#define M4(X)   (L4(X) + 8)
#define H4(X)   (M4(X) + 8)

/**
  * @brief  音乐播放状态枚举
  */
typedef enum {
    MIDI_STATE_STOPPED = 0,
    MIDI_STATE_PLAYING = 1,
    MIDI_STATE_PAUSED  = 2,
} MIDI_StateTypeDef;

/**
  * @brief  初始化MIDI驱动
  * @param  htim: PWM定时器句柄指针
  * @param  channel: PWM通道
  * @retval None
  */
void MIDI_Init(TIM_HandleTypeDef *htim, uint32_t channel);

/**
  * @brief  设置要播放的音乐数据
  * @param  data: 音乐数据数组指针
  * @param  len: 音乐数据长度
  * @param  base_bpm: 音乐的基准BPM
  * @retval None
  */
void MIDI_SetMusic(uint32_t *data, uint16_t len, uint16_t base_bpm);

/**
  * @brief  开始/继续播放音乐
  * @retval None
  */
void MIDI_Play(void);

/**
  * @brief  暂停播放音乐
  * @retval None
  */
void MIDI_Pause(void);

/**
  * @brief  重新开始播放音乐
  * @retval None
  */
void MIDI_Restart(void);

/**
  * @brief  停止播放音乐
  * @retval None
  */
void MIDI_Stop(void);

/**
  * @brief  设置播放时长（单位：秒）
  * @param  seconds: 播放时长，0表示无限播放
  * @retval None
  */
void MIDI_SetDuration(uint32_t seconds);

/**
  * @brief  设置是否循环播放
  * @param  enable: 1=循环播放，0=不循环
  * @retval None
  */
void MIDI_SetLoop(uint8_t enable);

/**
  * @brief  设置音量
  * @param  vol: 音量值，范围0-100
  * @retval None
  */
void MIDI_SetVolume(uint8_t vol);

/**
  * @brief  获取当前音量
  * @retval 当前音量值
  */
uint8_t MIDI_GetVolume(void);

/**
  * @brief  设置播放速度（BPM）
  * @param  bpm: 每分钟节拍数
  * @retval None
  */
void MIDI_SetBPM(uint16_t bpm);

/**
  * @brief  获取当前播放速度
  * @retval 当前BPM值
  */
uint16_t MIDI_GetBPM(void);

/**
  * @brief  设置音调偏移（移调）
  * @param  shift: 移调半音数，正数升调，负数降调
  * @retval None
  */
void MIDI_SetShift(int8_t shift);

/**
  * @brief  获取当前音调偏移
  * @retval 当前移调值
  */
int8_t MIDI_GetShift(void);

/**
  * @brief  获取当前播放状态
  * @retval 当前状态
  */
MIDI_StateTypeDef MIDI_GetState(void);

/**
  * @brief  播放单音
  * @param  tune: 音符编号（0-24，0=休止符）
  * @param  time: 音符时长（毫秒）
  * @retval None
  */
void MIDI_Beep(uint8_t tune, uint16_t time);

/**
  * @brief  音乐播放节拍处理函数，需在主循环中周期调用
  * @retval None
  */
void MIDI_Tick(void);

/**
  * @brief  定时器中断回调函数，需在定时器更新中断中调用
  * @param  htim: 定时器句柄
  * @retval None
  */
void MIDI_TimerCallback(TIM_HandleTypeDef *htim);

/**
  * @brief  获取数组大小的宏
  */
#define ARRAY_SIZE(arr)  (sizeof(arr) / sizeof((arr)[0]))

#ifdef __cplusplus
}
#endif

#endif
