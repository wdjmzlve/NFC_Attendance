/**
 * @file    nfc_storage.c
 * @brief   NFC attendance storage middleware implementation
 * @note    Wraps w25q128.h for semantic config/record storage operations.
 *         Config uses dual-backup (sector 0 primary, sector 1 backup).
 *         Records use circular logging with per-sector erase-on-demand.
 */

#include "nfc_storage.h"
#include "w25q128.h"
#include "cmsis_os.h"
#include <string.h>

/* -------------------------------------------------------------------------- */
/*  External: storage mutex (defined in freertos.c)                           */
/* -------------------------------------------------------------------------- */
extern osMutexId_t storageMutexHandle;

/* -------------------------------------------------------------------------- */
/*  Static Globals                                                             */
/* -------------------------------------------------------------------------- */

/** In-memory copy of loaded device config */
static DeviceConfig_t g_dev_config;

/** In-memory copy of record area header */
static RecordHeader_t g_rec_header;

/** LRU cache table */
static LRUCacheEntry_t g_lru_cache[LRU_CACHE_SIZE];

/** LRU tick counter for eviction ordering */
static uint32_t g_lru_tick = 0;

/* -------------------------------------------------------------------------- */
/*  Internal Helpers                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief  Check if a Flash region is fully erased (all 0xFF)
 * @param  addr: starting byte address
 * @param  len:  bytes to check (max 64)
 * @retval 1: all erased, 0: has non-0xFF data
 */
static uint8_t is_flash_erased(uint32_t addr, uint32_t len)
{
    uint8_t buf[64];
    uint32_t offset = 0;

    while (len > 0) {
        uint32_t chunk = (len > sizeof(buf)) ? sizeof(buf) : len;
        W25QXX_Read(buf, addr + offset, chunk);
        for (uint32_t i = 0; i < chunk; i++) {
            if (buf[i] != 0xFFU) return 0;
        }
        offset += chunk;
        len -= chunk;
    }
    return 1;
}

/**
 * @brief  Calculate config checksum (sum of first 14 bytes, stored as uint16_t)
 */
static uint16_t calc_config_checksum(const DeviceConfig_t *cfg)
{
    const uint8_t *p = (const uint8_t *)cfg;
    uint16_t sum = 0;
    uint8_t i;
    for (i = 0; i < 14U; i++) {
        sum += (uint16_t)p[i];
    }
    return sum;
}

/**
 * @brief  Calculate record header checksum (sum of bytes 0-15, stored as uint32_t)
 */
static uint32_t calc_header_checksum(const RecordHeader_t *hdr)
{
    const uint8_t *p = (const uint8_t *)hdr;
    uint32_t sum = 0;
    uint8_t i;
    for (i = 0; i < 16U; i++) {
        sum += (uint32_t)p[i];
    }
    return sum;
}

/**
 * @brief  Validate config magic and checksum
 * @param  cfg: config to validate
 * @retval 1: valid, 0: invalid
 */
static uint8_t validate_config(const DeviceConfig_t *cfg)
{
    uint32_t magic;
    memcpy(&magic, cfg->magic, 4);
    if (magic != STORAGE_CONFIG_MAGIC) return 0;
    if (cfg->checksum != calc_config_checksum(cfg)) return 0;
    if (cfg->att_mode > ATT_MODE_BOTH) return 0;
    return 1;
}

/**
 * @brief  Validate record header magic, version, checksum, and offset alignment
 * @param  hdr: header to validate
 * @retval 1: valid, 0: invalid
 */
static uint8_t validate_header(const RecordHeader_t *hdr)
{
    uint32_t magic;
    memcpy(&magic, hdr->magic, 4);
    if (magic != STORAGE_HEADER_MAGIC) return 0;
    if (hdr->version > STORAGE_HEADER_VERSION) return 0;
    if (hdr->checksum != calc_header_checksum(hdr)) return 0;
    if (hdr->write_offset % STORAGE_RECORD_SIZE != 0) return 0;
    if (hdr->write_offset >= STORAGE_DATA_TOTAL_BYTES) return 0;
    if (hdr->upload_offset % STORAGE_RECORD_SIZE != 0) return 0;
    if (hdr->upload_offset >= STORAGE_DATA_TOTAL_BYTES) return 0;
    return 1;
}

/**
 * @brief  Write record header to Flash (handles sector erase internally)
 */
static void write_header_to_flash(void)
{
    g_rec_header.checksum = calc_header_checksum(&g_rec_header);

    if (is_flash_erased(STORAGE_HEADER_ADDR, STORAGE_HEADER_SIZE)) {
        /* Sector already erased, direct write */
        W25QXX_Write_NoCheck((uint8_t *)&g_rec_header,
                             STORAGE_HEADER_ADDR, STORAGE_HEADER_SIZE);
    } else {
        /* Need to erase first */
        W25QXX_Erase_Sector(STORAGE_SECTOR_RECORD_HEADER);
        W25QXX_Wait_Busy();
        W25QXX_Write_NoCheck((uint8_t *)&g_rec_header,
                             STORAGE_HEADER_ADDR, STORAGE_HEADER_SIZE);
    }
    W25QXX_Wait_Busy();
}

/**
 * @brief  Initialize record header with defaults
 */
static void init_default_header(void)
{
    memset(&g_rec_header, 0, sizeof(g_rec_header));
    g_rec_header.magic[0] = 'N';
    g_rec_header.magic[1] = 'F';
    g_rec_header.magic[2] = 'C';
    g_rec_header.magic[3] = 'r';
    g_rec_header.write_offset  = 0;
    g_rec_header.total_count   = 0;
    g_rec_header.upload_offset = 0;
    g_rec_header.version       = STORAGE_HEADER_VERSION;
    g_rec_header.reserved      = 0;
    memset(g_rec_header.padding, 0, sizeof(g_rec_header.padding));

    /* Erase header sector and write */
    W25QXX_Erase_Sector(STORAGE_SECTOR_RECORD_HEADER);
    W25QXX_Wait_Busy();
    write_header_to_flash();
}

/**
 * @brief  Initialize default config and write to both sectors
 */
static void init_default_config(void)
{
    memset(&g_dev_config, 0, sizeof(g_dev_config));
    g_dev_config.magic[0] = 'N';
    g_dev_config.magic[1] = 'F';
    g_dev_config.magic[2] = 'C';
    g_dev_config.magic[3] = 'A';
    g_dev_config.dev_id      = STORAGE_DEFAULT_DEV_ID;
    g_dev_config.att_mode    = STORAGE_DEFAULT_ATT_MODE;
    g_dev_config.time_offset = STORAGE_DEFAULT_TIME_OFFSET;
    memset(g_dev_config.reserved, 0, sizeof(g_dev_config.reserved));
    g_dev_config.checksum = calc_config_checksum(&g_dev_config);

    /* Write to both sectors */
    W25QXX_Erase_Sector(STORAGE_SECTOR_CONFIG_PRIMARY);
    W25QXX_Wait_Busy();
    W25QXX_Write_NoCheck((uint8_t *)&g_dev_config,
                         STORAGE_CONFIG_ADDR_PRIMARY, STORAGE_CONFIG_SIZE);
    W25QXX_Wait_Busy();

    W25QXX_Erase_Sector(STORAGE_SECTOR_CONFIG_BACKUP);
    W25QXX_Wait_Busy();
    W25QXX_Write_NoCheck((uint8_t *)&g_dev_config,
                         STORAGE_CONFIG_ADDR_BACKUP, STORAGE_CONFIG_SIZE);
    W25QXX_Wait_Busy();
}

/* ========================================================================== */
/*  Public API                                                                 */
/* ========================================================================== */

/**
 * @brief  Initialize storage subsystem
 */
void NFC_Storage_Init(void)
{
    /* Initialize W25Q128 SPI Flash */
    W25QXX_Init();
    (void)W25QXX_ReadID();  /* Verify chip communication */

    /* Init LRU cache */
    LRU_Clear();

    /* ---- Load config ---- */
    if (NFC_Storage_LoadConfig(&g_dev_config)) {
        /* Config loaded successfully from Flash */
    }

    /* ---- Load record header ---- */
    W25QXX_Read((uint8_t *)&g_rec_header, STORAGE_HEADER_ADDR, STORAGE_HEADER_SIZE);

    if (!validate_header(&g_rec_header)) {
        /* Header invalid or first boot, initialize */
        init_default_header();
    }
}

/**
 * @brief  Load device config: primary -> backup -> default
 * @retval 1: loaded from Flash, 0: using defaults
 */
uint8_t NFC_Storage_LoadConfig(DeviceConfig_t *cfg)
{
    DeviceConfig_t temp_cfg;

    /* Try primary */
    W25QXX_Read((uint8_t *)&temp_cfg, STORAGE_CONFIG_ADDR_PRIMARY, STORAGE_CONFIG_SIZE);
    if (validate_config(&temp_cfg)) {
        memcpy(cfg, &temp_cfg, sizeof(DeviceConfig_t));
        return 1;
    }

    /* Try backup */
    W25QXX_Read((uint8_t *)&temp_cfg, STORAGE_CONFIG_ADDR_BACKUP, STORAGE_CONFIG_SIZE);
    if (validate_config(&temp_cfg)) {
        memcpy(cfg, &temp_cfg, sizeof(DeviceConfig_t));
        /* Restore primary from backup */
        W25QXX_Erase_Sector(STORAGE_SECTOR_CONFIG_PRIMARY);
        W25QXX_Wait_Busy();
        W25QXX_Write_NoCheck((uint8_t *)&temp_cfg,
                             STORAGE_CONFIG_ADDR_PRIMARY, STORAGE_CONFIG_SIZE);
        W25QXX_Wait_Busy();
        return 1;
    }

    /* Both invalid, use defaults and write to Flash */
    init_default_config();
    memcpy(cfg, &g_dev_config, sizeof(DeviceConfig_t));
    return 0;
}

/**
 * @brief  Save config with dual backup (backup first, then primary)
 */
uint8_t NFC_Storage_SaveConfig(const DeviceConfig_t *cfg)
{
    DeviceConfig_t save_cfg;
    memcpy(&save_cfg, cfg, sizeof(DeviceConfig_t));
    save_cfg.checksum = calc_config_checksum(&save_cfg);

    /* Write backup first */
    W25QXX_Erase_Sector(STORAGE_SECTOR_CONFIG_BACKUP);
    W25QXX_Wait_Busy();
    W25QXX_Write_NoCheck((uint8_t *)&save_cfg,
                         STORAGE_CONFIG_ADDR_BACKUP, STORAGE_CONFIG_SIZE);
    W25QXX_Wait_Busy();

    /* Write primary second */
    W25QXX_Erase_Sector(STORAGE_SECTOR_CONFIG_PRIMARY);
    W25QXX_Wait_Busy();
    W25QXX_Write_NoCheck((uint8_t *)&save_cfg,
                         STORAGE_CONFIG_ADDR_PRIMARY, STORAGE_CONFIG_SIZE);
    W25QXX_Wait_Busy();

    /* Update in-memory copy */
    memcpy(&g_dev_config, &save_cfg, sizeof(DeviceConfig_t));
    return 1;
}

/**
 * @brief  Append an attendance record (mutex protected)
 */
uint8_t NFC_Storage_AddRecord(const AttendanceRecord_t *rec)
{
    uint32_t target_addr;

    osMutexAcquire(storageMutexHandle, osWaitForever);

    /* Load latest header state from in-memory copy */
    target_addr = STORAGE_RECORD_BASE_ADDR + g_rec_header.write_offset;

    /* Check if we need to erase the target sector (first write to this sector) */
    if ((g_rec_header.write_offset % STORAGE_SECTOR_SIZE) == 0) {
        uint32_t sector_num = STORAGE_SECTOR_RECORD_START
                            + (g_rec_header.write_offset / STORAGE_SECTOR_SIZE);
        W25QXX_Erase_Sector(sector_num);
        W25QXX_Wait_Busy();
    }

    /* Write 32-byte record */
    W25QXX_Write_NoCheck((uint8_t *)rec, target_addr, STORAGE_RECORD_SIZE);
    W25QXX_Wait_Busy();

    /* Update in-memory header (circular wrap) */
    g_rec_header.write_offset += STORAGE_RECORD_SIZE;
    if (g_rec_header.write_offset >= STORAGE_DATA_TOTAL_BYTES) {
        g_rec_header.write_offset = 0;
    }
    g_rec_header.total_count++;

    /* Sync header to Flash */
    write_header_to_flash();

    osMutexRelease(storageMutexHandle);
    return 1;
}

/**
 * @brief  Read a record by sequential index
 */
uint8_t NFC_Storage_GetRecord(uint32_t index, AttendanceRecord_t *rec)
{
    uint32_t byte_offset;

    if (index >= g_rec_header.total_count) return 0;

    /* Calculate byte offset into data area with circular wrap */
    if (g_rec_header.total_count <= (STORAGE_DATA_TOTAL_BYTES / STORAGE_RECORD_SIZE)) {
        /* No wrap has occurred */
        byte_offset = index * STORAGE_RECORD_SIZE;
    } else {
        /* Wrap has occurred: data starts at write_offset, oldest is write_offset */
        uint32_t start_idx = g_rec_header.total_count
                           - (STORAGE_DATA_TOTAL_BYTES / STORAGE_RECORD_SIZE);
        if (index < start_idx) return 0; /* Overwritten record */
        byte_offset = (g_rec_header.write_offset
                      + (index - start_idx) * STORAGE_RECORD_SIZE)
                      % STORAGE_DATA_TOTAL_BYTES;
    }

    W25QXX_Read((uint8_t *)rec,
                STORAGE_RECORD_BASE_ADDR + byte_offset,
                STORAGE_RECORD_SIZE);
    return 1;
}

/**
 * @brief  Get total record count
 */
uint32_t NFC_Storage_GetTotalRecords(void)
{
    return g_rec_header.total_count;
}

/**
 * @brief  Find the most recent record for a UID (reverse scan)
 */
uint8_t NFC_Storage_FindLastByUID(const uint8_t uid[4], AttendanceRecord_t *rec)
{
    AttendanceRecord_t temp;
    uint32_t i;
    uint32_t total = g_rec_header.total_count;

    if (total == 0) return 0;

    /* Search backwards from most recent */
    uint32_t start = (total > 500) ? (total - 500) : 0; /* Limit scan to last 500 */

    for (i = total; i > start; i--) {
        uint32_t idx = i - 1;
        if (NFC_Storage_GetRecord(idx, &temp)) {
            if (memcmp(temp.uid, uid, 4) == 0) {
                memcpy(rec, &temp, sizeof(AttendanceRecord_t));
                return 1;
            }
        }
    }
    return 0;
}

/**
 * @brief  Get pointer to in-memory config
 */
DeviceConfig_t *NFC_Storage_GetConfig(void)
{
    return &g_dev_config;
}

/* ========================================================================== */
/*  Upload Management (Phase 3: Network)                                        */
/* ========================================================================== */

/**
 * @brief  Get current upload offset (byte offset in data area for next upload)
 */
uint32_t NFC_Storage_GetUploadOffset(void)
{
    uint32_t offset;
    osMutexAcquire(storageMutexHandle, osWaitForever);
    offset = g_rec_header.upload_offset;
    osMutexRelease(storageMutexHandle);
    return offset;
}

/**
 * @brief  Get current write offset (byte offset in data area for next write)
 */
uint32_t NFC_Storage_GetWriteOffset(void)
{
    uint32_t offset;
    osMutexAcquire(storageMutexHandle, osWaitForever);
    offset = g_rec_header.write_offset;
    osMutexRelease(storageMutexHandle);
    return offset;
}

/**
 * @brief  Advance upload offset by one record (32 bytes) with circular wrap
 * @note   Recalculates header checksum and writes to Flash.
 *         Must be called after a successful upload ACK.
 */
void NFC_Storage_AdvanceUploadOffset(void)
{
    osMutexAcquire(storageMutexHandle, osWaitForever);

    g_rec_header.upload_offset += STORAGE_RECORD_SIZE;
    if (g_rec_header.upload_offset >= STORAGE_DATA_TOTAL_BYTES) {
        g_rec_header.upload_offset = 0;
    }

    write_header_to_flash();

    osMutexRelease(storageMutexHandle);
}

/**
 * @brief  Read a record at a specific byte offset in the data area
 * @param  byte_offset: byte offset from STORAGE_RECORD_BASE_ADDR
 * @param  rec: output record buffer (32 bytes)
 * @retval 1: success (always succeeds for valid offsets)
 * @note   Caller must ensure byte_offset is valid (0 <= offset < STORAGE_DATA_TOTAL_BYTES
 *         and offset % STORAGE_RECORD_SIZE == 0)
 */
uint8_t NFC_Storage_GetRecordAtOffset(uint32_t byte_offset, AttendanceRecord_t *rec)
{
    if (byte_offset >= STORAGE_DATA_TOTAL_BYTES) return 0;
    if ((byte_offset % STORAGE_RECORD_SIZE) != 0) return 0;

    W25QXX_Read((uint8_t *)rec,
                STORAGE_RECORD_BASE_ADDR + byte_offset,
                STORAGE_RECORD_SIZE);
    return 1;
}

/* ========================================================================== */
/*  LRU Cache                                                                  */
/* ========================================================================== */

/**
 * @brief  Look up a UID in LRU cache
 */
uint8_t LRU_Lookup(const uint8_t uid[4], uint8_t *last_event)
{
    uint8_t i;
    for (i = 0; i < LRU_CACHE_SIZE; i++) {
        if (g_lru_cache[i].valid
            && memcmp(g_lru_cache[i].uid, uid, 4) == 0) {
            g_lru_cache[i].lru_tick = g_lru_tick++;
            *last_event = g_lru_cache[i].last_event;
            return 1;
        }
    }
    return 0;
}

/**
 * @brief  Update or insert a UID in LRU cache
 */
void LRU_Update(const uint8_t uid[4], uint8_t event)
{
    uint8_t i;
    uint8_t empty_slot = 0xFFU;
    uint8_t oldest_slot = 0;
    uint32_t oldest_tick = 0xFFFFFFFFUL;

    for (i = 0; i < LRU_CACHE_SIZE; i++) {
        if (g_lru_cache[i].valid
            && memcmp(g_lru_cache[i].uid, uid, 4) == 0) {
            /* Update existing entry */
            g_lru_cache[i].last_event = event;
            g_lru_cache[i].lru_tick   = g_lru_tick++;
            return;
        }
        if (!g_lru_cache[i].valid) {
            empty_slot = i;
        }
        if (g_lru_cache[i].lru_tick < oldest_tick) {
            oldest_tick = g_lru_cache[i].lru_tick;
            oldest_slot = i;
        }
    }

    /* Not found, need to insert */
    if (empty_slot != 0xFFU) {
        i = empty_slot;
    } else {
        i = oldest_slot; /* Evict oldest */
    }

    memcpy(g_lru_cache[i].uid, uid, 4);
    g_lru_cache[i].last_event = event;
    g_lru_cache[i].lru_tick   = g_lru_tick++;
    g_lru_cache[i].valid      = 1;
}

/**
 * @brief  Clear all LRU cache entries
 */
void LRU_Clear(void)
{
    uint8_t i;
    for (i = 0; i < LRU_CACHE_SIZE; i++) {
        g_lru_cache[i].valid = 0;
        memset(g_lru_cache[i].uid, 0, 4);
    }
    g_lru_tick = 0;
}
