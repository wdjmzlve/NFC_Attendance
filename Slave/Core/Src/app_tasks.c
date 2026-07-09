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
#include <stdio.h>
#include <string.h>
#include "usart.h"
#include "u8g2.h"
/* -------------------------------------------------------------------------- */
/*  Constants and Macros                                                      */
/* -------------------------------------------------------------------------- */

/* Task periods (ms) */
#define DISPLAY_PERIOD_MS       50U
#define CARD_READ_PERIOD_MS     300U
#define SPLASH_DURATION_MS      3000U
#define CARD_DISPLAY_MS         3000U
#define TEMP_READ_PERIOD_MS     2000U
#define BEEP_DURATION_MS        80U

/* Key timing (ms) */
#define KEY_LONG_PRESS_MS       600U
#define KEY_REPEAT_MS           150U
#define BLINK_PERIOD_MS         500U

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
    DISP_MODE_SPLASH,     /* Boot splash screen (3 sec)                       */
    DISP_MODE_CLOCK,      /* Clock + temperature standby                      */
    DISP_MODE_SETTING,    /* Date/time setting via keys                       */
    DISP_MODE_CARD        /* Card detected info overlay                       */
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

/* -------------------------------------------------------------------------- */
/*  Message Queue                                                             */
/* -------------------------------------------------------------------------- */

#define CARD_QUEUE_SIZE  4U
osMessageQueueId_t cardQueueHandle;
/* Card bitmap buffers: read from sectors 9+, used by display task */
static uint8_t card_name_bmp[161];
static uint8_t card_sector_bmp[161];
static uint8_t card_has_bmp = 0;

/* -------------------------------------------------------------------------- */
/*  RC522互斥量                                                               */
/* -------------------------------------------------------------------------- */
osMutexId_t rc522MutexHandle;

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

/* 向前声明：Beep 实现在本文件后半部分 */
static void Beep(uint32_t duration_ms);

void Task_CardRead(void *argument)
{
    (void)argument;

    uint8_t uid[4];
    uint8_t block_data[16];
    uint8_t write_buf[16];
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
    if (RC522_ScanCard(uid) == RC522_OK) {
        memset(write_buf, 0, sizeof(write_buf));
        memcpy(&write_buf[0], uid, 4);
        write_buf[4]  = 0x01;
        write_buf[5]  = 0x5F;
        write_buf[6]  = 0x92;
        write_buf[7]  = 0xD2;
        write_buf[12] = CARD_TYPE_NORMAL;

        sum = 0;
        for (i = 0; i < 14U; i++) {
            sum += write_buf[i];
        }
        write_buf[14] = (uint8_t)(sum & 0xFFU);
        write_buf[15] = (uint8_t)((sum >> 8U) & 0xFFU);

        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
                            3, default_key, uid) == RC522_OK) {
            RC522_WriteBlock(0, 1, write_buf);
        }
        RC522_Halt();
        RC522_WaitCardOff();
    }

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
            }

            RC522_Halt();
        }

        /* ---- 释放 RC522 互斥量 ---- */
        osMutexRelease(rc522MutexHandle);

        if (status == RC522_OK) {
            /* 发给显示任务 */
            osMessageQueuePut(cardQueueHandle, &card_info, 0, 0);

            /* 蜂鸣反馈 */
            Beep(BEEP_DURATION_MS);
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

        } else {
            /* 未知指令 */
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

/*  Helper: Short Beep                                                        */
/* -------------------------------------------------------------------------- */
static void Beep(uint32_t duration_ms)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    osDelay(duration_ms);
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
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

    /* ---- Key event from Task_KeyScan via queue ---- */
    KeyMsg_t key_msg;

    /* Display buffers */
    char line_buf[24];
    const char *weekdays[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *field_names[] = {"YEAR","MONTH","DAY","HOUR","MIN","SEC"};

    for (;;) {
        uint32_t now = osKernelGetTickCount();

        /* ---- Process key events from Task_KeyScan via IPC queue ---- */
        while (osMessageQueueGet(keyQueueHandle, &key_msg, NULL, 0) == osOK) {
            if (key_msg.key_id == KEY_ID_MODE) {
                /* MODE key: enter setting / cycle field / exit card mode */
                if (mode == DISP_MODE_CLOCK) {
                    BSP_RTC_GetDateTime(&dt);
                    dt.second = 0;
                    mode  = DISP_MODE_SETTING;
                    field = FIELD_YEAR;
                    blink_on = 1;
                    last_blink_tick = now;
                } else if (mode == DISP_MODE_SETTING) {
                    field = (SetField_t)((uint8_t)field + 1U);
                    if (field >= FIELD_COUNT) {
                        dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                        dt.second = 0;
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
                }
            } else if (key_msg.key_id == KEY_ID_SAVE) {
                /* SAVE key: save & exit setting mode immediately */
                if (mode == DISP_MODE_SETTING) {
                    dt.weekday = BSP_RTC_CalcWeekday(dt.year, dt.month, dt.day);
                    dt.second = 0;
                    BSP_RTC_SetDateTime(&dt);
                    BSP_RTC_MarkInitialized();
                    mode = DISP_MODE_CLOCK;
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

        /* ---- Blink timer for setting mode cursor ---- */
        if (now - last_blink_tick >= BLINK_PERIOD_MS) {
            blink_on = !blink_on;
            last_blink_tick = now;
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
                }
            } else {
                mode = DISP_MODE_CARD;

                /* Update card status LEDs */
                LED_ON(LED1_GPIO_Port, LED1_Pin);
                if (card_info.card_type == CARD_TYPE_NORMAL) {
                    LED_ON(LED2_GPIO_Port, LED2_Pin);
                    LED_OFF(LED3_GPIO_Port, LED3_Pin);
                } else if (card_info.card_type == CARD_TYPE_IMAGE) {
                    LED_OFF(LED2_GPIO_Port, LED2_Pin);
                    LED_ON(LED3_GPIO_Port, LED3_Pin);
                } else {
                    LED_OFF(LED2_GPIO_Port, LED2_Pin);
                    LED_OFF(LED3_GPIO_Port, LED3_Pin);
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

            /* Status text */
            OLED_ShowString(0, LINE3_Y, "Waiting card...");

            /* Temperature: right-aligned at bottom */
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

        } else if (mode == DISP_MODE_CARD) {
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
			

            
            /* Swipe timestamp */
            snprintf(line_buf, sizeof(line_buf),
                     "%04u-%02u-%02u %02u:%02u:%02u",
                     card_info.timestamp.year,
                     card_info.timestamp.month,
                     card_info.timestamp.day,
                     card_info.timestamp.hour,
                     card_info.timestamp.minute,
                     card_info.timestamp.second);
            OLED_ShowString(0, 60, line_buf);

//            OLED_ShowString(0, 56, "MODE: back to clock");
        }

        OLED_Refresh();

        osDelay(DISPLAY_PERIOD_MS);
    }
}

/* -------------------------------------------------------------------------- */
/*  Task: Card Read (RC522 Polling + Data Read)                               */
/* -------------------------------------------------------------------------- */
//void Task_CardRead(void *argument)
//{
//    (void)argument;

//    uint8_t uid[4];
//    uint8_t block_data[16];
//    uint8_t write_buf[16];
//    uint8_t default_key[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
//    uint8_t i;
//    uint16_t sum;
//    CardInfo_t card_info;

//    /* Initialize RC522 platform */
//    RC522_Platform_Init();
//    RC522_ConfigISOType('A');

//    /*
//     * One-time card issuance: write default account header to
//     * Sector 0, Block 1 of the first card presented after boot.
//     *
//     * Block 1 layout (16 bytes):
//     *   [0..3]   UID (from anticollision)
//     *   [4..7]   Student ID (default: 23040722 -> 0x01,0x5F,0x92,0xD2)
//     *   [8..11]  Reserved (0x00)
//     *   [12]     Card type (0x00=Normal, 0x01=Image, 0x02=Admin)
//     *   [13]     Status flag (0x00)
//     *   [14..15] Checksum = sum(bytes 0-13), little-endian
//     */
//    if (RC522_ScanCard(uid) == RC522_OK) {
//        memset(write_buf, 0, sizeof(write_buf));
//        memcpy(&write_buf[0], uid, 4);               /* UID from card        */
//        write_buf[4]  = 0x01;                         /* Student ID byte 0    */
//        write_buf[5]  = 0x5F;                         /* Student ID byte 1    */
//        write_buf[6]  = 0x92;                         /* Student ID byte 2    */
//        write_buf[7]  = 0xD2;                         /* Student ID byte 3    */
//        /* bytes 8-11 remain 0 (reserved)                                     */
//        write_buf[12] = CARD_TYPE_NORMAL;             /* Card type = Normal   */
//        /* byte 13 remains 0 (status flag)                                    */

//        /* 16-bit sum of bytes 0-13, little-endian */
//        sum = 0;
//        for (i = 0; i < 14U; i++) {
//            sum += write_buf[i];
//        }
//        write_buf[14] = (uint8_t)(sum & 0xFFU);
//        write_buf[15] = (uint8_t)((sum >> 8U) & 0xFFU);

//        /* Authenticate Sector 0 (trailer = block 3), then write block 1 */
//        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
//                            3, default_key, uid) == RC522_OK) {
//            RC522_WriteBlock(0, 1, write_buf);
//        }

//        /*
//         * Write Name[] + Sector[] to sectors 9-13, blocks 0-2 (240 bytes).
//         * Bytes are walked sequentially, packed into 16-byte blocks, and
//         * written to consecutive data blocks across 5 sectors.
//         */
//        {
//            uint8_t  blk_buf[16];
//            uint8_t  sec = 9, blk = 0;
//            uint16_t pos = 0, arr_idx;
//            const uint8_t *arrays[2];
//            uint16_t arr_len[2];

//            arrays[0]  = (const uint8_t *)Name;
//            arrays[1]  = (const uint8_t *)Sector;
//            arr_len[0] = 161U;
//            arr_len[1] = 161U;

//            for (arr_idx = 0; arr_idx < 2U; arr_idx++) {
//                uint16_t j;
//                for (j = 0; j < arr_len[arr_idx]; j++) {
//                    blk_buf[pos % 16U] = arrays[arr_idx][j];
//                    pos++;
//                    if ((pos % 16U) == 0U) {
//                        if (RC522_AuthState(RC522_PICC_AUTHENT1A,
//                                            (uint8_t)(sec * 4U + 3U),
//                                            default_key, uid) == RC522_OK) {
//                            RC522_WriteBlock(sec, blk, blk_buf);
//                        }
//                        blk++;
//                        if (blk >= 3U) { blk = 0U; sec++; }
//                    }
//                }
//            }
//            /* Write remaining partial block padded with 0x00 */
//            if ((pos % 16U) != 0U) {
//                while ((pos % 16U) != 0U) {
//                    blk_buf[pos % 16U] = 0x00;
//                    pos++;
//                }
//                if (RC522_AuthState(RC522_PICC_AUTHENT1A,
//                                    (uint8_t)(sec * 4U + 3U),
//                                    default_key, uid) == RC522_OK) {
//                    RC522_WriteBlock(sec, blk, blk_buf);
//                }
//            }
//        }

//        RC522_Halt();
//        RC522_WaitCardOff();
//    }

//    for (;;) {
//        char status = RC522_ScanCard(uid);
//        printf("%d\r\n", (uint8_t)status);

//        if (status == RC522_OK) {
//            /* Fill card info basics */
//            memcpy(card_info.uid, uid, 4);
//            BSP_RTC_GetDateTime(&card_info.timestamp);
//            card_info.card_type = 0;
//            card_info.id_num    = 0;

//            /*
//             * Read Sector 0, Block 1 (account header).
//             * Auth on block 3 (trailer of sector 0).
//             *
//             * Block 1 layout:
//             *   [0..3]   UID
//             *   [4..7]   Student ID (big-endian uint32)
//             *   [8..11]  Reserved
//             *   [12]     Card type (0x00=Normal, 0x01=Image, 0x02=Admin)
//             *   [13]     Status flag
//             *   [14..15] Checksum = sum(bytes 0-13), little-endian
//             */
//            status = RC522_AuthState(RC522_PICC_AUTHENT1A,
//                                     3, default_key, uid);
//            if (status == RC522_OK) {
//                status = RC522_Read(1, block_data);
//                if (status == RC522_OK) {
//                    /* Verify checksum: 16-bit sum of bytes 0-13 */
//                    sum = 0;
//                    for (i = 0; i < 14U; i++) {
//                        sum += block_data[i];
//                    }
//                    if ((uint8_t)(sum & 0xFFU) == block_data[14]
//                        && (uint8_t)((sum >> 8U) & 0xFFU) == block_data[15]) {
//                        card_info.card_type = block_data[12];
//                        /* Big-endian ID -> uint32_t */
//                        card_info.id_num =
//                            ((uint32_t)block_data[4] << 24U)
//                          | ((uint32_t)block_data[5] << 16U)
//                          | ((uint32_t)block_data[6] << 8U)
//                          |  (uint32_t)block_data[7];
//                    }
//                }
//            }

//            /*
//             * Read Name + Sector bitmaps from Sectors 9+ onward.
//             * Blocks are filled sequentially: 0, 1, 2 of each sector.
//             */
//            {
//                uint8_t  blk_buf[16];
//                uint8_t  sec = 9, blk = 0;
//                uint16_t pos = 0;

//                while (pos < BMP_TOTAL_BYTES) {
//                    if (blk >= 3U) { blk = 0U; sec++; }
//                    if (RC522_AuthState(RC522_PICC_AUTHENT1A,
//                                        (uint8_t)(sec * 4U + 3U),
//                                        default_key, uid) == RC522_OK) {
//                        if (RC522_ReadBlock(sec, blk, blk_buf) == RC522_OK) {
//                            uint8_t k;
//                            for (k = 0; k < 16U && pos < BMP_TOTAL_BYTES; k++) {
//                                if (pos < 161U) {
//                                    card_name_bmp[pos] = blk_buf[k];
//                                } else {
//                                    card_sector_bmp[pos - 161U] = blk_buf[k];
//                                }
//                                pos++;
//                            }
//                        }
//                    }
//                    blk++;
//                }
//                card_has_bmp = 1;
//            }

//            /* Halt card so it can be re-detected */
//            RC522_Halt();

//            /* Send to display task (non-blocking, drop if queue full) */
//            osMessageQueuePut(cardQueueHandle, &card_info, 0, 0);

//            /* Audible feedback */
//            Beep(BEEP_DURATION_MS);

//            /* Wait for card removal before scanning again */
//            RC522_WaitCardOff();
//        }
//        osDelay(CARD_READ_PERIOD_MS);
//    }
//}
