/**
 * @file    bsp_rtc.c
 * @brief   RTC 日历驱动实现
 */
#include "bsp_rtc.h"

/* CubeMX 生成的 RTC 句柄, 由 MX_RTC_Init() 初始化 */
extern RTC_HandleTypeDef hrtc;

/* 全局日期时间变量, 每次 BSP_RTC_GetDateTime() 读取后自动更新 */
BSP_RTC_DateTime_t g_rtc_datetime = {0};

/* -----------------------------------------------------------
 *  备份寄存器 —— 上电判断
 * --------------------------------------------------------- */
uint8_t BSP_RTC_IsFirstPowerOn(void)
{
    return (HAL_RTCEx_BKUPRead(&hrtc, BSP_RTC_BKUP_DR) != BSP_RTC_BKUP_MAGIC);
}

void BSP_RTC_MarkInitialized(void)
{
    HAL_RTCEx_BKUPWrite(&hrtc, BSP_RTC_BKUP_DR, BSP_RTC_BKUP_MAGIC);
}

/* -----------------------------------------------------------
 *  日期时间读写
 * --------------------------------------------------------- */
HAL_StatusTypeDef BSP_RTC_GetDateTime(BSP_RTC_DateTime_t *dt)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    /*
     * STM32 RTC 影子寄存器要求: 必须先读 Time 再读 Date,
     * 读取 Date 才会解锁/锁定影子寄存器, 顺序不能颠倒。
     */
    if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
        return HAL_ERROR;
    if (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)
        return HAL_ERROR;

    g_rtc_datetime.year    = 2000U + sDate.Year;
    g_rtc_datetime.month   = sDate.Month;
    g_rtc_datetime.day     = sDate.Date;
    g_rtc_datetime.hour    = sTime.Hours;
    g_rtc_datetime.minute  = sTime.Minutes;
    g_rtc_datetime.second  = sTime.Seconds;
    g_rtc_datetime.weekday = BSP_RTC_CalcWeekday(g_rtc_datetime.year,
                                                 g_rtc_datetime.month,
                                                 g_rtc_datetime.day);

    if (dt != NULL)
        *dt = g_rtc_datetime;

    return HAL_OK;
}

HAL_StatusTypeDef BSP_RTC_SetDate(uint16_t year, uint8_t month, uint8_t day)
{
    RTC_DateTypeDef sDate = {0};
    sDate.Year  = year % 2000U;
    sDate.Month = month;
    sDate.Date  = day;
    return HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
}

HAL_StatusTypeDef BSP_RTC_SetTime(uint8_t hour, uint8_t minute, uint8_t second)
{
    RTC_TimeTypeDef sTime = {0};
    sTime.Hours          = hour;
    sTime.Minutes        = minute;
    sTime.Seconds        = second;
    sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    sTime.StoreOperation = RTC_STOREOPERATION_RESET;
    return HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
}

HAL_StatusTypeDef BSP_RTC_SetDateTime(const BSP_RTC_DateTime_t *dt)
{
    HAL_StatusTypeDef ret;
    ret = BSP_RTC_SetDate(dt->year, dt->month, dt->day);
    if (ret != HAL_OK)
        return ret;
    return BSP_RTC_SetTime(dt->hour, dt->minute, dt->second);
}

/* -----------------------------------------------------------
 *  日历辅助函数
 * --------------------------------------------------------- */
uint8_t BSP_RTC_IsLeapYear(uint16_t year)
{
    return ((year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U));
}

uint8_t BSP_RTC_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[] = {0, 31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (month < 1U || month > 12U)
        return 0U;
    if (month == 2U && BSP_RTC_IsLeapYear(year))
        return 29U;
    return days[month];
}

uint8_t BSP_RTC_CalcWeekday(uint16_t year, uint8_t month, uint8_t day)
{
    /* Tomohiko Sakamoto 算法 —— 返回 0=周日, 1=周一, ... 6=周六 */
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (month < 3U)
        year--;
    return (uint8_t)((year + year / 4U - year / 100U + year / 400U
                      + t[month - 1U] + day) % 7U);
}
