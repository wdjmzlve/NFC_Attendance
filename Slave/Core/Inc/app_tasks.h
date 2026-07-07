/**
 * @file    app_tasks.h
 * @brief   Application task definitions for NFC Attendance System
 * @note    Provides task prototypes, queue handles, and shared data structures
 *          for the clock display, time setting, card reading, and card display features.
 */
#ifndef __APP_TASKS_H
#define __APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "cmsis_os.h"
#include "bsp_rtc.h"

/* -------------------------------------------------------------------------- */
/*  Card Info Structure                                                       */
/* -------------------------------------------------------------------------- */

/** Card information passed from CardRead task to Display task via queue */
typedef struct {
    uint8_t  uid[4];              /**< 4-byte card UID from anticollision */
    BSP_RTC_DateTime_t timestamp; /**< Timestamp when card was detected   */
} CardInfo_t;

/* -------------------------------------------------------------------------- */
/*  Queue Handles (defined in app_tasks.c)                                    */
/* -------------------------------------------------------------------------- */

/** Message queue for passing CardInfo_t from CardRead to Display task */
extern osMessageQueueId_t cardQueueHandle;

/* -------------------------------------------------------------------------- */
/*  Task Function Prototypes                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Main display task: clock / time-setting / card-info UI
 * @param  argument: unused
 * @note   Handles OLED rendering, key scanning, RTC read/write, and receives
 *         card info from cardQueueHandle for temporary display.
 */
void Task_Display(void *argument);

/**
 * @brief  Card reading task: periodically polls RC522 for NFC cards
 * @param  argument: unused
 * @note   Initializes RC522 platform, scans for cards every 300ms,
 *          sends CardInfo_t to display task via cardQueueHandle on detection.
 */
void Task_CardRead(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TASKS_H */
