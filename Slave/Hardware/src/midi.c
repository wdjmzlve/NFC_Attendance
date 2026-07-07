/**
  ******************************************************************************
  * @file    midi.c
  * @brief   蜂鸣器播放音乐驱动源文件
  ******************************************************************************
  */

#include "midi.h"

/**
  * @brief  MIDI句柄结构体
  *         保存MIDI驱动的所有状态和配置信息
  */
typedef struct {
    TIM_HandleTypeDef *htim;
    uint32_t channel;

    uint8_t tune;
    uint8_t key;
    uint8_t vol;
    int8_t shift;
    uint32_t cnt;

    MIDI_StateTypeDef state;
    uint8_t loop;
    uint16_t midi_idx;
    uint32_t play_time;
    uint32_t tune_tick;
    uint32_t end_tick;

    uint16_t bpm;
    uint16_t base_bpm;

    uint32_t *midi_data;
    uint16_t midi_len;
} MIDI_HandleTypeDef;

static MIDI_HandleTypeDef hmidi;

/**
  * @brief  音符频率表（C大调，一个八度）
  *         索引0为休止符，1-7对应C-B音符
  */
static const float midi_freq_tab[8] = {0, 261.6, 293.6, 329.6, 349.2, 392.0, 440.0, 493.9};

/**
  * @brief  初始化MIDI驱动
  * @param  htim: PWM定时器句柄指针
  * @param  channel: PWM通道
  * @retval None
  */
void MIDI_Init(TIM_HandleTypeDef *htim, uint32_t channel)
{
    hmidi.htim = htim;
    hmidi.channel = channel;

    hmidi.tune = 0;
    hmidi.key = 1;
    hmidi.vol = 10;
    hmidi.shift = 0;
    hmidi.cnt = 0;

    hmidi.state = MIDI_STATE_STOPPED;
    hmidi.loop = 0;
    hmidi.midi_idx = 0;
    hmidi.play_time = 10;
    hmidi.tune_tick = 0;
    hmidi.end_tick = 0;

    hmidi.bpm = MIDI_DEFAULT_BPM;
    hmidi.base_bpm = MIDI_DEFAULT_BPM;

    hmidi.midi_data = NULL;
    hmidi.midi_len = 0;
		
		__HAL_TIM_ENABLE_IT(htim, TIM_IT_UPDATE);
}

/**
  * @brief  设置要播放的音乐数据
  * @param  data: 音乐数据数组指针
  * @param  len: 音乐数据长度
  * @param  base_bpm: 音乐的基准BPM
  * @retval None
  */
void MIDI_SetMusic(uint32_t *data, uint16_t len, uint16_t base_bpm)
{
    hmidi.midi_data = data;
    hmidi.midi_len = len;
    hmidi.base_bpm = base_bpm;
    hmidi.bpm = base_bpm;
}

/**
  * @brief  开始/继续播放音乐
  * @retval None
  */
void MIDI_Play(void)
{
    if (hmidi.state == MIDI_STATE_PAUSED)
    {
        hmidi.state = MIDI_STATE_PLAYING;
    }
    else if (hmidi.state == MIDI_STATE_STOPPED)
    {
        if (hmidi.play_time > 0 && hmidi.midi_data != NULL)
        {
            hmidi.midi_idx = 0;
            hmidi.tune_tick = 0;
            hmidi.end_tick = 0;
            hmidi.state = MIDI_STATE_PLAYING;
        }
    }
}

/**
  * @brief  暂停播放音乐
  * @retval None
  */
void MIDI_Pause(void)
{
    if (hmidi.state == MIDI_STATE_PLAYING)
    {
        HAL_TIM_PWM_Stop_IT(hmidi.htim, hmidi.channel);
        hmidi.state = MIDI_STATE_PAUSED;
    }
}

/**
  * @brief  重新开始播放音乐
  * @retval None
  */
void MIDI_Restart(void)
{
    if (hmidi.midi_data != NULL)
    {
        HAL_TIM_PWM_Stop_IT(hmidi.htim, hmidi.channel);
        hmidi.midi_idx = 0;
        hmidi.tune_tick = 0;
        hmidi.end_tick = 0;
        hmidi.state = MIDI_STATE_PLAYING;
    }
}

/**
  * @brief  停止播放音乐
  * @retval None
  */
void MIDI_Stop(void)
{
    HAL_TIM_PWM_Stop_IT(hmidi.htim, hmidi.channel);
    hmidi.tune = 0;
    hmidi.cnt = 0;
    hmidi.midi_idx = 0;
    hmidi.tune_tick = 0;
    hmidi.end_tick = 0;
    hmidi.state = MIDI_STATE_STOPPED;
}

/**
  * @brief  设置播放时长（单位：秒）
  * @param  seconds: 播放时长，0表示无限播放
  * @retval None
  */
void MIDI_SetDuration(uint32_t seconds)
{
    hmidi.play_time = seconds;
}

/**
  * @brief  设置是否循环播放
  * @param  enable: 1=循环播放，0=不循环
  * @retval None
  */
void MIDI_SetLoop(uint8_t enable)
{
    hmidi.loop = enable;
}

/**
  * @brief  设置音量
  * @param  vol: 音量值，范围0-100
  * @retval None
  */
void MIDI_SetVolume(uint8_t vol)
{
    if (vol > 100)
        vol = 100;
    hmidi.vol = vol;
}

/**
  * @brief  获取当前音量
  * @retval 当前音量值
  */
uint8_t MIDI_GetVolume(void)
{
    return hmidi.vol;
}

/**
  * @brief  设置播放速度（BPM）
  * @param  bpm: 每分钟节拍数
  * @retval None
  */
void MIDI_SetBPM(uint16_t bpm)
{
    if (bpm > 0)
        hmidi.bpm = bpm;
}

/**
  * @brief  获取当前播放速度
  * @retval 当前BPM值
  */
uint16_t MIDI_GetBPM(void)
{
    return hmidi.bpm;
}

/**
  * @brief  设置音调偏移（移调）
  * @param  shift: 移调半音数，正数升调，负数降调
  * @retval None
  */
void MIDI_SetShift(int8_t shift)
{
    hmidi.shift = shift;
}

/**
  * @brief  获取当前音调偏移
  * @retval 当前移调值
  */
int8_t MIDI_GetShift(void)
{
    return hmidi.shift;
}

/**
  * @brief  获取当前播放状态
  * @retval 当前状态
  */
MIDI_StateTypeDef MIDI_GetState(void)
{
    return hmidi.state;
}

/**
  * @brief  播放单音
  * @param  tune: 音符编号（0-24，0=休止符）
  * @param  time: 音符时长（毫秒）
  * @retval None
  */
void MIDI_Beep(uint8_t tune, uint16_t time)
{
    float key = (tune / 8) * 2;
    if (key < 1)
        key = 1;

    tune %= 8;
    hmidi.tune = tune;

    HAL_TIM_PWM_Stop_IT(hmidi.htim, hmidi.channel);

    if (tune > 0)
    {
        int8_t tt = tune;
        tt += hmidi.shift;
        if (tt >= 8)
        {
            tt -= 7;
            key *= 2;
        }
        else if (tt <= 0)
        {
            tt += 7;
            key *= 0.5;
        }

        float arr = (1000000 / (midi_freq_tab[tt] * key)) - 1;
        __HAL_TIM_SET_AUTORELOAD(hmidi.htim, (uint16_t)arr);
        __HAL_TIM_SET_COMPARE(hmidi.htim, hmidi.channel, arr * (hmidi.vol % 101) / 100);
        hmidi.key = (uint8_t)key;
        hmidi.cnt = midi_freq_tab[tt] * key * time / 1000;
        HAL_TIM_PWM_Start_IT(hmidi.htim, hmidi.channel);
    }
}

/**
  * @brief  音乐播放节拍处理函数，需在主循环中周期调用
  * @retval None
  */
void MIDI_Tick(void)
{
    if (hmidi.state != MIDI_STATE_PLAYING)
        return;
    if (hmidi.midi_data == NULL || hmidi.play_time == 0)
        return;

    uint32_t ct = HAL_GetTick();

    if (hmidi.end_tick == 0)
    {
        hmidi.end_tick = ct + hmidi.play_time * 1000;
        hmidi.midi_idx = 0;
    }

    if (ct >= hmidi.end_tick)
    {
        MIDI_Stop();
        return;
    }

    if (ct < hmidi.tune_tick)
        return;

    if (hmidi.midi_idx >= hmidi.midi_len)
    {
        if (hmidi.loop)
            hmidi.midi_idx = 0;
        else
        {
            MIDI_Stop();
            return;
        }
    }

    uint32_t note = hmidi.midi_data[hmidi.midi_idx++];
    uint16_t time = note / 100;
    time = (uint32_t)time * hmidi.base_bpm / hmidi.bpm;

    MIDI_Beep(note % 100, time * 9 / 10);
    hmidi.tune_tick = ct + time;
}

/**
  * @brief  定时器中断回调函数，需在定时器更新中断中调用
  * @param  htim: 定时器句柄
  * @retval None
  */
void MIDI_TimerCallback(TIM_HandleTypeDef *htim)
{
    if (hmidi.htim != NULL && htim->Instance == hmidi.htim->Instance)
    {
        if (hmidi.cnt > 0 && hmidi.tune > 0)
            --hmidi.cnt;
        else
            HAL_TIM_PWM_Stop_IT(htim, hmidi.channel);
    }
}
