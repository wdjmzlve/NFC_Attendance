/**
 * @file    app_tasks.c
 * @brief   Application tasks: Display (splash/clock/card/settings) and CardRead
 * @note    UTF-8 encoding
 */

/* Includes ------------------------------------------------------------------*/
#include "app_tasks.h"
#include "oled.h"
#include "rc522.h"
#include "bsp_rtc.h"
#include "gui.h"
#include "ds18b20.h"
#include "uart_drv.h"
#include "nfc_storage.h"
#include "w25q128.h"
#include "midi.h"
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "u8g2.h"
#include "esp01s.h"
#include "rtc.h"
/* -------------------------------------------------------------------------- */
/*  Constants and Macros                                                      */
/* -------------------------------------------------------------------------- */

/* Task periods (ms) */
#define DISPLAY_PERIOD_MS       50U
#define CARD_READ_PERIOD_MS     300U
#define SPLASH_DURATION_MS      3000U
#define CARD_DISPLAY_MS         3000U
#define TEMP_READ_PERIOD_MS     2000U

/* Key timing (ms) */
#define KEY_LONG_PRESS_MS       600U
#define KEY_REPEAT_MS           150U
#define BLINK_PERIOD_MS         500U

/* Network configuration (Phase 3: modify these for your environment) */
#define WIFI_SSID               "Misaki"
#define WIFI_PASSWORD           "Yc20231122"
#define TCP_SERVER_IP           "api.seniverse.com"
#define TCP_SERVER_PORT         80U
#define NTP_SERVER              "ntp.aliyun.com"
#define NTP_TIMEZONE            8

/* Seniverse (Xinzhi) Weather API key */
#define WEATHER_API_KEY         "SOot2cNJy0vjvbzrc"
#define WEATHER_LOCATION        "hangzhou"
#define WEATHER_LANGUAGE        "zh-Hans"
#define WEATHER_UNIT            "c"

/* Network task timing (ms) */
#define NETWORK_PERIOD_MS       1000U
#define RECONNECT_INTERVAL_SEC  60U
#define WEATHER_INTERVAL_MS     1800000U  /* 30 minutes */
#define HEARTBEAT_INTERVAL_MS   60000U
#define UPLOAD_ACK_TIMEOUT_MS   5000U

/* Display layout Y coordinates (pixels) */
#define LINE1_Y   12U
#define LINE2_Y   26U
#define LINE3_Y   40U
#define LINE4_Y   54U

/* Name/Sector bitmap dimensions (SSD1306 page format: 161 = ceil(80/8) * 16 + 1) */
#define NAME_BMP_W     80U
#define NAME_BMP_H     16U
#define SECTOR_BMP_W   80U
#define SECTOR_BMP_H   16U
#define BMP_TOTAL_BYTES 322U  /* 161 Name + 161 Sector */

/* Key pin macros */
#define KEY_MODE_PIN      KEY1_Pin
#define KEY_MODE_PORT     KEY1_GPIO_Port
#define KEY_UP_PIN        KEY2_Pin
#define KEY_UP_PORT       KEY2_GPIO_Port
#define KEY_DOWN_PIN      KEY3_Pin
#define KEY_DOWN_PORT     KEY3_GPIO_Port
#define KEY_SAVE_PIN      KEY4_Pin
#define KEY_SAVE_PORT     KEY4_GPIO_Port

#define KEY_IS_PRESSED(port, pin)  (HAL_GPIO_ReadPin((port), (pin)) == GPIO_PIN_RESET)

/*
 * LED macros: LEDs are open-drain output, anode to VCC, cathode to GPIO.
 * GPIO_PIN_RESET (LOW)  = NMOS on  = current flows = LED ON
 * GPIO_PIN_SET   (HIGH) = NMOS off = Hi-Z         = LED OFF
 */
#define LED_ON(port, pin)   HAL_GPIO_WritePin((port), (pin), GPIO_PIN_RESET)
#define LED_OFF(port, pin)  HAL_GPIO_WritePin((port), (pin), GPIO_PIN_SET)

/* -------------------------------------------------------------------------- */
/*  Display State Machine                                                     */
/* -------------------------------------------------------------------------- */

typedef enum {
    DISP_MODE_SPLASH,        /* Boot splash screen (3 sec)                    */
    DISP_MODE_CLOCK,         /* Clock + temperature standby                   */
    DISP_MODE_SETTING,       /* Date/time setting via keys                    */
    DISP_MODE_CARD,          /* Card detected info + attendance result        */
    DISP_MODE_ADMIN_INFO,    /* Admin card info display (2 sec)               */
    DISP_MODE_ADMIN_SETTING  /* Admin config: dev_id + att_mode               */
} DispMode_t;

typedef enum {
    FIELD_YEAR = 0,
    FIELD_MONTH,
    FIELD_DAY,
    FIELD_HOUR,
    FIELD_MINUTE,
    FIELD_SECOND,
    FIELD_COUNT
} SetField_t;

typedef enum {
    ADMIN_FIELD_DEV_ID = 0,  /**< Device ID setting field                     */
    ADMIN_FIELD_ATT_MODE,    /**< Attendance mode setting field               */
    ADMIN_FIELD_COUNT
} AdminField_t;

/* -------------------------------------------------------------------------- */
/*  LED/Buzzer Feedback State Machine (driven by Task_Display each cycle)     */
/* -------------------------------------------------------------------------- */
#define FB_LED_GREEN_PIN     LED4_Pin
#define FB_LED_GREEN_PORT    LED4_GPIO_Port
#define FB_LED_RED_PIN       LED5_Pin
#define FB_LED_RED_PORT      LED5_GPIO_Port
#define FB_LED_YELLOW_PIN    LED6_Pin
#define FB_LED_YELLOW_PORT   LED6_GPIO_Port
#define FB_LED_ADMIN_PIN     LED7_Pin
#define FB_LED_ADMIN_PORT    LED7_GPIO_Port

typedef struct {
    uint8_t  active;          /**< 1 when feedback sequence is running        */
    uint8_t  type;            /**< FeedbackEvt_t                              */
    uint32_t start_tick;      /**< Tick when feedback started                 */
    uint8_t  phase;           /**< Current phase in multi-phase sequence      */
    uint32_t phase_deadline;  /**< Tick when current phase ends               */
} FeedbackState_t;

/* -------------------------------------------------------------------------- */
/*  Message Queue                                                             */
/* -------------------------------------------------------------------------- */

#define CARD_QUEUE_SIZE  4U
osMessageQueueId_t cardQueueHandle;
/* Card bitmap buffers: read from sectors 9+, used by display task */
static uint8_t card_name_bmp[161];
static uint8_t card_sector_bmp[161];
static uint8_t card_has_bmp = 0;

/* Avatar bitmap buffer: 48x64 pixels, 1bpp horizontal packing (384 bytes) */
static uint8_t card_avatar_bmp[384];
static uint8_t card_has_avatar = 0;

/* -------------------------------------------------------------------------- */
/*  RC522互斥量                                                               */
/* -------------------------------------------------------------------------- */
osMutexId_t rc522MutexHandle;

/* -------------------------------------------------------------------------- */
/*  W25Q128存储互斥量                                                         */
/* -------------------------------------------------------------------------- */
osMutexId_t storageMutexHandle;

/* -------------------------------------------------------------------------- */
/*  图片数据缓存（静态分配，不占用堆）                                          */
/* -------------------------------------------------------------------------- */
uint8_t g_img_avatar[IMG_AVATAR_BLOCKS][IMG_BLOCK_SIZE];
uint8_t g_img_name[IMG_NAME_BLOCKS][IMG_BLOCK_SIZE];
uint8_t g_img_dept[IMG_DEPT_BLOCKS][IMG_BLOCK_SIZE];

/* -------------------------------------------------------------------------- */
/*  串口指令接口相关全局变量                                                    */
/* -------------------------------------------------------------------------- */

/** USART1 串口驱动实例（供上位机指令通信） */
static UartDrv_t g_serialDrv;

/** 串口接收行缓冲区：累积字符直到收到 \n */
#define SERIAL_LINE_BUF_SIZE    256
static char s_line_buf[SERIAL_LINE_BUF_SIZE];
static uint16_t s_line_len = 0;

/** 已就绪的完整指令行（中断填充，任务消费） */
static char s_ready_cmd[SERIAL_LINE_BUF_SIZE];
static volatile uint8_t s_cmd_ready = 0;

/** 指令就绪信号量（ISR 中释放，Task_Serial 中等待） */
osSemaphoreId_t s_cmdSem;

/* -------------------------------------------------------------------------- */
/*  扇区映射表 —— 图片数据对应 M1 卡物理位置                                   */
/* -------------------------------------------------------------------------- */

/** 单块映射：扇区号 + 块号（块 0~2，块 3 为密钥块不可写） */
typedef struct {
    uint8_t sec;   /**< 扇区号 0~15 */
    uint8_t blk;   /**< 块号 0~2     */
} SecBlk_t;

/**
 * 头像 24 块 → 扇区 1~8，每扇区块 0,1,2
 */
static const SecBlk_t s_avatar_map[IMG_AVATAR_BLOCKS] = {
    {1,0},{1,1},{1,2}, {2,0},{2,1},{2,2},
    {3,0},{3,1},{3,2}, {4,0},{4,1},{4,2},
    {5,0},{5,1},{5,2}, {6,0},{6,1},{6,2},
    {7,0},{7,1},{7,2}, {8,0},{8,1},{8,2},
};

/**
 * 姓名 10 块 → 扇区 9~11（块 0,1,2）+ 扇区 12 块 0
 */
static const SecBlk_t s_name_map[IMG_NAME_BLOCKS] = {
    {9,0},{9,1},{9,2}, {10,0},{10,1},{10,2},
    {11,0},{11,1},{11,2}, {12,0},
};

/**
 * 部门 10 块 → 扇区 12 块 1~2 + 扇区 13~14（块 0,1,2）+ 扇区 15 块 0~1
 */
static const SecBlk_t s_dept_map[IMG_DEPT_BLOCKS] = {
    {12,1},{12,2}, {13,0},{13,1},{13,2},
    {14,0},{14,1},{14,2}, {15,0},{15,1},
};

/* -------------------------------------------------------------------------- */
/*  工具函数：十六进制字符 → 数值                                              */
/* -------------------------------------------------------------------------- */
static uint8_t hex_char_to_nibble(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0;
}

static uint8_t hex_pair_to_byte(const char *hex)
{
    return (uint8_t)((hex_char_to_nibble(hex[0]) << 4U) | hex_char_to_nibble(hex[1]));
}

/**
 * @brief  将 hex_len 个十六进制字符转换为 byte_len 个字节
 * @param  hex      输入的 hex 字符串
 * @param  hex_len  hex 字符数（必须为偶数）
 * @param  out      输出字节缓冲区
 * @param  out_len  预期输出的字节数 = hex_len / 2
 * @retval 0:成功  -1:长度不匹配
 */
static int hex_to_bytes(const char *hex, uint16_t hex_len,
                         uint8_t *out, uint16_t out_len)
{
    if (hex_len != out_len * 2U)
        return -1;
    for (uint16_t i = 0; i < out_len; i++) {
        out[i] = hex_pair_to_byte(hex + i * 2U);
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/*  RC522 互斥锁封装                                                           */
/* -------------------------------------------------------------------------- */

/** 以阻塞方式获取 RC522 互斥锁（串口任务使用） */
static void rc522_lock(void)
{
    osMutexAcquire(rc522MutexHandle, osWaitForever);
}

/** 释放 RC522 互斥锁 */
static void rc522_unlock(void)
{
    osMutexRelease(rc522MutexHandle);
}

/* -------------------------------------------------------------------------- */
/*  账户头校验和计算                                                           */
/* -------------------------------------------------------------------------- */
static void calc_checksum(uint8_t *block_data)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 14U; i++) {
        sum += block_data[i];
    }
    block_data[14] = (uint8_t)(sum & 0xFFU);
    block_data[15] = (uint8_t)((sum >> 8U) & 0xFFU);
}

/* -------------------------------------------------------------------------- */
/*  批量块写入：按扇区分组认证 + 写入                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief  将 sec_blk_map 中的 len 个块数据写入 M1 卡
 * @param  data       数据源（每个元素 16 字节）
 * @param  sec_blk_map  扇区/块映射表
 * @param  len        映射表长度（块数）
 * @retval 0:全部写入成功  负值:失败（-1 认证失败，-2 写块失败）
 * @note   每个扇区的块 3（密钥块）不会被写入。
 *         同一扇区内的多个块共享一次认证。
 */
static int write_blocks_mapped(uint8_t (*data)[16], const SecBlk_t *sec_blk_map, uint16_t len)
{
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t uid[4];
    char status;
    int8_t last_sector = -1;

    for (uint16_t i = 0; i < len; i++) {
        uint8_t sec = sec_blk_map[i].sec;
        uint8_t blk = sec_blk_map[i].blk;
        uint8_t abs_addr = sec * 4U + blk;

        int block_success = 0;
        /* 每个块最多重试 5 次 */
        for (int retry = 0; retry < 5; retry++) {
            /* 如果是跨扇区，或者当前重试中，需要重新寻卡和认证 */
            if ((int8_t)sec != last_sector || retry > 0) {
                RC522_Halt();  // 确保卡片状态复位
                osDelay(2);
                if (RC522_ScanCard(uid) != RC522_OK) continue;
                
                uint8_t trailer = sec * 4U + 3U;
                if (RC522_AuthState(RC522_PICC_AUTHENT1A, trailer, default_key, uid) != RC522_OK) continue;
            }

            status = RC522_Write(abs_addr, (uint8_t *)data[i]);
            if (status == RC522_OK) {
                last_sector = (int8_t)sec;
                block_success = 1;
                break; // 写入成功，跳出重试循环
            }
        }
        
        /* 5次全败，彻底放弃 */
        if (!block_success) {
            RC522_Halt();
            return -2;
        }
    }

    RC522_Halt();
    return 0;
}

/**
 * @brief  将 sec_blk_map 中所有块清零（销卡用）
 * @param  sec_blk_map  扇区/块映射表
 * @param  len        映射表长度
 * @retval 0:成功  负值:失败
 * @note   清除逻辑同 write_blocks_mapped，数据全为 0x00
 */
static int clear_blocks_mapped(const SecBlk_t *sec_blk_map, uint16_t len)
{
    uint8_t zeros[16];
    memset(zeros, 0, sizeof(zeros));

    /* 用一块全零数据重复写入——write_blocks_mapped 从 data[i] 读 */
    /* 为复用 write_blocks_mapped，构造一个临时二维数组 */
    /* 这里直接逐块清零更简单 */
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t uid[4];
    uint8_t tagType[2];
    char status;
    int8_t last_sector = -1;

    status = RC522_Request(RC522_PICC_REQALL, tagType);
    if (status != RC522_OK) return -1;
    status = RC522_Anticoll(uid);
    if (status != RC522_OK) return -1;
    status = RC522_Select(uid);
    if (status != RC522_OK) return -1;

    for (uint16_t i = 0; i < len; i++) {
        uint8_t sec = sec_blk_map[i].sec;
        uint8_t blk = sec_blk_map[i].blk;
        uint8_t abs_addr = sec * 4U + blk;

        if ((int8_t)sec != last_sector) {
            uint8_t trailer = sec * 4U + 3U;
            status = RC522_AuthState(RC522_PICC_AUTHENT1A,
                                      trailer, default_key, uid);
            if (status != RC522_OK) return -1;
            last_sector = (int8_t)sec;
        }
        status = RC522_Write(abs_addr, zeros);
        if (status != RC522_OK) return -2;
    }

    RC522_Halt();
    return 0;
}

/* ========================================================================== */
/*  串口指令处理函数                                                           */
/* ========================================================================== */

/**
 * @brief  通过串口发送响应字符串并等待发送完成
 */
static void send_response(const char *resp)
{
    UartDrv_SendStr(&g_serialDrv, resp);
}

/* -------- ISSUE：发卡初始化 -------- */

/**
 * @brief  处理 ISSUE 指令
 * @param  args  格式: "UID_HEX,SID,0,CTYPE"
 * @note   将学号、卡类型等写入 M1 卡扇区 0 块 1
 */
static void cmd_issue(const char *args)
{
    uint8_t uid[4];
    uint32_t sid;
    uint8_t card_type;
    uint8_t block1[16];
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /* 解析: "A1B2C3D4,23040722,0,1" */
    char uid_hex[9] = {0};
    uint8_t i = 0;
    while (*args && *args != ',' && i < 8) {
        uid_hex[i++] = *args++;
    }
    uid_hex[i] = '\0';
    if (*args != ',') { send_response("ERR\n"); return; }
    args++;

    /* 解析学号（十进制） */
    sid = 0;
    while (*args && *args != ',') {
        if (*args >= '0' && *args <= '9') {
            sid = sid * 10U + (uint32_t)(*args - '0');
        } else { send_response("ERR\n"); return; }
        args++;
    }
    if (*args != ',') { send_response("ERR\n"); return; }
    args++;

    /* 跳过保留字段 "0" */
    while (*args && *args != ',') args++;
    if (*args != ',') { send_response("ERR\n"); return; }
    args++;

    /* 解析卡类型 */
    card_type = (uint8_t)(*args - '0');
    if (card_type > 2) { send_response("ERR\n"); return; }

    /* 转换 UID hex → bytes */
    if (hex_to_bytes(uid_hex, 8, uid, 4) != 0) {
        send_response("ERR\n");
        return;
    }

    /* ---- 构造块 1 数据（与 Task_CardRead 规范一致） ---- */
    memset(block1, 0, sizeof(block1));
    memcpy(&block1[0], uid, 4);             /* [0..3] UID */
    block1[4]  = (uint8_t)((sid >> 24U) & 0xFFU);   /* [4..7] SID big-endian */
    block1[5]  = (uint8_t)((sid >> 16U) & 0xFFU);
    block1[6]  = (uint8_t)((sid >> 8U)  & 0xFFU);
    block1[7]  = (uint8_t)( sid        & 0xFFU);
    block1[12] = card_type;                         /* [12] card type */
    /* [13] = 0 (status flag) */
    calc_checksum(block1);                          /* [14..15] checksum */

    /* ---- 锁定 RC522，寻卡验证 UID，写卡 ---- */
    rc522_lock();

    char ret = RC522_ERR;
    uint8_t scanned_uid[4];
    /* 增加寻卡重试 */
    for (int retry = 0; retry < 5; retry++) {
        RC522_Halt();
        osDelay(2);
        ret = RC522_ScanCard(scanned_uid);
        if (ret == RC522_OK) break;
    }

    if (ret != RC522_OK) {
        rc522_unlock();
        send_response("ERR\n");
        return;
    }

    /* 验证 UID 是否匹配 */
    if (memcmp(scanned_uid, uid, 4) != 0) {
        RC522_Halt();
        rc522_unlock();
        send_response("ERR\n");
        return;
    }

    /* 认证扇区 0 */
    ret = RC522_AuthState(RC522_PICC_AUTHENT1A, 3, default_key, scanned_uid);
    if (ret != RC522_OK) {
        RC522_Halt();
        rc522_unlock();
        send_response("ERR\n");
        return;
    }

    /* 写入块 1 */
    ret = RC522_Write(1, block1);
    RC522_Halt();
    rc522_unlock();

    send_response((ret == RC522_OK) ? "OK\n" : "ERR\n");
}

/* -------- IMGA / IMGN / IMGD：图片数据缓存 -------- */

/**
 * @brief  处理 IMGx 指令（IMGA/IMGN/IMGD）
 * @param  cmd_type   'A'=头像  'N'=姓名  'D'=部门
 * @param  args       格式: "xx:16_byte_hex_data"
 *         xx 为十进制块序号（00-23 或 00-09）
 */
static void cmd_img_cache(char cmd_type, const char *args)
{
    /* 解析块序号 */
    uint16_t blk_idx = 0;
    while (*args && *args != ':') {
        if (*args >= '0' && *args <= '9')
            blk_idx = blk_idx * 10U + (uint8_t)(*args - '0');
        else { send_response("ERR\n"); return; }
        args++;
    }
    if (*args != ':') { send_response("ERR\n"); return; }
    args++;  /* 跳过 ':' */

    /* 校验范围 */
    uint16_t max_block;
    uint8_t (*cache)[16];
    switch (cmd_type) {
    case 'A': max_block = IMG_AVATAR_BLOCKS; cache = g_img_avatar; break;
    case 'N': max_block = IMG_NAME_BLOCKS;   cache = g_img_name;   break;
    case 'D': max_block = IMG_DEPT_BLOCKS;   cache = g_img_dept;   break;
    default:  send_response("ERR\n"); return;
    }
    if (blk_idx >= max_block) {
        send_response("ERR\n");
        return;
    }

    /* 解析 32 个 hex 字符 → 16 字节 */
    uint16_t hex_len = (uint16_t)strlen(args);
    while (hex_len > 0 && (args[hex_len-1] == '\n' || args[hex_len-1] == '\r'))
        hex_len--;
    if (hex_len != 32) {
        send_response("ERR\n");
        return;
    }

    if (hex_to_bytes(args, 32, cache[blk_idx], 16) != 0) {
        send_response("ERR\n");
        return;
    }

    send_response("OK\n");
}

/* -------- UPDATEIMG：图片数据写入 M1 卡 -------- */

static void cmd_update_img(void)
{
    int ret;

    rc522_lock();
    /* 按映射批量写入：头像 → 姓名 → 部门 */
    ret = write_blocks_mapped(g_img_avatar, s_avatar_map, IMG_AVATAR_BLOCKS);
    if (ret == 0)
        ret = write_blocks_mapped(g_img_name, s_name_map, IMG_NAME_BLOCKS);
    if (ret == 0)
        ret = write_blocks_mapped(g_img_dept, s_dept_map, IMG_DEPT_BLOCKS);
    rc522_unlock();

    send_response((ret == 0) ? "OK\n" : "ERR\n");
}

/* -------- READ：读卡验证 -------- */

static void cmd_read(void)
{
    uint8_t uid[4];
    uint8_t block1[16];
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    char status = RC522_ERR;

    rc522_lock();

    /* 增加重试机制：寻卡、认证、读块必须连续成功 */
    for (int retry = 0; retry < 5; retry++) {
        RC522_Halt();  // 先强制复位卡片射频状态
        osDelay(2);    // 给予卡片响应时间
        
        status = RC522_ScanCard(uid);
        if (status == RC522_OK) {
            status = RC522_AuthState(RC522_PICC_AUTHENT1A, 3, default_key, uid);
            if (status == RC522_OK) {
                status = RC522_Read(1, block1);
                if (status == RC522_OK) {
                    break; // 读卡完全成功，跳出重试循环
                }
            }
        }
    }

    RC522_Halt();
    rc522_unlock();

    if (status != RC522_OK) {
        send_response("ERR\n");
        return;
    }

    /* 校验 checksum */
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 14U; i++) sum += block1[i];
    if ((uint8_t)(sum & 0xFFU) != block1[14] ||
        (uint8_t)((sum >> 8U) & 0xFFU) != block1[15]) {
        send_response("ERR\n");
        return;
    }

    /* 提取信息 */
    uint32_t sid = ((uint32_t)block1[4] << 24U)
                 | ((uint32_t)block1[5] << 16U)
                 | ((uint32_t)block1[6] << 8U)
                 |  (uint32_t)block1[7];

    /* 构造与上位机约定好的格式并返回 */
    char resp[64];
    int n = snprintf(resp, sizeof(resp),
                     "OK:UID=%02X%02X%02X%02X,SID=%lu,TYPE=%u\n",
                     uid[0], uid[1], uid[2], uid[3],
                     (unsigned long)sid, (unsigned int)block1[12]);
    if (n > 0) send_response(resp);
    else       send_response("OK\n");
}

/* -------- CLEAR：销卡（清空所有数据块） -------- */

static void cmd_clear(const char *uid_hex)
{
    uint8_t uid[4];
    uint8_t scanned_uid[4];
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (hex_to_bytes(uid_hex, 8, uid, 4) != 0) {
        send_response("ERR\n");
        return;
    }

    rc522_lock();

    char ret = RC522_ERR;
    /* 增加寻卡重试 */
    for (int retry = 0; retry < 5; retry++) {
        RC522_Halt();
        osDelay(2);
        ret = RC522_ScanCard(scanned_uid);
        if (ret == RC522_OK) break;
    }

    if (ret != RC522_OK) {
        rc522_unlock();
        send_response("ERR\n");
        return;
    }
		
    /* 验证 UID */
    if (memcmp(scanned_uid, uid, 4) != 0) {
        RC522_Halt();
        rc522_unlock();
        send_response("ERR\n");
        return;
    }

    /* ---- 清空所有映射块 ---- */

    /* 扇区 0 块 1（账户头） */
    {
        uint8_t zero[16] = {0};
        ret = RC522_AuthState(RC522_PICC_AUTHENT1A, 3, default_key, uid);
        if (ret == RC522_OK) {
            RC522_Write(1, zero);
        }
    }

    /* 头像区域 */
    ret = clear_blocks_mapped(s_avatar_map, IMG_AVATAR_BLOCKS);

    /* 姓名 + 部门区域 */
    if (ret == 0) ret = clear_blocks_mapped(s_name_map, IMG_NAME_BLOCKS);
    if (ret == 0) ret = clear_blocks_mapped(s_dept_map,  IMG_DEPT_BLOCKS);

    rc522_unlock();
    send_response((ret == 0) ? "OK\n" : "ERR\n");
}

/* ========================================================================== */
/*  串口接收回调（ISR 上下文 —— 轻量，仅做行提取 + 信号量释放）                */
/* ========================================================================== */

static void Serial_RxCallback(UartDrv_RxData_t *pData, void *pUserCtx)
{
    (void)pUserCtx;

    for (uint16_t i = 0; i < pData->rx_len; i++) {
        char c = (char)pData->rx_buf[i];

        if (c == '\n') {
            /* 遇到换行：结束一行 */
            s_line_buf[s_line_len] = '\0';

            /* 仅当上一条指令已被消费时才覆盖（否则丢帧） */
            if (s_cmd_ready == 0) {
                memcpy(s_ready_cmd, s_line_buf, s_line_len + 1U);
                s_cmd_ready = 1;
                osSemaphoreRelease(s_cmdSem);
            }
            s_line_len = 0;
        } else if (c != '\r') {
            /* 普通字符：追加到行缓冲 */
            if (s_line_len < SERIAL_LINE_BUF_SIZE - 1U) {
                s_line_buf[s_line_len++] = c;
            }
        }
        /* '\r' 被静默丢弃 */
    }
}

/* ========================================================================== */
/*  Task_Display（已有，原样保留）                                             */
/*  Task_CardRead（修改：加入互斥量保护）                                      */
/* ========================================================================== */

/* ---- Task_Display 见上文（原样，未修改） ---- */

/* ---- Task_CardRead（修改版） ---- */


/* -------------------------------------------------------------------------- */
/*  Helper: Calculate duration in seconds between two date-times               */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Calculate seconds from last_rec timestamp to now
 * @param  last_rec: previous attendance record (holds entry time)
 * @param  now: current RTC date-time
 * @retval Duration in seconds (handles cross-midnight)
 */
static uint32_t calc_duration_sec(const AttendanceRecord_t *last_rec,
                                   const BSP_RTC_DateTime_t *now)
{
    static const uint8_t month_days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t days1 = 0, days2 = 0;
    uint32_t secs1, secs2;
    uint16_t y;
    uint8_t m;

    /* Calculate days since year 2000 for last_rec timestamp */
    for (y = 2000; y < last_rec->year; y++) {
        days1 += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days1++;
    }
    for (m = 1; m < last_rec->month; m++) {
        days1 += month_days[m - 1];
        if (m == 2 && ((last_rec->year % 4 == 0 && last_rec->year % 100 != 0)
                        || (last_rec->year % 400 == 0))) days1++;
    }
    days1 += last_rec->day - 1;

    /* Calculate days since year 2000 for now */
    for (y = 2000; y < now->year; y++) {
        days2 += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days2++;
    }
    for (m = 1; m < now->month; m++) {
        days2 += month_days[m - 1];
        if (m == 2 && ((now->year % 4 == 0 && now->year % 100 != 0)
                        || (now->year % 400 == 0))) days2++;
    }
    days2 += now->day - 1;

    secs1 = (uint32_t)last_rec->hour * 3600UL
          + (uint32_t)last_rec->minute * 60UL
          + (uint32_t)last_rec->second;
    secs2 = (uint32_t)now->hour * 3600UL
          + (uint32_t)now->minute * 60UL
          + (uint32_t)now->second;

    return (days2 - days1) * 86400UL + (secs2 - secs1);
}

void Task_CardRead(void *argument)
{
    (void)argument;

    uint8_t uid[4];
    uint8_t block_data[16];
//    uint8_t write_buf[16];
    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t i;
    uint16_t sum;
    CardInfo_t card_info;

    /* Initialize RC522 platform */
    RC522_Platform_Init();
    RC522_ConfigISOType('A');

    /*
     * One-time card issuance: write default account header to
     * Sector 0, Block 1 of the first card presented after boot.
     */
//    if (RC522_ScanCard(uid) == RC522_OK) {
//        memset(write_buf, 0, sizeof(write_buf));
//        memcpy(&write_buf[0], uid, 4);
//        write_buf[4]  = 0x01;
//        write_buf[5]  = 0x5F;
//        write_buf[6]  = 0x92;
//        write_buf[7]  = 0xD2;
//        write_buf[12] = CARD_TYPE_NORMAL;

//        sum = 0;
//        for (i = 0; i < 14U; i++) {
//            sum += write_buf[i];
//        }
//        write_buf[14] = (uint8_t)(sum & 0xFFU);
//        write_buf[15] = (uint8_t)((sum >> 8U) & 0xFFU);

//        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
//                            3, default_key, uid) == RC522_OK) {
//            RC522_WriteBlock(0, 1, write_buf);
//        }
//        /*
//         * Write Name[] + Sector[] bitmaps to card sectors 9-15.
//         * Uses same mapped layout as serial UPDATEIMG command for consistency.
//         * Name: 160 bytes packed into 10 blocks via s_name_map
//         * Sector: 160 bytes packed into 10 blocks via s_dept_map
//         * Note: 161st byte of each array (last column of bitmap) is truncated
//         *       to fit the M1 card block layout.
//         */
//        {
//            uint16_t b;

//            /* Pack Name[161] into g_img_name[10][16] */
//            for (b = 0; b < IMG_NAME_BLOCKS; b++) {
//                uint16_t base = b * IMG_BLOCK_SIZE;
//                uint8_t k;
//                for (k = 0; k < IMG_BLOCK_SIZE; k++) {
//                    g_img_name[b][k] = (base + k < 161U) ? Name[base + k] : 0x00;
//                }
//            }

//            /* Pack Sector[161] into g_img_dept[10][16] */
//            for (b = 0; b < IMG_DEPT_BLOCKS; b++) {
//                uint16_t base = b * IMG_BLOCK_SIZE;
//                uint8_t k;
//                for (k = 0; k < IMG_BLOCK_SIZE; k++) {
//                    g_img_dept[b][k] = (base + k < 161U) ? Sector[base + k] : 0x00;
//                }
//            }

//            /* Write using same mapped layout as serial UPDATEIMG command */
//            if (write_blocks_mapped(g_img_name, s_name_map, IMG_NAME_BLOCKS) == 0) {
//                write_blocks_mapped(g_img_dept, s_dept_map, IMG_DEPT_BLOCKS);
//            }
//        }

//        RC522_Halt();
//        RC522_WaitCardOff();
//    }
							
    

    for (;;) {
        char status;

        /* ---- 尝试获取 RC522 互斥量（非阻塞，获取不到就跳过本轮） ---- */
        if (osMutexAcquire(rc522MutexHandle, 0) != osOK) {
            /* RC522 被串口任务占用，跳过本轮寻卡 */
            osDelay(CARD_READ_PERIOD_MS);
            continue;
        }

        /* ---- 寻卡 ---- */
        status = RC522_ScanCard(uid);
        printf("%d\r\n", (uint8_t)status);

        if (status == RC522_OK) {
            /* 填充基本信息 */
            memcpy(card_info.uid, uid, 4);
            BSP_RTC_GetDateTime(&card_info.timestamp);
            card_info.card_type = 0;
            card_info.id_num    = 0;

            /* 读扇区 0 块 1 */
            status = RC522_AuthState(RC522_PICC_AUTHENT1A,
                                     3, default_key, uid);
            if (status == RC522_OK) {
                status = RC522_Read(1, block_data);
                if (status == RC522_OK) {
                    /* 校验 checksum */
                    sum = 0;
                    for (i = 0; i < 14U; i++) {
                        sum += block_data[i];
                    }
                    if ((uint8_t)(sum & 0xFFU) == block_data[14]
                        && (uint8_t)((sum >> 8U) & 0xFFU) == block_data[15]) {
                        card_info.card_type = block_data[12];
                        card_info.id_num =
                            ((uint32_t)block_data[4] << 24U)
                          | ((uint32_t)block_data[5] << 16U)
                          | ((uint32_t)block_data[6] << 8U)
                          |  (uint32_t)block_data[7];
                    }
                }

                /* Read Name + Sector bitmaps from card sectors 9-15 */
                {
                    uint8_t  blk_buf[16];
                    uint8_t  sec = 9, blk = 0;
                    uint16_t pos = 0;

                    while (pos < BMP_TOTAL_BYTES && sec <= 15) {
                        if (blk >= 3U) { blk = 0U; sec++; }
                        if (sec > 15) break;

                        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                                            (uint8_t)(sec * 4U + 3U),
                                            default_key, uid) == RC522_OK) {
                            if (RC522_ReadBlock(sec, blk, blk_buf) == RC522_OK) {
                                uint8_t k;
                                for (k = 0; k < 16U && pos < BMP_TOTAL_BYTES; k++) {
                                    if (pos < 161U) {
                                        card_name_bmp[pos] = blk_buf[k];
                                    } else {
                                        card_sector_bmp[pos - 161U] = blk_buf[k];
                                    }
                                    pos++;
                                }
                            }
                        }
                        blk++;
                    }
                    if (pos > 0) {
                        card_has_bmp = 1;
                    }
                }

                /* Read avatar bitmap from sectors 1-8 if image card */
                if (card_info.card_type == CARD_TYPE_IMAGE) {
                    uint8_t  av_blk_buf[16];
                    uint16_t av_byte_idx = 0;
                    uint8_t  av_last_sec  = 0xFFU;
                    uint8_t  av_map_idx;

                    card_has_avatar = 0;
                    for (av_map_idx = 0; av_map_idx < IMG_AVATAR_BLOCKS; av_map_idx++) {
                        uint8_t av_sec = s_avatar_map[av_map_idx].sec;
                        uint8_t av_blk = s_avatar_map[av_map_idx].blk;

                        if (av_sec != av_last_sec) {
                            if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                                                (uint8_t)(av_sec * 4U + 3U),
                                                default_key, uid) != RC522_OK) {
                                break;
                            }
                            av_last_sec = av_sec;
                        }

                        if (RC522_ReadBlock(av_sec, av_blk, av_blk_buf) != RC522_OK) {
                            break;
                        }

                        for (uint8_t k = 0; k < 16U; k++) {
                            card_avatar_bmp[av_byte_idx++] = av_blk_buf[k];
                        }
                    }

                    if (av_byte_idx == (IMG_AVATAR_BLOCKS * IMG_BLOCK_SIZE)) {
                        card_has_avatar = 1;
                    }
                }
            }

            RC522_Halt();
        }

        /* ---- 释放 RC522 互斥量 ---- */
        osMutexRelease(rc522MutexHandle);

        if (status == RC522_OK) {
            /* ================================================================ */
            /*  Attendance Determination & Record Writing                        */
            /* ================================================================ */
            {
                DeviceConfig_t *cfg = NFC_Storage_GetConfig();

                /* Initialize attendance fields to defaults */
                card_info.att_event    = ATT_EVENT_ENTRY;
                card_info.att_status   = ATT_STATUS_NORMAL;
                card_info.duration_sec = 0;
                card_info.feedback     = FB_EVT_VALID_ENTRY;

                if (card_info.card_type == CARD_TYPE_ADMIN) {
                    /* Admin card: no attendance record, just feedback */
                    card_info.feedback = FB_EVT_ADMIN;
                } else {
                    uint8_t last_event;
                    uint8_t cache_hit;
                    AttendanceRecord_t last_rec;

                    /* Step 1: LRU cache lookup */
                    cache_hit = LRU_Lookup(card_info.uid, &last_event);

                    /* Step 2: Cache miss -> scan Flash for last record */
                    if (!cache_hit) {
                        if (NFC_Storage_FindLastByUID(card_info.uid, &last_rec)) {
                            last_event = last_rec.event;
                            cache_hit = 1;
                        }
                    }

                    /* Step 3: Determine event by attendance mode */
                    if (!cache_hit) {
                        /* First time this card is seen */
                        if (cfg->att_mode == ATT_MODE_EXIT) {
                            card_info.att_event  = ATT_EVENT_EXIT;
                            card_info.att_status = ATT_STATUS_NO_ENTRY;
                            card_info.feedback   = FB_EVT_INVALID;
                        } else {
                            /* Entry or Both mode: treat as entry */
                            card_info.att_event  = ATT_EVENT_ENTRY;
                            card_info.att_status = ATT_STATUS_NORMAL;
                            card_info.feedback   = FB_EVT_VALID_ENTRY;
                        }
                    } else if (cfg->att_mode == ATT_MODE_ENTRY) {
                        /* Entry-only mode */
                        if (last_event == ATT_EVENT_ENTRY) {
                            card_info.att_event  = ATT_EVENT_ENTRY;
                            card_info.att_status = ATT_STATUS_DUP;
                            card_info.feedback   = FB_EVT_DUP;
                        } else {
                            card_info.att_event  = ATT_EVENT_ENTRY;
                            card_info.att_status = ATT_STATUS_NORMAL;
                            card_info.feedback   = FB_EVT_VALID_ENTRY;
                        }
                    } else if (cfg->att_mode == ATT_MODE_EXIT) {
                        /* Exit-only mode */
                        if (last_event == ATT_EVENT_ENTRY) {
                            card_info.att_event    = ATT_EVENT_EXIT;
                            card_info.att_status   = ATT_STATUS_NORMAL;
                            card_info.feedback     = FB_EVT_VALID_EXIT;
                            card_info.duration_sec = calc_duration_sec(&last_rec,
                                                     &card_info.timestamp);
                        } else {
                            card_info.att_event  = ATT_EVENT_EXIT;
                            card_info.att_status = ATT_STATUS_NO_ENTRY;
                            card_info.feedback   = FB_EVT_INVALID;
                        }
                    } else {
                        /* Both mode (ATT_MODE_BOTH): toggle entry/exit */
                        if (last_event == ATT_EVENT_ENTRY) {
                            card_info.att_event    = ATT_EVENT_EXIT;
                            card_info.att_status   = ATT_STATUS_NORMAL;
                            card_info.feedback     = FB_EVT_VALID_EXIT;
                            card_info.duration_sec = calc_duration_sec(&last_rec,
                                                     &card_info.timestamp);
                        } else {
                            card_info.att_event  = ATT_EVENT_ENTRY;
                            card_info.att_status = ATT_STATUS_NORMAL;
                            card_info.feedback   = FB_EVT_VALID_ENTRY;
                        }
                    }

                    /* Step 4: Create and write attendance record to Flash */
                    {
                        AttendanceRecord_t rec;
                        uint32_t total = NFC_Storage_GetTotalRecords();

                        memset(&rec, 0, sizeof(rec));
                        rec.seq        = total + 1;
                        memcpy(rec.uid, card_info.uid, 4);
                        rec.id_num     = card_info.id_num;
                        rec.card_type  = card_info.card_type;
                        rec.year       = card_info.timestamp.year;
                        rec.month      = card_info.timestamp.month;
                        rec.day        = card_info.timestamp.day;
                        rec.hour       = card_info.timestamp.hour;
                        rec.minute     = card_info.timestamp.minute;
                        rec.second     = card_info.timestamp.second;
                        rec.event      = card_info.att_event;
                        rec.status     = card_info.att_status;
                        rec.dev_id     = cfg->dev_id;
                        rec.time_offset = cfg->time_offset;
                        rec.duration   = card_info.duration_sec;

                        NFC_Storage_AddRecord(&rec);
                    }

                    /* Step 5: Update LRU cache */
                    LRU_Update(card_info.uid, card_info.att_event);
                }
            }

            /* Send to display task */
            osMessageQueuePut(cardQueueHandle, &card_info, 0, 0);

            /* Beep feedback */
            MIDI_Beep(9U, 80U);  /* C5 tone, 80ms */
						char resp[64];
						
            snprintf(resp, sizeof(resp),
                     "OK:UID=%02X%02X%02X%02X,SID=%lu,TYPE=%u\n",
                     card_info.uid[0], card_info.uid[1], card_info.uid[2], card_info.uid[3],
                     (unsigned long)card_info.id_num, (unsigned int)card_info.card_type);
						
            UartDrv_SendStr(&g_serialDrv, resp);
            /* 等待卡离开（不持互斥量） */
            uint8_t tagType[2];
            uint8_t fail_cnt = 0;
            
            while (fail_cnt < 3) {
                /* 1. 安全获取锁，查询卡片是否还在 */
                osMutexAcquire(rc522MutexHandle, osWaitForever);
                if (RC522_Request(RC522_PICC_REQALL, tagType) != RC522_OK) {
                    fail_cnt++; /* 未检测到卡片，增加计数 */
                } else {
                    fail_cnt = 0; /* 卡片还在，重置离卡计数 */
                }
                /* 2. 检测完毕立即释放锁，此时上位机的 Task_Serial 可以趁机抢到锁进行发卡 */
                osMutexRelease(rc522MutexHandle);

                /* 3. 调用 RTOS 的系统延时，不阻塞 CPU */
                if (fail_cnt < 3) {
                    osDelay(200);
								}
						}

            /* Send card-removed signal to display task */
            {
                CardInfo_t removed_info;
                memset(&removed_info, 0, sizeof(removed_info));
                osMessageQueuePut(cardQueueHandle, &removed_info, 0, 0);
            }
        }

        osDelay(CARD_READ_PERIOD_MS);
    }
}

/* ========================================================================== */
/*  Task_Serial：串口指令处理任务                                              */
/* ========================================================================== */

void Task_Serial(void *argument)
{
    (void)argument;

    char cmd[SERIAL_LINE_BUF_SIZE];

    for (;;) {
        /* 等待指令就绪信号量 */
        osSemaphoreAcquire(s_cmdSem, osWaitForever);

        /* 将指令从 ISR 共享缓冲复制到本地栈 */
        taskENTER_CRITICAL();
        strcpy(cmd, s_ready_cmd);
        s_cmd_ready = 0;
        taskEXIT_CRITICAL();

        /* ---- 指令分发 ---- */
        if (strncmp(cmd, "ISSUE:", 6) == 0) {
            cmd_issue(cmd + 6);

        } else if (strncmp(cmd, "IMGA", 4) == 0) {
            cmd_img_cache('A', cmd + 4);

        } else if (strncmp(cmd, "IMGN", 4) == 0) {
            cmd_img_cache('N', cmd + 4);

        } else if (strncmp(cmd, "IMGD", 4) == 0) {
            cmd_img_cache('D', cmd + 4);

        } else if (strcmp(cmd, "UPDATEIMG") == 0) {
            cmd_update_img();

        } else if (strcmp(cmd, "READ") == 0) {
            cmd_read();

        } else if (strncmp(cmd, "CLEAR:", 6) == 0) {
            cmd_clear(cmd + 6);

        } else if (strncmp(cmd, "LIST:", 5) == 0) {
            /* LIST command: LIST:ALL or LIST:N (return last N records) */
            {
                uint32_t total = NFC_Storage_GetTotalRecords();
                uint32_t count;
                uint32_t start_idx;
                uint32_t i;
                AttendanceRecord_t rec;
                char line[128];

                if (strcmp(cmd + 5, "ALL") == 0) {
                    count = total;
                } else {
                    count = 0;
                    const char *p = cmd + 5;
                    while (*p >= '0' && *p <= '9') {
                        count = count * 10U + (uint32_t)(*p - '0');
                        p++;
                    }
                    if (count > total) count = total;
                }

                if (count > total) count = total;
                if (count > 0) start_idx = total - count;
                else start_idx = 0;

                /* Send total count header */
                snprintf(line, sizeof(line), "NR=%lu\n", (unsigned long)count);
                send_response(line);

                for (i = 0; i < count; i++) {
                    if (NFC_Storage_GetRecord(start_idx + i, &rec)) {
                        snprintf(line, sizeof(line),
                            "SEQ=%lu,UID=%02X%02X%02X%02X,SID=%lu,"
                            "EVT=%c,STS=%c,DT=%04u-%02u-%02u %02u:%02u:%02u,"
                            "DUR=%lu\n",
                            (unsigned long)rec.seq,
                            rec.uid[0], rec.uid[1], rec.uid[2], rec.uid[3],
                            (unsigned long)rec.id_num,
                            (rec.event == ATT_EVENT_ENTRY) ? 'E' : 'X',
                            (rec.status == ATT_STATUS_NORMAL) ? 'N' :
                            (rec.status == ATT_STATUS_DUP) ? 'D' :
                            (rec.status == ATT_STATUS_NO_ENTRY) ? 'E' : 'U',
                            rec.year, rec.month, rec.day,
                            rec.hour, rec.minute, rec.second,
                            (unsigned long)rec.duration);
                        send_response(line);
                    }
                }
                send_response("OK\n");
            }

        } else {
            /* Unknown command */
            send_response("ERR\n");
        }
    }
}

/* ========================================================================== */
/*  Serial_Cmd_Init：串口指令接口初始化（在 main.c 中调用）                    */
/* ========================================================================== */

void Serial_Cmd_Init(void)
{
    /* 初始化 UART 驱动实例（绑定 USART1） */
    UartDrv_Init(&g_serialDrv, &huart1);

    /* 注册接收回调（ISR 中做行提取） */
    UartDrv_RegisterRxCb(&g_serialDrv, Serial_RxCallback, NULL);

    /* 启动空闲中断接收 */
    UartDrv_StartRecv(&g_serialDrv);
}

/* ========================================================================== */
/*  Phase 3: Network Communication                                             */
/* ========================================================================== */

/* ---- Network globals ---- */

/** UART driver instance for ESP01S (USART6) */
static UartDrv_t g_espDrv;

/** Weather info cache, written by Task_Network, read by Task_Display */
WeatherInfo_t g_weather;

/** Network status: written by Task_Network, read by Task_Display */
volatile NetStatus_t g_net_status = NET_STAT_OFFLINE;

/** Pending upload record count: written by Task_Network, read by Task_Display */
volatile uint32_t g_net_pending = 0;

/** Upload ACK flag: set to 1 by RX callback when "OK\n" received */
static volatile uint8_t g_upload_ack = 0;

/* ---- Network RX callback (ISR context) ---- */

/**
 * @brief  TCP data receive callback for upload ACK detection
 * @note   Called from UART ISR context, sets flag only, returns quickly
 */
static void Network_RxCallback(const uint8_t *pData, uint16_t len, void *pUserCtx)
{
    (void)pUserCtx;
    if (len >= 2 && pData[0] == 'O' && pData[1] == 'K') {
        g_upload_ack = 1;
    }
}

/* ---- Upload helpers ---- */

/**
 * @brief  Upload all pending attendance records to server via transparent mode
 * @note   Sends one record at a time, waits for "OK\n" ACK before next.
 *         Stops on first failure or when all records are synced.
 */
static void upload_pending_records(void)
{
    AttendanceRecord_t rec;
    char line[160];
    uint32_t upload_off;
    uint32_t write_off;
    uint32_t timeout;

    upload_off = NFC_Storage_GetUploadOffset();
    write_off  = NFC_Storage_GetWriteOffset();

    while (upload_off != write_off) {
        /* Read record at current upload offset */
        if (!NFC_Storage_GetRecordAtOffset(upload_off, &rec)) {
            break;
        }

        /* Build "ATT:..." upload line */
        snprintf(line, sizeof(line),
                 "ATT:SEQ=%lu,UID=%02X%02X%02X%02X,SID=%lu,"
                 "EVT=%c,STS=%c,DT=%04u-%02u-%02u %02u:%02u:%02u,"
                 "DUR=%lu,DEV=%u,OFS=%ld\n",
                 (unsigned long)rec.seq,
                 rec.uid[0], rec.uid[1], rec.uid[2], rec.uid[3],
                 (unsigned long)rec.id_num,
                 (rec.event == ATT_EVENT_ENTRY) ? 'E' : 'X',
                 (rec.status == ATT_STATUS_NORMAL) ? 'N' :
                 (rec.status == ATT_STATUS_DUP) ? 'D' :
                 (rec.status == ATT_STATUS_NO_ENTRY) ? 'E' : 'U',
                 rec.year, rec.month, rec.day,
                 rec.hour, rec.minute, rec.second,
                 (unsigned long)rec.duration,
                 (unsigned int)rec.dev_id,
                 (long)rec.time_offset);

        /* Send record, wait for ACK */
        g_upload_ack = 0;
        ESP01S_SendStr(line);

        timeout = UPLOAD_ACK_TIMEOUT_MS / 100U;
        while (timeout > 0 && g_upload_ack == 0) {
            osDelay(100);
            timeout--;
        }

        if (g_upload_ack) {
            /* ACK received, advance upload offset */
            NFC_Storage_AdvanceUploadOffset();
            upload_off = NFC_Storage_GetUploadOffset();
        } else {
            /* Timeout, retry next cycle */
            break;
        }
    }
}

/* ---- Weather helper ---- */

/**
 * @brief  Query weather once from Seniverse API
 * @note   Updates g_weather on success, clears valid flag on failure.
 *         ESP01S_QueryWeather handles transparent exit/restore internally.
 */
static void query_weather_once(void)
{
    int ret;

    ret = ESP01S_QueryWeather(WEATHER_API_KEY, WEATHER_LOCATION,
                              WEATHER_LANGUAGE, WEATHER_UNIT,
                              g_weather.city, sizeof(g_weather.city),
                              g_weather.textDay, sizeof(g_weather.textDay),
                              g_weather.high, sizeof(g_weather.high),
                              g_weather.textNight, sizeof(g_weather.textNight),
                              g_weather.low, sizeof(g_weather.low),
                              g_weather.precip, sizeof(g_weather.precip));

    if (ret == 0) {
        g_weather.valid = 1;
        g_weather.queryTick = HAL_GetTick();
    } else {
        g_weather.valid = 0;
    }
}

/* ---- Heartbeat helper ---- */

/**
 * @brief  Send heartbeat frame to server
 * @note   Format: HB:DEV=N,TS=YYYY-MM-DD HH:MM:SS,REC=N\n
 */
static void send_heartbeat(void)
{
    BSP_RTC_DateTime_t dt;
    char line[96];
    uint32_t pending;

    BSP_RTC_GetDateTime(&dt);

    {
        uint32_t upload_off = NFC_Storage_GetUploadOffset();
        uint32_t write_off  = NFC_Storage_GetWriteOffset();
        uint32_t total = NFC_Storage_GetTotalRecords();
        /* Estimate pending: records not yet uploaded */
        if (total > 0) {
            /* Rough estimate based on byte offsets */
            if (write_off >= upload_off) {
                pending = (write_off - upload_off) / STORAGE_RECORD_SIZE;
            } else {
                pending = ((STORAGE_DATA_TOTAL_BYTES - upload_off) + write_off)
                          / STORAGE_RECORD_SIZE;
            }
        } else {
            pending = 0;
        }

        snprintf(line, sizeof(line),
                 "HB:DEV=%u,TS=%04u-%02u-%02u %02u:%02u:%02u,REC=%lu,PEND=%lu\n",
                 (unsigned int)NFC_Storage_GetConfig()->dev_id,
                 dt.year, dt.month, dt.day,
                 dt.hour, dt.minute, dt.second,
                 (unsigned long)total, (unsigned long)pending);
    }

    ESP01S_SendStr(line);
}

/* ---- Time offset helper ---- */

/**
 * @brief  Update time_offset in device config after NTP sync
 * @note   Calculates offset = server_time - local_time and saves to Flash
 */
static void update_time_offset(void)
{
    DeviceConfig_t *cfg = NFC_Storage_GetConfig();
    DeviceConfig_t new_cfg;
    BSP_RTC_DateTime_t dt;
    int32_t ntp_ts;
    int32_t local_ts;

    /* Read current local RTC time */
    BSP_RTC_GetDateTime(&dt);

    /* Convert local RTC to Unix timestamp using day-count formula */
    {
        uint16_t y = dt.year;
        uint8_t  m = dt.month;
        uint8_t  d = dt.day;
        int32_t days;

        /* Days from 1970 to given date (approximate) */
        if (m <= 2) { y--; m += 12; }
        days = (int32_t)(365 * (int32_t)y + (int32_t)(y / 4) - (int32_t)(y / 100)
               + (int32_t)(y / 400) + (int32_t)((153 * (int32_t)m + 8) / 5)
               + (int32_t)d - 719469);

        local_ts = days * 86400L
                 + (int32_t)dt.hour * 3600L
                 + (int32_t)dt.minute * 60L
                 + (int32_t)dt.second;
    }

    /* Get NTP time as Unix timestamp via ESP01S_GetDateTime */
    {
        char ntp_buf[24];
        if (ESP01S_GetDateTime(NULL, DT_ALL, ntp_buf, sizeof(ntp_buf)) == 0) {
            int ny, nm, nd, nh, nmin, ns;
            if (sscanf(ntp_buf, "%d-%d-%d %d:%d:%d",
                       &ny, &nm, &nd, &nh, &nmin, &ns) == 6) {
                int32_t ndays;
                uint16_t ny_u = (uint16_t)ny;
                uint8_t  nm_u = (uint8_t)nm;
                uint8_t  nd_u = (uint8_t)nd;
                if (nm_u <= 2) { ny_u--; nm_u += 12; }
                ndays = (int32_t)(365 * (int32_t)ny_u + (int32_t)(ny_u / 4)
                       - (int32_t)(ny_u / 100) + (int32_t)(ny_u / 400)
                       + (int32_t)((153 * (int32_t)nm_u + 8) / 5)
                       + (int32_t)nd_u - 719469);
                ntp_ts = ndays * 86400L
                       + (int32_t)nh * 3600L
                       + (int32_t)nmin * 60L
                       + (int32_t)ns;

                cfg->time_offset = ntp_ts - local_ts;
            }
        }
    }

    /* Save config with updated time_offset */
    memcpy(&new_cfg, cfg, sizeof(DeviceConfig_t));
    new_cfg.checksum = 0;
    {
        const uint8_t *p = (const uint8_t *)&new_cfg;
        uint16_t sum = 0;
        uint8_t i;
        for (i = 0; i < 14U; i++) {
            sum += (uint16_t)p[i];
        }
        new_cfg.checksum = sum;
    }
    NFC_Storage_SaveConfig(&new_cfg);
}

/* ---- Network Task ---- */

/**
 * @brief  Network communication task: WiFi+NTP+TCP upload+weather+heartbeat
 * @note   Handles full ESP01S lifecycle. Blocking ESP01S_Start() calls
 *         are safe here because this is a low-priority dedicated task.
 */
void Task_Network(void *argument)
{
    int ret;
    uint32_t reconnect_timer = 0;
    uint32_t weather_timer   = 0;
    uint32_t heartbeat_timer = 0;

    (void)argument;

    g_net_status = NET_STAT_OFFLINE;
    g_net_pending = 0;

    /* ---- One-time hardware init ---- */
    UartDrv_Init(&g_espDrv, &huart6);
    ESP01S_Init(&g_espDrv);

    /* Configure WiFi, TCP server, NTP */
    ESP01S_SetWiFi(WIFI_SSID, WIFI_PASSWORD);
    ESP01S_SetTcpServer(TCP_SERVER_IP, TCP_SERVER_PORT);
    ESP01S_SetNtpServer(NTP_SERVER, NTP_TIMEZONE);

    /* Register TCP data callback for upload ACK detection */
    ESP01S_RegisterDataCb(Network_RxCallback, NULL);

    /* Initial connection (blocking ~15s) */
    ret = ESP01S_Start();
    if (ret == 0) {
        /* NTP time sync -> RTC */
        ESP01S_SetRtcFromNtp(&hrtc);
        update_time_offset();
    }

    /* ---- Main loop (1s period) ---- */
    for (;;) {
        ESP01S_State_t st = ESP01S_GetState();

        if (st == ESP01S_STATE_TRANSPARENT) {
            /* Connected and in transparent mode — do work */
            g_net_status = NET_STAT_ONLINE;

            /* Calculate pending count for display */
            {
                uint32_t up_off = NFC_Storage_GetUploadOffset();
                uint32_t wr_off = NFC_Storage_GetWriteOffset();
                if (wr_off >= up_off) {
                    g_net_pending = (wr_off - up_off) / STORAGE_RECORD_SIZE;
                } else {
                    g_net_pending = ((STORAGE_DATA_TOTAL_BYTES - up_off) + wr_off)
                                    / STORAGE_RECORD_SIZE;
                }
            }

            /* Upload pending attendance records */
            if (g_net_pending > 0) {
                g_net_status = NET_STAT_SYNCING;
            }
            upload_pending_records();

            /* Re-check pending after upload */
            {
                uint32_t up_off = NFC_Storage_GetUploadOffset();
                uint32_t wr_off = NFC_Storage_GetWriteOffset();
                if (wr_off >= up_off) {
                    g_net_pending = (wr_off - up_off) / STORAGE_RECORD_SIZE;
                } else {
                    g_net_pending = ((STORAGE_DATA_TOTAL_BYTES - up_off) + wr_off)
                                    / STORAGE_RECORD_SIZE;
                }
            }
            g_net_status = (g_net_pending == 0) ? NET_STAT_ONLINE : NET_STAT_SYNCING;

            /* Heartbeat (every 60s) */
            if (HAL_GetTick() - heartbeat_timer >= HEARTBEAT_INTERVAL_MS) {
                send_heartbeat();
                heartbeat_timer = HAL_GetTick();
            }

            /* Weather query (every 30 min) */
            if (HAL_GetTick() - weather_timer >= WEATHER_INTERVAL_MS) {
                query_weather_once();
                weather_timer = HAL_GetTick();
            }
        } else {
            /* Disconnected or connecting — attempt reconnect */
            g_net_status = NET_STAT_OFFLINE;
            g_net_pending = 0;
            reconnect_timer++;
            if (reconnect_timer >= RECONNECT_INTERVAL_SEC) {
                ret = ESP01S_Start();
                if (ret == 0) {
                    ESP01S_SetRtcFromNtp(&hrtc);
                    update_time_offset();
                }
                reconnect_timer = 0;
            }
        }

        osDelay(NETWORK_PERIOD_MS);
    }
}

/* ========================================================================== */
/*  Task_KeyScan: Key scanning task (IPC producer)                             */
/* ========================================================================== */

/** Message queue for passing KeyMsg_t from KeyScan to Display task */
osMessageQueueId_t keyQueueHandle;

void Task_KeyScan(void *argument)
{
    (void)argument;

    uint8_t  prev_mode = 0, prev_up = 0, prev_down = 0, prev_save = 0;
    uint32_t up_press_tick   = 0, down_press_tick   = 0;
    uint32_t up_repeat_tick  = 0, down_repeat_tick  = 0;
    KeyMsg_t msg;

    for (;;) {
        uint32_t now = osKernelGetTickCount();

        /* Poll all four keys (active-low with pull-up) */
        uint8_t key_mode = KEY_IS_PRESSED(KEY_MODE_PORT, KEY_MODE_PIN);
        uint8_t key_up   = KEY_IS_PRESSED(KEY_UP_PORT,   KEY_UP_PIN);
        uint8_t key_down = KEY_IS_PRESSED(KEY_DOWN_PORT, KEY_DOWN_PIN);
        uint8_t key_save = KEY_IS_PRESSED(KEY_SAVE_PORT, KEY_SAVE_PIN);

        /* ---- MODE key: short press on falling edge only ---- */
        if (key_mode && !prev_mode) {
            msg.key_id  = KEY_ID_MODE;
            msg.evt_type = KEY_EVT_SHORT;
            osMessageQueuePut(keyQueueHandle, &msg, 0, 0);
        }

        /* ---- SAVE key: short press on falling edge only ---- */
        if (key_save && !prev_save) {
            msg.key_id  = KEY_ID_SAVE;
            msg.evt_type = KEY_EVT_SHORT;
            osMessageQueuePut(keyQueueHandle, &msg, 0, 0);
        }

        /* ---- UP key: short press on falling edge + long-press auto-repeat ---- */
        if (key_up && !prev_up) {
            msg.key_id  = KEY_ID_UP;
            msg.evt_type = KEY_EVT_SHORT;
            osMessageQueuePut(keyQueueHandle, &msg, 0, 0);
            up_press_tick  = now;
            up_repeat_tick = now;
        }
        if (key_up && (now - up_press_tick >= KEY_LONG_PRESS_MS)
            && (now - up_repeat_tick >= KEY_REPEAT_MS)) {
            msg.key_id  = KEY_ID_UP;
            msg.evt_type = KEY_EVT_LONG;
            osMessageQueuePut(keyQueueHandle, &msg, 0, 0);
            up_repeat_tick = now;
        }

        /* ---- DOWN key: short press on falling edge + long-press auto-repeat ---- */
        if (key_down && !prev_down) {
            msg.key_id  = KEY_ID_DOWN;
            msg.evt_type = KEY_EVT_SHORT;
            osMessageQueuePut(keyQueueHandle, &msg, 0, 0);
            down_press_tick  = now;
            down_repeat_tick = now;
        }
        if (key_down && (now - down_press_tick >= KEY_LONG_PRESS_MS)
            && (now - down_repeat_tick >= KEY_REPEAT_MS)) {
            msg.key_id  = KEY_ID_DOWN;
            msg.evt_type = KEY_EVT_LONG;
            osMessageQueuePut(keyQueueHandle, &msg, 0, 0);
            down_repeat_tick = now;
        }

        /* Save previous states for edge detection */
        prev_mode = key_mode;
        prev_up   = key_up;
        prev_down = key_down;
        prev_save = key_save;

        osDelay(20U); /* 50 Hz scan rate */
    }
}


/* -------------------------------------------------------------------------- */
/*  Helper: Blink-field String Builder                                        */
/* -------------------------------------------------------------------------- */
/**
 * @brief  Replace the selected date/time field with spaces when blink is off
 * @param  buf:      string buffer to modify in-place (e.g. "2026-07-01")
 * @param  blink_on: 1 = show field, 0 = hide field
 * @param  field_id: which field is selected (FIELD_YEAR .. FIELD_SECOND)
 */
static void ApplyBlink(char *buf, uint8_t blink_on, uint8_t field_id)
{
    struct {
        uint8_t id;
        uint8_t pos;
        uint8_t width;
    } fields[] = {
        {FIELD_YEAR,   0, 4},   /* "YYYY"-MM-DD   */
        {FIELD_MONTH,  5, 2},   /* YYYY-"MM"-DD   */
        {FIELD_DAY,    8, 2},   /* YYYY-MM-"DD"   */
        {FIELD_HOUR,   0, 2},   /* "HH":MM:SS     */
        {FIELD_MINUTE, 3, 2},   /* HH:"MM":SS     */
        {FIELD_SECOND, 6, 2},   /* HH:MM:"SS"     */
    };
    const uint8_t field_count = sizeof(fields) / sizeof(fields[0]);
    uint8_t i;

    if (blink_on) return;

    for (i = 0; i < field_count; i++) {
        if (fields[i].id == field_id) {
            memset(buf + fields[i].pos, ' ', fields[i].width);
            return;
        }
    }
}

/* -------------------------------------------------------------------------- */
/*  Helper: Clamp Day to Valid Range                                          */
/* -------------------------------------------------------------------------- */
static uint8_t ClampDay(uint8_t day, uint16_t year, uint8_t month)
{
    uint8_t max_day = BSP_RTC_DaysInMonth(year, month);
    if (day > max_day) day = max_day;
    if (day < 1U) day = 1U;
    return day;
}

/* -------------------------------------------------------------------------- */
/*  Task: Display (Splash + Clock + Settings + Card UI)                       */
/* -------------------------------------------------------------------------- */
void Task_Display(void *argument)
{
    (void)argument;

    /* Wait for SSD1306 power-on reset (~150ms needed). */
    osDelay(200);

    /* Initialize OLED, DS18B20 temperature sensor */
    OLED_Init();
    ds18b20_init();

    /* Turn off all LEDs initially */
    LED_OFF(LED1_GPIO_Port, LED1_Pin);
    LED_OFF(LED2_GPIO_Port, LED2_Pin);
    LED_OFF(LED3_GPIO_Port, LED3_Pin);
    LED_OFF(LED4_GPIO_Port, LED4_Pin);
    LED_OFF(LED5_GPIO_Port, LED5_Pin);
    LED_OFF(LED6_GPIO_Port, LED6_Pin);
    LED_OFF(LED7_GPIO_Port, LED7_Pin);

    /* ---- Splash mode state ---- */
    DispMode_t mode = DISP_MODE_SPLASH;
    uint32_t splash_deadline = osKernelGetTickCount() + SPLASH_DURATION_MS;

    /* ---- Clock / card state ---- */
	BSP_RTC_DateTime_t dt = {
		.year = 2026, .month = 6, .day = 20,
		.hour = 14, .minute = 30, .second = 0
	};
	if (BSP_RTC_IsFirstPowerOn()) {
    BSP_RTC_MarkInitialized();

	}

    CardInfo_t card_info;
    float    temperature = 0.0f;
    uint32_t last_temp_read = 0;

    /* ---- Setting mode state ---- */
    SetField_t field = FIELD_YEAR;
    uint8_t  blink_on       = 1;
    uint32_t last_blink_tick = 0;

    /* ---- Card display tracking (avatar delay for image cards) ---- */
    uint32_t card_enter_tick    = 0;
    uint8_t  card_disp_is_image = 0;

    /* ---- Attendance result tracking for card display page ---- */
    uint8_t  card_att_event     = ATT_EVENT_ENTRY;
    uint8_t  card_att_status    = ATT_STATUS_NORMAL;
    uint32_t card_duration_sec  = 0;

    /* ---- Admin mode state ---- */
    AdminField_t admin_field   = ADMIN_FIELD_DEV_ID;
    uint8_t      admin_blink   = 1;
    uint32_t     admin_enter_tick = 0;
    uint16_t     admin_dev_id;
    uint8_t      admin_att_mode;

    /* ---- LED/Buzzer feedback state machine ---- */
    FeedbackState_t fb_state;
    memset(&fb_state, 0, sizeof(fb_state));

    /* ---- Key event from Task_KeyScan via queue ---- */
    KeyMsg_t key_msg;

    /* Display buffers */
    char line_buf[24];
    const char *weekdays[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *field_names[] = {"YEAR","MONTH","DAY","HOUR","MIN","SEC"};

    for (;;) {
        uint32_t now = osKernelGetTickCount();

        /* Service MIDI music sequencer (non-blocking, returns immediately if idle) */
        MIDI_Tick();

        /* ---- Process key events from Task_KeyScan via IPC queue ---- */
        while (osMessageQueueGet(keyQueueHandle, &key_msg, NULL, 0) == osOK) {
            if (key_msg.key_id == KEY_ID_MODE) {
                /* MODE key: enter setting / cycle field / exit card mode */
                if (mode == DISP_MODE_CLOCK) {
                    BSP_RTC_GetDateTime(&dt);
                    mode  = DISP_MODE_SETTING;
                    field = FIELD_YEAR;
                    blink_on = 1;
                    last_blink_tick = now;
                } else if (mode == DISP_MODE_SETTING) {
                    field = (SetField_t)((uint8_t)field + 1U);
                    if (field >= FIELD_COUNT) {
                        dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                        BSP_RTC_SetDateTime(&dt);
                        BSP_RTC_MarkInitialized();
                        mode = DISP_MODE_CLOCK;
                    }
                    blink_on = 1;
                    last_blink_tick = now;
                } else if (mode == DISP_MODE_CARD) {
                    mode = DISP_MODE_CLOCK;
                    LED_OFF(LED1_GPIO_Port, LED1_Pin);
                    LED_OFF(LED2_GPIO_Port, LED2_Pin);
                    LED_OFF(LED3_GPIO_Port, LED3_Pin);
                    LED_OFF(LED4_GPIO_Port, LED4_Pin);
                    LED_OFF(LED5_GPIO_Port, LED5_Pin);
                    LED_OFF(LED6_GPIO_Port, LED6_Pin);
                    LED_OFF(LED7_GPIO_Port, LED7_Pin);
                    fb_state.active = 0;
                } else if (mode == DISP_MODE_ADMIN_INFO) {
                    /* Skip to admin setting */
                    mode = DISP_MODE_ADMIN_SETTING;
                    admin_blink = 1;
                    /* admin blink tick updated */
                } else if (mode == DISP_MODE_ADMIN_SETTING) {
                    /* Cycle admin field */
                    admin_field = (AdminField_t)((uint8_t)admin_field + 1U);
                    if (admin_field >= ADMIN_FIELD_COUNT) {
                        admin_field = ADMIN_FIELD_DEV_ID;
                    }
                    admin_blink = 1;
                    /* admin blink tick updated */
                }
            } else if (key_msg.key_id == KEY_ID_SAVE) {
                /* SAVE key: save & exit setting / admin mode */
                if (mode == DISP_MODE_SETTING) {
                    dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                    BSP_RTC_SetDateTime(&dt);
                    BSP_RTC_MarkInitialized();
                    mode = DISP_MODE_CLOCK;
                } else if (mode == DISP_MODE_ADMIN_SETTING) {
                    /* Save config to Flash */
                    DeviceConfig_t save_cfg;
                    memset(&save_cfg, 0, sizeof(save_cfg));
                    save_cfg.magic[0] = 'N';
                    save_cfg.magic[1] = 'F';
                    save_cfg.magic[2] = 'C';
                    save_cfg.magic[3] = 'A';
                    save_cfg.dev_id      = admin_dev_id;
                    save_cfg.att_mode    = admin_att_mode;
                    save_cfg.time_offset = 0;
                    NFC_Storage_SaveConfig(&save_cfg);
                    /* Clear LRU on mode change */
                    LRU_Clear();
                    mode = DISP_MODE_CLOCK;
                    LED_OFF(LED1_GPIO_Port, LED1_Pin);
                    LED_OFF(LED2_GPIO_Port, LED2_Pin);
                    LED_OFF(LED3_GPIO_Port, LED3_Pin);
                    LED_OFF(LED4_GPIO_Port, LED4_Pin);
                    LED_OFF(LED5_GPIO_Port, LED5_Pin);
                    LED_OFF(LED6_GPIO_Port, LED6_Pin);
                    LED_OFF(LED7_GPIO_Port, LED7_Pin);
                    fb_state.active = 0;
                }
            } else if (key_msg.key_id == KEY_ID_UP) {
                /* UP key: increment selected field (short + long press) */
                if (mode == DISP_MODE_SETTING) {
                    switch (field) {
                    case FIELD_YEAR:
                        if (dt.year < 2099U) dt.year++; break;
                    case FIELD_MONTH:
                        dt.month = (dt.month % 12U) + 1U;
                        dt.day = ClampDay(dt.day, dt.year, dt.month);
                        break;
                    case FIELD_DAY:
                        dt.day = ClampDay(dt.day + 1U, dt.year, dt.month);
                        break;
                    case FIELD_HOUR:
                        dt.hour = (dt.hour + 1U) % 24U; break;
                    case FIELD_MINUTE:
                        dt.minute = (dt.minute + 1U) % 60U; break;
                    case FIELD_SECOND:
                        dt.second = (dt.second + 1U) % 60U; break;
                    default: break;
                    }
                } else if (mode == DISP_MODE_ADMIN_SETTING) {
                    if (admin_field == ADMIN_FIELD_DEV_ID) {
                        if (admin_dev_id < 65535U) admin_dev_id++;
                    } else {
                        admin_att_mode = (admin_att_mode + 1U) % 3U;
                    }
                }
            } else if (key_msg.key_id == KEY_ID_DOWN) {
                /* DOWN key: decrement selected field (short + long press) */
                if (mode == DISP_MODE_SETTING) {
                    switch (field) {
                    case FIELD_YEAR:
                        if (dt.year) dt.year--; break;
                    case FIELD_MONTH:
                        dt.month = (dt.month == 1U) ? 12U : dt.month - 1U;
                        dt.day = ClampDay(dt.day, dt.year, dt.month);
                        break;
                    case FIELD_DAY:
                        dt.day = ClampDay(dt.day - 1U, dt.year, dt.month);
                        break;
                    case FIELD_HOUR:
                        dt.hour = (dt.hour == 0U) ? 23U : dt.hour - 1U; break;
                    case FIELD_MINUTE:
                        dt.minute = (dt.minute == 0U) ? 59U : dt.minute - 1U; break;
                    case FIELD_SECOND:
                        dt.second = (dt.second == 0U) ? 59U : dt.second - 1U; break;
                    default: break;
                    }
                } else if (mode == DISP_MODE_ADMIN_SETTING) {
                    if (admin_field == ADMIN_FIELD_DEV_ID) {
                        if (admin_dev_id > 1U) admin_dev_id--;
                    } else {
                        admin_att_mode = (admin_att_mode == 0U) ? 2U : admin_att_mode - 1U;
                    }
                }
            }
        }

        /* ---- Read RTC (skip in setting mode to preserve edits) ---- */
        if (mode != DISP_MODE_SETTING) {
            BSP_RTC_GetDateTime(&dt);
        }

        /* ---- Read temperature (only in clock mode, every 2 sec) ---- */
        if (mode == DISP_MODE_CLOCK
            && (now - last_temp_read >= TEMP_READ_PERIOD_MS)) {
            temperature = ds18b20_read();
            last_temp_read = now;
        }

        /* ---- Splash -> Clock transition ---- */
        if (mode == DISP_MODE_SPLASH) {
            if ((int32_t)(now - splash_deadline) >= 0) {
                mode = DISP_MODE_CLOCK;
            }
        }

        /* ---- Blink timer for setting mode + admin setting cursor ---- */
        if (now - last_blink_tick >= BLINK_PERIOD_MS) {
            blink_on = !blink_on;
            last_blink_tick = now;
            if (mode == DISP_MODE_ADMIN_SETTING) {
                admin_blink = !admin_blink;
            }
        }

        /* ---- Admin mode 120s timeout ---- */
        if ((mode == DISP_MODE_ADMIN_INFO || mode == DISP_MODE_ADMIN_SETTING)
            && (int32_t)(now - admin_enter_tick) >= 120000) {
            mode = DISP_MODE_CLOCK;
            LED_OFF(LED1_GPIO_Port, LED1_Pin);
            LED_OFF(LED2_GPIO_Port, LED2_Pin);
            LED_OFF(LED3_GPIO_Port, LED3_Pin);
            LED_OFF(LED4_GPIO_Port, LED4_Pin);
            LED_OFF(LED5_GPIO_Port, LED5_Pin);
            LED_OFF(LED6_GPIO_Port, LED6_Pin);
            LED_OFF(LED7_GPIO_Port, LED7_Pin);
            fb_state.active = 0;
        }

        /* ---- Admin info -> Admin setting auto transition (2s) ---- */
        if (mode == DISP_MODE_ADMIN_INFO
            && (int32_t)(now - admin_enter_tick) >= 2000) {
            mode = DISP_MODE_ADMIN_SETTING;
            admin_blink = 1;
            /* admin blink tick updated */
        }

        /* ---- Check card queue (non-blocking) ---- */
        if (osMessageQueueGet(cardQueueHandle, &card_info, NULL, 0) == osOK) {
            /* Check for card-removed signal (all-zero UID means card left) */
            if (card_info.uid[0] == 0U && card_info.uid[1] == 0U &&
                card_info.uid[2] == 0U && card_info.uid[3] == 0U) {
                if (mode == DISP_MODE_CARD) {
                    mode = DISP_MODE_CLOCK;
                    LED_OFF(LED1_GPIO_Port, LED1_Pin);
                    LED_OFF(LED2_GPIO_Port, LED2_Pin);
                    LED_OFF(LED3_GPIO_Port, LED3_Pin);
                    LED_OFF(LED4_GPIO_Port, LED4_Pin);
                    LED_OFF(LED5_GPIO_Port, LED5_Pin);
                    LED_OFF(LED6_GPIO_Port, LED6_Pin);
                    LED_OFF(LED7_GPIO_Port, LED7_Pin);
                    fb_state.active = 0;
                }
                /* Admin modes persist until timeout or save */
            } else {
                /* Save attendance info for card display page */
                card_att_event    = card_info.att_event;
                card_att_status   = card_info.att_status;
                card_duration_sec = card_info.duration_sec;

                /* Admin card: enter admin mode (or refresh timeout) */
                if (card_info.card_type == CARD_TYPE_ADMIN) {
                    if (mode == DISP_MODE_ADMIN_INFO
                        || mode == DISP_MODE_ADMIN_SETTING) {
                        /* Already in admin mode, refresh timeout */
                        admin_enter_tick = now;
                    } else {
                        mode = DISP_MODE_ADMIN_INFO;
                        admin_enter_tick = now;
                        /* Load current config for editing */
                        {
                            DeviceConfig_t *cfg = NFC_Storage_GetConfig();
                            admin_dev_id   = cfg->dev_id;
                            admin_att_mode = cfg->att_mode;
                        }
                        admin_field = ADMIN_FIELD_DEV_ID;
                        admin_blink = 1;
                        /* admin blink tick updated */

                        /* Trigger admin LED feedback */
                        fb_state.active   = 1;
                        fb_state.type     = FB_EVT_ADMIN;
                        fb_state.phase    = 0;
                        fb_state.start_tick    = now;
                        fb_state.phase_deadline = now + 300;
                        MIDI_Beep(9U, 250U);  /* C5 high tone, 250ms */
                    }
                } else if (mode == DISP_MODE_ADMIN_INFO
                           || mode == DISP_MODE_ADMIN_SETTING) {
                    /* Normal card during admin mode: reject with beep */
                    MIDI_Beep(1U, 50U);  /* C4 low tone, 50ms */
                    /* Don't change display, stay in admin mode */
                } else {
                    mode = DISP_MODE_CARD;

                    /* Card type LED indication: LED1=present, LED2=normal, LED3=image */
                    LED_ON(LED1_GPIO_Port, LED1_Pin);
                    if (card_info.card_type == CARD_TYPE_NORMAL) {
                        LED_ON(LED2_GPIO_Port, LED2_Pin);
                        LED_OFF(LED3_GPIO_Port, LED3_Pin);
                    } else if (card_info.card_type == CARD_TYPE_IMAGE) {
                        LED_OFF(LED2_GPIO_Port, LED2_Pin);
                        LED_ON(LED3_GPIO_Port, LED3_Pin);
                    }

                    /* Record entry time for avatar delay (2s) */
                    card_enter_tick    = now;
                    card_disp_is_image = (card_info.card_type == CARD_TYPE_IMAGE);

                    /* Trigger LED/buzzer feedback based on attendance result */
                    fb_state.active   = 1;
                    fb_state.type     = card_info.feedback;
                    fb_state.phase    = 0;
                    fb_state.start_tick    = now;

                    /* Set initial phase deadline based on feedback type */
                    if (card_info.feedback == FB_EVT_INVALID) {
                        /* Invalid: red blink 2x100ms + beep 2x100ms */
                        fb_state.phase_deadline = now + 100;
                        MIDI_Beep(1U, 100U);  /* C4 low tone, 100ms */
                    } else if (card_info.feedback == FB_EVT_DUP) {
                        /* Duplicate: yellow 50ms + short beep */
                        fb_state.phase_deadline = now + 50;
                        MIDI_Beep(5U, 50U);  /* G4 mid tone, 50ms */
                    } else {
                        /* Valid entry/exit: green 150ms + beep 100ms */
                        fb_state.phase_deadline = now + 150;
                        MIDI_Beep(9U, 100U);  /* C5 high tone, 100ms */
                    }
                }
            }
        }

        /* ---- LED feedback state machine (50ms tick) ---- */
        if (fb_state.active) {
            if (fb_state.phase == 0) {
                /* Phase 0: turn on LEDs (first run since activation) */
                switch (fb_state.type) {
                case FB_EVT_VALID_ENTRY:
                case FB_EVT_VALID_EXIT:
                    LED_ON(FB_LED_GREEN_PORT, FB_LED_GREEN_PIN);
                    break;
                case FB_EVT_INVALID:
                    LED_ON(FB_LED_RED_PORT, FB_LED_RED_PIN);
                    break;
                case FB_EVT_DUP:
                    LED_ON(FB_LED_YELLOW_PORT, FB_LED_YELLOW_PIN);
                    break;
                case FB_EVT_ADMIN:
                    LED_ON(FB_LED_ADMIN_PORT, FB_LED_ADMIN_PIN);
                    break;
                default:
                    break;
                }
                fb_state.phase = 1;
            } else if ((int32_t)(now - fb_state.phase_deadline) >= 0) {
                /* Deadline reached, advance to next phase */
                fb_state.phase++;
                if (fb_state.type == FB_EVT_INVALID) {
                    /* Invalid: red blink 2x (100ms on, 100ms off, 100ms on, 100ms off) */
                    if (fb_state.phase == 2) {
                        /* First blink off */
                        LED_OFF(FB_LED_RED_PORT, FB_LED_RED_PIN);
                        fb_state.phase_deadline = now + 100;
                    } else if (fb_state.phase == 3) {
                        /* Second blink on */
                        LED_ON(FB_LED_RED_PORT, FB_LED_RED_PIN);
                        MIDI_Beep(1U, 100U);  /* C4 low tone, 100ms */
                        fb_state.phase_deadline = now + 100;
                    } else if (fb_state.phase == 4) {
                        /* Second blink off - done */
                        LED_OFF(FB_LED_RED_PORT, FB_LED_RED_PIN);
                        fb_state.active = 0;
                    }
                } else {
                    /* Single on/off: turn off and done */
                    LED_OFF(FB_LED_GREEN_PORT, FB_LED_GREEN_PIN);
                    LED_OFF(FB_LED_RED_PORT, FB_LED_RED_PIN);
                    LED_OFF(FB_LED_YELLOW_PORT, FB_LED_YELLOW_PIN);
                    LED_OFF(FB_LED_ADMIN_PORT, FB_LED_ADMIN_PIN);
                    fb_state.active = 0;
                }
            }
        }

        /* ---- Render to OLED ---- */
        OLED_Clear();

        if (mode == DISP_MODE_SPLASH) {
            /* Logo on left half: 64x64 native page-format bitmap */
            OLED_DrawBitmap(0, 0, 64, 64, logo);
            OLED_SetFont(u8g2_font_wqy12_t_gb2312);
            OLED_DrawUTF8(68, 36,  "姓名:杨城");


            /* Chinese course name on right side (wqy12, 12px font) */
            OLED_SetFont(u8g2_font_wqy12_t_gb2312);
            OLED_DrawUTF8(68, 12,  "专业综合");
            OLED_DrawUTF8(68, 25, "实践||");

            /* Project name in ASCII (6x10 font) */
            OLED_SetFont(u8g2_font_6x10_tf);
            OLED_ShowString(68, 46, "NFC");
            OLED_ShowString(68, 56, "Attendance");

        } else if (mode == DISP_MODE_CLOCK) {
            OLED_SetFont(u8g2_font_6x10_tf);

            /* Date + weekday */
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u %s",
                     dt.year, dt.month, dt.day, weekdays[dt.weekday]);
            OLED_ShowString(0, LINE1_Y, line_buf);

            /* Time */
            snprintf(line_buf, sizeof(line_buf),
                     "     %02u:%02u:%02u",
                     dt.hour, dt.minute, dt.second);
            OLED_ShowString(0, LINE2_Y, line_buf);

            /* Status or weather text (LINE3) */
            if (g_weather.valid) {
                snprintf(line_buf, sizeof(line_buf), "%s %s~%sC",
                         g_weather.textDay, g_weather.high, g_weather.low);
                OLED_ShowString(0, LINE3_Y, line_buf);
            } else {
                OLED_ShowString(0, LINE3_Y, "Waiting card...");
            }

            /* Temperature: right-aligned at LINE4 */
            snprintf(line_buf, sizeof(line_buf),
                     "%.1fC", (double)temperature);
            {
                uint8_t temp_len = (uint8_t)strlen(line_buf);
                OLED_ShowString((uint8_t)(128U - temp_len * 6U),
                                LINE4_Y, line_buf);
            }

        } else if (mode == DISP_MODE_SETTING) {
            OLED_SetFont(u8g2_font_6x10_tf);

            OLED_ShowString(0, 8, "=== SET DATE/TIME ===");

            /* Date: blink only when editing YEAR/MONTH/DAY */
            OLED_ShowString(0, 18, "D:");
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u", dt.year, dt.month, dt.day);
            ApplyBlink(line_buf,
                       (field <= FIELD_DAY) ? blink_on : 1, field);
            OLED_ShowString(12, 18, line_buf);

            /* Time: blink only when editing HOUR/MINUTE/SECOND */
            snprintf(line_buf, sizeof(line_buf),
                     "%02u:%02u:%02u", dt.hour, dt.minute, dt.second);
            ApplyBlink(line_buf,
                       (field >= FIELD_HOUR) ? blink_on : 1, field);
            OLED_ShowString(0, 30, "T:");
            OLED_ShowString(12, 30, line_buf);

            /* Field name and hints */
            snprintf(line_buf, sizeof(line_buf),
                     "Set: %s  +-:adj", field_names[field]);
            OLED_ShowString(0, 44, line_buf);
            OLED_ShowString(0, 56, "KEY4:save MODE:next");

        } else if (mode == DISP_MODE_ADMIN_INFO) {
            /* ---- Admin card info page (2 sec) ---- */
            OLED_SetFont(u8g2_font_6x10_tf);
            OLED_ShowString(0, 8, "=== ADMIN CARD ===");
            snprintf(line_buf, sizeof(line_buf),
                     "ID: %lu", (unsigned long)card_info.id_num);
            OLED_ShowString(0, 22, line_buf);
            snprintf(line_buf, sizeof(line_buf),
                     "UID:%02X%02X%02X%02X",
                     card_info.uid[0], card_info.uid[1],
                     card_info.uid[2], card_info.uid[3]);
            OLED_ShowString(0, 36, line_buf);
            OLED_ShowString(0, 52, "Entering setup...");

        } else if (mode == DISP_MODE_ADMIN_SETTING) {
            /* ---- Admin setting page: dev_id + att_mode ---- */
            const char *mode_names[] = {"Entry", "Exit", "Both"};
            OLED_SetFont(u8g2_font_6x10_tf);
            OLED_ShowString(0, 8, "=== ADMIN SETTING ===");

            /* Device ID */
            snprintf(line_buf, sizeof(line_buf), "Dev ID: %-5u",
                     (unsigned int)admin_dev_id);
            if (admin_field == ADMIN_FIELD_DEV_ID && admin_blink) {
                /* Blink selected field: show with brackets */
                snprintf(line_buf, sizeof(line_buf), "Dev ID:[%-5u]",
                         (unsigned int)admin_dev_id);
            }
            OLED_ShowString(0, 22, line_buf);

            /* Attendance mode */
            snprintf(line_buf, sizeof(line_buf), "Mode: %s",
                     (admin_att_mode < 3) ? mode_names[admin_att_mode] : "?");
            if (admin_field == ADMIN_FIELD_ATT_MODE && admin_blink) {
                snprintf(line_buf, sizeof(line_buf), "Mode:[%s]",
                         (admin_att_mode < 3) ? mode_names[admin_att_mode] : "?");
            }
            OLED_ShowString(0, 36, line_buf);

            OLED_ShowString(0, 52, "UP/DN:adj MODE:next");
            OLED_ShowString(0, 62, "KEY4:save 120s timeout");

        } else if (mode == DISP_MODE_CARD) {
            /*
             * For image cards, show avatar page after a 2-second delay.
             * Normal cards always show the standard card info page.
             */
            if (card_disp_is_image && card_has_avatar
                && (now - card_enter_tick >= 2000U)) {
                /* ---- Avatar Display Page (48x64 photo + text) ---- */
                OLED_DrawBitmap(2, 0, 48U, 64U, card_avatar_bmp);

                OLED_SetFont(u8g2_font_6x10_tf);
                OLED_ShowString(56, 10, "Photo Card");

                snprintf(line_buf, sizeof(line_buf),
                         "UID:%02X%02X%02X%02X",
                         card_info.uid[0], card_info.uid[1],
                         card_info.uid[2], card_info.uid[3]);
                OLED_ShowString(56, 24, line_buf);

                snprintf(line_buf, sizeof(line_buf),
                         "ID:%lu", (unsigned long)card_info.id_num);
                OLED_ShowString(56, 36, line_buf);

                /* Attendance result on avatar page */
                {
                    const char *evt_str;
                    evt_str = (card_att_event == ATT_EVENT_ENTRY) ? "In" : "Out";
                    if (card_att_status == ATT_STATUS_NORMAL) {
                        snprintf(line_buf, sizeof(line_buf), "%s OK", evt_str);
                    } else if (card_att_status == ATT_STATUS_DUP) {
                        snprintf(line_buf, sizeof(line_buf), "%s DUP", evt_str);
                    } else {
                        snprintf(line_buf, sizeof(line_buf), "%s ERR", evt_str);
                    }
                    OLED_ShowString(56, 48, line_buf);

                    if (card_att_event == ATT_EVENT_EXIT
                        && card_att_status == ATT_STATUS_NORMAL
                        && card_duration_sec > 0) {
                        uint32_t h, m;
                        h = card_duration_sec / 3600U;
                        m = (card_duration_sec % 3600U) / 60U;
                        if (h > 0) {
                            snprintf(line_buf, sizeof(line_buf),
                                     "%luh%lum", (unsigned long)h, (unsigned long)m);
                        } else {
                            snprintf(line_buf, sizeof(line_buf),
                                     "%lum", (unsigned long)m);
                        }
                        OLED_ShowString(56, 58, line_buf);
                    }
                }
            } else {
                /* ---- Normal Card Info Page ---- */
                OLED_SetFont(u8g2_font_6x10_tf);

                /* Card UID header */
                snprintf(line_buf, sizeof(line_buf),
                         "Card: %02X%02X%02X%02X",
                         card_info.uid[0], card_info.uid[1],
                         card_info.uid[2], card_info.uid[3]);
                OLED_ShowString(0, 8, line_buf);

                /* Name bitmap (Chinese dot-matrix from card sectors 9+) */
                OLED_SetFont(u8g2_font_wqy12_t_gb2312);
                OLED_DrawUTF8(0, 20, "姓名:");
                if (card_has_bmp) {
                    OLED_DrawBitmap(24, 10, NAME_BMP_W, NAME_BMP_H, card_name_bmp);
                }

                /* Student ID in decimal */
                snprintf(line_buf, sizeof(line_buf),
                         "ID: %lu", (unsigned long)card_info.id_num);
                OLED_ShowString(0, 32, line_buf);

                /* Department label (Chinese) + Sector bitmap from card */
                OLED_SetFont(u8g2_font_wqy12_t_gb2312);
                OLED_DrawUTF8(0, 44, "部门:");
                if (card_has_bmp) {
                    OLED_DrawBitmap(24, 35, SECTOR_BMP_W, SECTOR_BMP_H,
                                    card_sector_bmp);
                }

                /* Swipe timestamp + attendance result */
                {
                    const char *evt_str;
                    const char *sts_str;

                    evt_str = (card_att_event == ATT_EVENT_ENTRY) ? "In " : "Out";
                    if (card_att_status == ATT_STATUS_NORMAL) {
                        sts_str = "OK";
                    } else if (card_att_status == ATT_STATUS_DUP) {
                        sts_str = "DUP";
                    } else if (card_att_status == ATT_STATUS_NO_ENTRY) {
                        sts_str = "NOIN";
                    } else {
                        sts_str = "UNK";
                    }

                    snprintf(line_buf, sizeof(line_buf),
                             "%s %s %02u:%02u:%02u",
                             evt_str, sts_str,
                             card_info.timestamp.hour,
                             card_info.timestamp.minute,
                             card_info.timestamp.second);
                    OLED_ShowString(0, 60, line_buf);

                    /* Show duration for exit events */
                    if (card_att_event == ATT_EVENT_EXIT
                        && card_att_status == ATT_STATUS_NORMAL
                        && card_duration_sec > 0) {
                        uint32_t h, m, s;
                        h = card_duration_sec / 3600U;
                        m = (card_duration_sec % 3600U) / 60U;
                        s = card_duration_sec % 60U;
                        if (h > 0) {
                            snprintf(line_buf, sizeof(line_buf),
                                     "%luh%lum", (unsigned long)h, (unsigned long)m);
                        } else if (m > 0) {
                            snprintf(line_buf, sizeof(line_buf),
                                     "%lum%lus", (unsigned long)m, (unsigned long)s);
                        } else {
                            snprintf(line_buf, sizeof(line_buf),
                                     "%lus", (unsigned long)s);
                        }
                        OLED_ShowString(80, 60, line_buf);
                    }
                }

//              OLED_ShowString(0, 56, "MODE: back to clock");
            }
        }

        OLED_Refresh();

        osDelay(DISPLAY_PERIOD_MS);
    }
}

