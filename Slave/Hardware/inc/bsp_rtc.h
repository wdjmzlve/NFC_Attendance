/**
 * @file    bsp_rtc.h
 * @brief   RTC 日历驱动 - 封装 STM32 HAL RTC, 提供易用的日期时间读写接口
 * @note    依赖 CubeMX 生成的 MX_RTC_Init() 已完成 RTC 硬件初始化 (hrtc)
 *          本驱动通过备份寄存器判断是否首次上电, 避免每次复位重置时间
 *          断电保持需要 VBAT 引脚接电池或并接 VDD
 */
#ifndef __BSP_RTC_H__
#define __BSP_RTC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* -----------------------------------------------------------
 *  日期时间结构体
 * --------------------------------------------------------- */
typedef struct {
    uint16_t year;    /* 年, 完整 4 位, 如 2026            */
    uint8_t  month;   /* 月, 1-12                          */
    uint8_t  day;     /* 日, 1-31                          */
    uint8_t  weekday; /* 星期, 0=周日 1=周一 ... 6=周六     */
    uint8_t  hour;    /* 时, 0-23                          */
    uint8_t  minute;  /* 分, 0-59                          */
    uint8_t  second;  /* 秒, 0-59                          */
} BSP_RTC_DateTime_t;

/* 备份寄存器配置 —— 用于判断是否首次上电 */
#define BSP_RTC_BKUP_DR     RTC_BKP_DR0
#define BSP_RTC_BKUP_MAGIC  0x5050U

/* -----------------------------------------------------------
 *  全局日期时间变量
 *  每次 BSP_RTC_GetDateTime() 读取后自动更新, 方便其他任务直接访问
 * --------------------------------------------------------- */
extern BSP_RTC_DateTime_t g_rtc_datetime;

/* -----------------------------------------------------------
 *  公共接口
 * --------------------------------------------------------- */

/**
 * @brief  判断是否首次上电 (备份寄存器中无魔数)
 * @retval 1: 首次上电   0: 非首次 (RTC 已被初始化过)
 */
uint8_t BSP_RTC_IsFirstPowerOn(void);

/**
 * @brief  标记 RTC 已初始化 (向备份寄存器写入魔数)
 */
void BSP_RTC_MarkInitialized(void);

/**
 * @brief  读取 RTC 日期时间
 * @param  dt: 输出, 指向日期时间结构体; 传 NULL 时仅更新全局变量
 * @retval HAL_OK 成功, 其他失败
 * @note   读取结果会同时存入全局变量 g_rtc_datetime, 方便其他任务访问
 */
HAL_StatusTypeDef BSP_RTC_GetDateTime(BSP_RTC_DateTime_t *dt);

/**
 * @brief  设置 RTC 日期
 * @param  year:  年, 如 2026
 * @param  month: 月 1-12
 * @param  day:   日 1-31
 * @retval HAL_OK 成功, 其他失败
 */
HAL_StatusTypeDef BSP_RTC_SetDate(uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief  设置 RTC 时间
 * @param  hour:   时 0-23
 * @param  minute: 分 0-59
 * @param  second: 秒 0-59
 * @retval HAL_OK 成功, 其他失败
 */
HAL_StatusTypeDef BSP_RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second);

/**
 * @brief  同时设置 RTC 日期和时间
 * @param  dt: 指向日期时间结构体
 * @retval HAL_OK 成功, 其他失败
 */
HAL_StatusTypeDef BSP_RTC_SetDateTime(const BSP_RTC_DateTime_t *dt);

/**
 * @brief  根据日期计算星期
 * @param  year:  年
 * @param  month: 月
 * @param  day:   日
 * @retval 0=周日, 1=周一, ... 6=周六
 */
uint8_t BSP_RTC_CalcWeekday(uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief  判断闰年
 * @param  year: 年
 * @retval 1: 闰年  0: 平年
 */
uint8_t BSP_RTC_IsLeapYear(uint16_t year);

/**
 * @brief  获取某月最大天数
 * @param  year:  年
 * @param  month: 月
 * @retval 该月天数 (28-31)
 */
uint8_t BSP_RTC_DaysInMonth(uint16_t year, uint8_t month);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_RTC_H__ */
