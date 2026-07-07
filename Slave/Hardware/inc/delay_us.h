/**
 * @file    delay_us.h
 * @brief   公共微秒延时服务（基于 Cortex-M DWT CYCCNT）
 * @details 所有驱动统一通过此模块获取微秒延时能力，
 *          避免各驱动各自初始化 DWT 带来的冗余和潜在冲突。
 *
 *          使用方法：
 *          - delay_us_init()  在系统启动时调用一次
 *          - delay_us()       即可在任何地方使用
 *
 * @note    编码: UTF-8
 */

#ifndef DELAY_US_H
#define DELAY_US_H

#include <stdint.h>

/**
 * @brief 初始化 DWT 微秒延时
 * @note 多次调用是安全的（第二次起不会重置计数器）
 */
void delay_us_init(void);

/**
 * @brief 微秒级阻塞延时
 * @param us 延时微秒数
 * @note 可重入，多任务/中断中均可调用
 */
void delay_us(uint32_t us);

#endif /* DELAY_US_H */
