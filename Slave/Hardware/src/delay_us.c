/**
 * @file    delay_us.c
 * @brief   公共微秒延时服务实现
 * @details 基于 Cortex-M DWT CYCCNT 实现，
 *          使用静态标志确保 DWT 只初始化一次。
 *
 * @note    编码: UTF-8
 */

#include "delay_us.h"
#include "main.h"          /* 提供 SystemCoreClock */

static volatile uint8_t s_dwt_inited = 0;

void delay_us_init(void)
{
    if (s_dwt_inited) {
        return;             /* 已初始化过，跳过 */
    }
    s_dwt_inited = 1;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;
}

void delay_us(uint32_t us)
{
    uint32_t cycles = (SystemCoreClock / 1000000L) * us;
    uint32_t start  = DWT->CYCCNT;

    while ((DWT->CYCCNT - start) < cycles);
}
