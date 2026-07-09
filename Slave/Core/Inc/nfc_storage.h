/**
 * @file    nfc_storage.h
 * @brief   NFC attendance storage middleware - wraps W25Q128 for config & records
 * @note    Provides semantic APIs over w25q128.h: config dual-backup,
 *          circular record logging, LRU cache, and safe record header sync.
 */

#ifndef __NFC_STORAGE_H
#define __NFC_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* -------------------------------------------------------------------------- */
/*  Attendance Event Types                                                     */
/* -------------------------------------------------------------------------- */
#define ATT_EVENT_ENTRY     0x00U   /**< Entry event                          */
#define ATT_EVENT_EXIT      0x01U   /**< Exit event                           */

/* -------------------------------------------------------------------------- */
/*  Attendance Record Status                                                   */
/* -------------------------------------------------------------------------- */
#define ATT_STATUS_NORMAL   0x00U   /**< Normal valid record                  */
#define ATT_STATUS_DUP      0x01U   /**< Duplicate swipe                      */
#define ATT_STATUS_NO_ENTRY 0x02U   /**< No prior entry record for exit       */
#define ATT_STATUS_UNKNOWN  0x03U   /**< Unknown/invalid card                 */

/* -------------------------------------------------------------------------- */
/*  Attendance Mode                                                            */
/* -------------------------------------------------------------------------- */
#define ATT_MODE_ENTRY      0x00U   /**< Entry-only point                     */
#define ATT_MODE_EXIT       0x01U   /**< Exit-only point                      */
#define ATT_MODE_BOTH       0x02U   /**< Entry+exit point (default)           */

/* -------------------------------------------------------------------------- */
/*  LED/Buzzer Feedback Event Types                                            */
/* -------------------------------------------------------------------------- */
typedef enum {
    FB_EVT_NONE = 0,       /**< No feedback                                  */
    FB_EVT_VALID_ENTRY,    /**< Valid entry: green LED + high beep           */
    FB_EVT_VALID_EXIT,     /**< Valid exit:  green LED + high beep + dur     */
    FB_EVT_INVALID,        /**< Invalid card: red blink x2 + low beep x2    */
    FB_EVT_DUP,            /**< Duplicate:   yellow LED + low short beep    */
    FB_EVT_ADMIN           /**< Admin card:  red+green + long high beep     */
} FeedbackEvt_t;

/* -------------------------------------------------------------------------- */
/*  Flash Layout Constants                                                     */
/* -------------------------------------------------------------------------- */
#define STORAGE_SECTOR_CONFIG_PRIMARY   0U
#define STORAGE_SECTOR_CONFIG_BACKUP    1U
#define STORAGE_SECTOR_RECORD_HEADER    2U
#define STORAGE_SECTOR_RECORD_START     3U
#define STORAGE_SECTOR_SIZE             4096U

#define STORAGE_CONFIG_ADDR_PRIMARY     (STORAGE_SECTOR_CONFIG_PRIMARY * STORAGE_SECTOR_SIZE)
#define STORAGE_CONFIG_ADDR_BACKUP      (STORAGE_SECTOR_CONFIG_BACKUP  * STORAGE_SECTOR_SIZE)
#define STORAGE_HEADER_ADDR             (STORAGE_SECTOR_RECORD_HEADER  * STORAGE_SECTOR_SIZE)
#define STORAGE_RECORD_BASE_ADDR        (STORAGE_SECTOR_RECORD_START   * STORAGE_SECTOR_SIZE)

#define STORAGE_CONFIG_SIZE             16U
#define STORAGE_HEADER_SIZE             32U
#define STORAGE_RECORD_SIZE             32U
#define STORAGE_RECORDS_PER_SECTOR      128U    /* 4096 / 32 */
#define STORAGE_DATA_TOTAL_SECTORS      4093U   /* sectors 3..4095 */
#define STORAGE_DATA_TOTAL_BYTES        (STORAGE_DATA_TOTAL_SECTORS * STORAGE_SECTOR_SIZE)

#define STORAGE_CONFIG_MAGIC            0x4143464EU  /* "NFCA" LE */
#define STORAGE_HEADER_MAGIC            0x7243464EU  /* "NFCr" LE */
#define STORAGE_HEADER_VERSION          1U

/* Default config values */
#define STORAGE_DEFAULT_DEV_ID          1U
#define STORAGE_DEFAULT_ATT_MODE        ATT_MODE_BOTH
#define STORAGE_DEFAULT_TIME_OFFSET     0

/* LRU cache size */
#define LRU_CACHE_SIZE                  8U

/* -------------------------------------------------------------------------- */
/*  Attendance Record (32 bytes, packed for Flash storage)                     */
/* -------------------------------------------------------------------------- */
__packed typedef struct {
    uint32_t seq;           /**< Record sequence number                     */
    uint8_t  uid[4];        /**< 4-byte card UID                            */
    uint32_t id_num;        /**< Student/employee ID number                 */
    uint8_t  card_type;     /**< Card type (0=normal, 1=image, 2=admin)    */
    uint16_t year;          /**< Year (e.g. 2026)                           */
    uint8_t  month;         /**< Month 1-12                                 */
    uint8_t  day;           /**< Day 1-31                                   */
    uint8_t  hour;          /**< Hour 0-23                                  */
    uint8_t  minute;        /**< Minute 0-59                                */
    uint8_t  second;        /**< Second 0-59                                */
    uint8_t  event;         /**< ATT_EVENT_ENTRY or ATT_EVENT_EXIT          */
    uint8_t  status;        /**< ATT_STATUS_NORMAL/DUP/NO_ENTRY/UNKNOWN     */
    uint16_t dev_id;        /**< Device ID                                  */
    int32_t  time_offset;   /**< Local RTC offset from server time (sec)    */
    uint32_t duration;      /**< Duration in seconds (exit events only)     */
} AttendanceRecord_t;

/* -------------------------------------------------------------------------- */
/*  Device Config (16 bytes, packed)                                           */
/* -------------------------------------------------------------------------- */
__packed typedef struct {
    uint8_t  magic[4];      /**< "NFCA" magic number                        */
    uint16_t dev_id;        /**< Device ID (1-65535)                        */
    uint8_t  att_mode;      /**< ATT_MODE_ENTRY/EXIT/BOTH                   */
    int32_t  time_offset;   /**< Time offset from server (seconds)          */
    uint8_t  reserved[3];   /**< Reserved for future use                    */
    uint16_t checksum;      /**< Checksum of first 14 bytes                 */
} DeviceConfig_t;

/* -------------------------------------------------------------------------- */
/*  Record Header (32 bytes, packed)                                           */
/* -------------------------------------------------------------------------- */
__packed typedef struct {
    uint8_t  magic[4];       /**< "NFCr" magic number                       */
    uint32_t write_offset;   /**< Byte offset in data area for next write   */
    uint32_t total_count;    /**< Total records ever written                */
    uint32_t upload_offset;  /**< Byte offset for next upload (server sync) */
    uint32_t checksum;       /**< Checksum of first 16 bytes                */
    uint16_t version;        /**< Header format version                     */
    uint16_t reserved;       /**< Reserved                                  */
    uint8_t  padding[8];     /**< Padding to 32 bytes                       */
} RecordHeader_t;

/* -------------------------------------------------------------------------- */
/*  LRU Cache Entry                                                            */
/* -------------------------------------------------------------------------- */
typedef struct {
    uint8_t  uid[4];        /**< Card UID                                   */
    uint8_t  last_event;    /**< Last event type (ATT_EVENT_ENTRY/EXIT)      */
    uint32_t lru_tick;      /**< Tick for eviction ordering                 */
    uint8_t  valid;         /**< 1 if entry is occupied                     */
} LRUCacheEntry_t;

/* -------------------------------------------------------------------------- */
/*  Public Functions                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Initialize storage subsystem: W25Q128 init, config load, header validate
 * @note   Must be called once after FreeRTOS scheduler starts
 */
void NFC_Storage_Init(void);

/**
 * @brief  Load device config from Flash (primary, fallback to backup, default)
 * @param  cfg: output config buffer
 * @retval 1: loaded from Flash, 0: using defaults
 */
uint8_t NFC_Storage_LoadConfig(DeviceConfig_t *cfg);

/**
 * @brief  Save device config to Flash with dual-backup (backup first, then primary)
 * @param  cfg: config to save
 * @retval 1: success, 0: failure
 */
uint8_t NFC_Storage_SaveConfig(const DeviceConfig_t *cfg);

/**
 * @brief  Append an attendance record to Flash storage (circular)
 * @param  rec: pointer to 32-byte AttendanceRecord_t
 * @retval 1: success, 0: failure
 * @note   Protected by storage mutex internally
 */
uint8_t NFC_Storage_AddRecord(const AttendanceRecord_t *rec);

/**
 * @brief  Read a record by sequential index
 * @param  index: 0-based record index
 * @param  rec: output record buffer
 * @retval 1: success, 0: index out of range
 */
uint8_t NFC_Storage_GetRecord(uint32_t index, AttendanceRecord_t *rec);

/**
 * @brief  Get total number of records stored
 * @retval Total record count
 */
uint32_t NFC_Storage_GetTotalRecords(void);

/**
 * @brief  Find the most recent record for a given card UID (reverse scan)
 * @param  uid: 4-byte card UID to search
 * @param  rec: output record buffer (if found)
 * @retval 1: found, 0: not found (new card)
 * @note   Scans Flash backwards from latest record; may be slow for many records
 */
uint8_t NFC_Storage_FindLastByUID(const uint8_t uid[4], AttendanceRecord_t *rec);

/* -------------------------------------------------------------------------- */
/*  LRU Cache Functions                                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Look up a card UID in the LRU cache
 * @param  uid: 4-byte card UID
 * @param  last_event: output, last event type if found
 * @retval 1: cache hit, 0: cache miss
 */
uint8_t LRU_Lookup(const uint8_t uid[4], uint8_t *last_event);

/**
 * @brief  Update or insert a card UID in the LRU cache
 * @param  uid: 4-byte card UID
 * @param  event: ATT_EVENT_ENTRY or ATT_EVENT_EXIT
 */
void LRU_Update(const uint8_t uid[4], uint8_t event);

/**
 * @brief  Clear all LRU cache entries (e.g. on attendance mode change)
 */
void LRU_Clear(void);

/**
 * @brief  Get pointer to the global device config (loaded at init)
 */
DeviceConfig_t *NFC_Storage_GetConfig(void);

#ifdef __cplusplus
}
#endif

#endif /* __NFC_STORAGE_H */
