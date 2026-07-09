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
#include "nfc_storage.h"

/* -------------------------------------------------------------------------- */
/*  Card Type Constants                                                        */
/* -------------------------------------------------------------------------- */
#define CARD_TYPE_NORMAL    0x00U   /**< Normal NFC card                    */
#define CARD_TYPE_IMAGE     0x01U   /**< Image/photo NFC card               */
#define CARD_TYPE_ADMIN     0x02U   /**< Admin NFC card                     */

/* Image data block counts (M1 card: 16 bytes per block, 3 data blocks per sector) */
#define IMG_BLOCK_SIZE      16U     /**< Bytes per M1 card data block        */
#define IMG_AVATAR_BLOCKS   24U     /**< Avatar:  sectors 1~8  (8x3 blocks)  */
#define IMG_NAME_BLOCKS     10U     /**< Name:    sectors 9~12 (3+3+3+1)    */
#define IMG_DEPT_BLOCKS     10U     /**< Dept:    sectors 12~15 (2+3+3+2)   */

/* -------------------------------------------------------------------------- */
/*  Card Info Structure                                                       */
/* -------------------------------------------------------------------------- */

/** Card information passed from CardRead task to Display task via queue */
typedef struct {
    uint8_t  uid[4];              /**< 4-byte card UID from anticollision   */
    uint32_t id_num;              /**< Student ID number (from block1 bytes 4-7) */
    uint8_t  card_type;           /**< Card type (CARD_TYPE_NORMAL/_IMAGE)  */
    BSP_RTC_DateTime_t timestamp; /**< Timestamp when card was detected     */
    uint8_t  att_event;           /**< ATT_EVENT_ENTRY / ATT_EVENT_EXIT     */
    uint8_t  att_status;          /**< ATT_STATUS_NORMAL/DUP/NO_ENTRY/UNKNOWN */
    uint32_t duration_sec;        /**< Duration in seconds (exit events)    */
    uint8_t  feedback;            /**< FeedbackEvt_t for LED/buzzer control */
} CardInfo_t;

/* -------------------------------------------------------------------------- */
/*  Key Event Types (IPC between KeyScan and Display tasks)                    */
/* -------------------------------------------------------------------------- */

/** Key event type: short press (falling edge) or long press (auto-repeat) */
typedef enum {
    KEY_EVT_SHORT = 0,  /**< Short press detected on falling edge         */
    KEY_EVT_LONG  = 1,  /**< Long press auto-repeat (every KEY_REPEAT_MS) */
} KeyEvtType_t;

/** Physical key identifiers */
typedef enum {
    KEY_ID_MODE = 0,    /**< MODE key: cycle field / enter-exit modes     */
    KEY_ID_UP,          /**< UP key: increment selected field             */
    KEY_ID_DOWN,        /**< DOWN key: decrement selected field           */
    KEY_ID_SAVE,        /**< SAVE key: save & exit setting mode           */
} KeyId_t;

/** Key event message sent from Task_KeyScan to Task_Display via queue */
typedef struct {
    KeyId_t      key_id;   /**< Which physical key generated the event    */
    KeyEvtType_t evt_type; /**< Short press or long-press repeat          */
} KeyMsg_t;

/* -------------------------------------------------------------------------- */
/*  Queue Handles (defined in app_tasks.c)                                    */
/* -------------------------------------------------------------------------- */

/** Message queue for passing CardInfo_t from CardRead to Display task */
extern osMessageQueueId_t cardQueueHandle;

/** Message queue for passing KeyMsg_t from KeyScan to Display task */
extern osMessageQueueId_t keyQueueHandle;

/** Semaphore for serial command ready notification (ISR -> Task_Serial) */
extern osSemaphoreId_t s_cmdSem;

/** Mutex for RC522 access arbitration (CardRead task vs Serial task) */
extern osMutexId_t rc522MutexHandle;

/** Mutex for W25Q128 Flash storage access (CardRead vs Serial LIST) */
extern osMutexId_t storageMutexHandle;

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

/**
 * @brief  Key scanning task: polls GPIO keys, detects short/long press events,
 *         and sends KeyMsg_t to Display task via keyQueueHandle.
 * @param  argument: unused
 * @note   Polls at 20ms intervals. Short press detected on falling edge.
 *         Long press triggers after KEY_LONG_PRESS_MS, then repeats every KEY_REPEAT_MS.
 */
void Task_KeyScan(void *argument);

/**
 * @brief  Serial command interface initialization (USART1 RX ISR + semaphore)
 */
void Serial_Cmd_Init(void);

/**
 * @brief  Serial command processing task: waits for command lines from ISR,
 *         dispatches to cmd_issue / cmd_read / cmd_clear / cmd_img_cache / cmd_update_img.
 * @param  argument: unused
 */
void Task_Serial(void *argument);


#ifdef __cplusplus
}
#endif

#endif /* __APP_TASKS_H */
