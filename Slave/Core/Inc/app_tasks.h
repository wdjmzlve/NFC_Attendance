/**
 * @file    app_tasks.h
 * @brief   Application task definitions for NFC Attendance System
 * @note    Provides task prototypes, queue handles, and shared data structures
 *          for the clock display, time setting, card reading, and card display features.
 * @note    UTF-8 encoding
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
/*  Card Type Constants                                                        */
/* -------------------------------------------------------------------------- */
#define CARD_TYPE_NORMAL    0x00U   /**< Normal NFC card                    */
#define CARD_TYPE_IMAGE     0x01U   /**< Image/photo NFC card               */
#define CARD_TYPE_ADMIN     0x02U   /**< Admin NFC card                     */

/* -------------------------------------------------------------------------- */
/*  Card Info Structure                                                       */
/* -------------------------------------------------------------------------- */

/** Card information passed from CardRead task to Display task via queue */
typedef struct {
    uint8_t  uid[4];              /**< 4-byte card UID from anticollision   */
    uint32_t id_num;              /**< Student ID number (from block1 bytes 4-7) */
    uint8_t  card_type;           /**< Card type (CARD_TYPE_NORMAL/_IMAGE)  */
    BSP_RTC_DateTime_t timestamp; /**< Timestamp when card was detected     */
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
 * @brief  Main display task: splash / clock / time-setting / card-info UI
 * @param  argument: unused
 * @note   Handles OLED rendering, key scanning, RTC read/write, temperature
 *         reading, LED indicators, and receives card info from cardQueueHandle.
 */
void Task_Display(void *argument);

/**
 * @brief  Card reading task: periodically polls RC522 for NFC cards
 * @param  argument: unused
 * @note   Initializes RC522 platform, scans for cards every 300ms,
 *          authenticates and reads card data, sends CardInfo_t to display
 *          task via cardQueueHandle on detection.
 */
void Task_CardRead(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __APP_TASKS_H */
