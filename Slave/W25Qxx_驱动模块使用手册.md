# W25Qxx SPI Flash 驱动模块使用手册

## 1. 驱动概述

W25Qxx 驱动用于驱动 W25Q 系列 SPI NOR Flash 存储芯片（W25Q80/16/32/64/128/256），提供读取 ID、读数据、页编程、扇区擦除、整片擦除、掉电/唤醒等完整操作。驱动通过 STM32 硬件 SPI1 与 Flash 芯片通信，支持自动识别芯片型号，并提供带擦除管理的安全写入接口。

| 项目 | 说明 |
|------|------|
| 文件 | `w25qxx.c`、`w25qxx.h` |
| 依赖 | STM32 HAL 库、`spi.h`（硬件 SPI1） |
| 支持芯片 | W25Q80 / W25Q16 / W25Q32 / W25Q64 / W25Q128 / W25Q256 |
| 通信接口 | 硬件 SPI（SPI1），片选由 GPIO 控制 |

---

## 2. 模块架构

```
┌───────────────────────────────────────┐
│             用户应用层                │
│   W25QXX_Read() / W25QXX_Write()      │
└─────────────────┬─────────────────────┘
                  │
┌─────────────────▼─────────────────────┐
│           W25Qxx 驱动层               │
│  ┌──────────┐ ┌──────────┐ ┌────────┐ │
│  │ 读操作    │ │ 写操作    │ │ 擦除    │ │
│  │ ReadData │ │ PageProg │ │ Sector │ │
│  │          │ │ +擦除管理 │ │ Chip   │ │
│  └──────────┘ └──────────┘ └────────┘ │
│  ┌──────────────────────────────────┐ │
│  │ 状态寄存器 / 写使能 / 等待忙      │ │
│  └──────────────────────────────────┘ │
└─────────────────┬─────────────────────┘
                  │ SPI1 + GPIO CS
┌─────────────────▼─────────────────────┐
│           STM32 HAL / 硬件            │
│      HAL_SPI_TransmitReceive          │
└───────────────────────────────────────┘
```

**核心设计**：
- **页编程 + 自动跨页**：`W25QXX_Write_NoCheck` 自动处理跨页写入，每页 256 字节。
- **安全写入**：`W25QXX_Write` 内部执行"读扇区→检测是否需擦除→擦除→回写"流程，确保数据不丢失。
- **忙等待**：所有写/擦操作后通过状态寄存器 BUSY 位轮询等待完成。

---

## 3. 核心数据结构

### 芯片型号 ID 定义

```c
#define W25Q80   0XEF13
#define W25Q16   0XEF14
#define W25Q32   0XEF15
#define W25Q64   0XEF16
#define W25Q128  0XEF17
#define W25Q256  0XEF18
```

### 全局变量

```c
extern uint16_t W25QXX_TYPE;  // 当前芯片型号（由 W25QXX_Init 自动读取填充）
```

### 关键常量

| 宏 | 值 | 说明 |
|----|----|------|
| `SECTOR_SIZE` | 4096 | 扇区大小（字节） |
| `W25QXX_CS0` | — | 拉低片选（选中） |
| `W25QXX_CS1` | — | 拉高片选（释放） |

---

## 4. API 参考

### 初始化与识别

| 函数 | 说明 |
|------|------|
| `void W25QXX_Init(void)` | 初始化 SPI Flash，设置 SPI 速度，读取并保存芯片 ID 到 `W25QXX_TYPE` |
| `uint16_t W25QXX_ReadID(void)` | 读取芯片 ID（ Manufacturer Device ID） |
| `void W25QXX_4ByteAddr_Enable(void)` | 使能 4 字节地址模式（W25Q256 需要） |

### 读写操作

| 函数 | 说明 |
|------|------|
| `void W25QXX_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)` | 从指定地址读取数据 |
| `void W25QXX_Write(uint8_t* pBuffer, uint32_t WriteAddr, uint32_t NumByteToWrite)` | 写入数据（自动擦除管理） |
| `void W25QXX_Write_NoCheck(uint8_t* pBuffer, uint32_t WriteAddr, uint32_t NumByteToWrite)` | 无校验写入（目标地址必须全为 0xFF） |

### 擦除操作

| 函数 | 说明 |
|------|------|
| `void W25QXX_Erase_Sector(uint32_t Dst_Addr)` | 擦除指定扇区（参数为扇区号，非地址） |
| `void W25QXX_Erase_Chip(void)` | 整片擦除（耗时较长） |

### 状态与保护

| 函数 | 说明 |
|------|------|
| `uint8_t W25QXX_ReadSR(uint8_t regno)` | 读取状态寄存器（regno: 1~3） |
| `void W25QXX_Write_SR(uint8_t regno, uint8_t sr)` | 写状态寄存器 |
| `void W25QXX_Write_Enable(void)` | 写使能 |
| `void W25QXX_Write_Disable(void)` | 写禁止 |
| `void W25QXX_Wait_Busy(void)` | 等待芯片空闲（轮询 BUSY 位） |

### 电源管理

| 函数 | 说明 |
|------|------|
| `void W25QXX_PowerDown(void)` | 进入掉电模式 |
| `void W25QXX_WAKEUP(void)` | 唤醒 |

---

## 5. 使用示例

### 基本使用 — 初始化、读写

```c
#include "w25qxx.h"

uint8_t writeBuf[] = "Hello W25Q128!";
uint8_t readBuf[32];

/* 1. 初始化（自动读取芯片 ID） */
W25QXX_Init();
printf("Flash ID: 0x%04X, Type: W25Q%d\r\n",
       W25QXX_TYPE, ...);

/* 2. 写入数据到地址 0x000000 */
W25QXX_Write(writeBuf, 0x000000, sizeof(writeBuf));

/* 3. 读回数据 */
W25QXX_Read(readBuf, 0x000000, sizeof(writeBuf));
printf("Read: %s\r\n", readBuf);
```

### 扇区擦除后写入

```c
/* 擦除第 0 扇区（4KB），再写入 */
W25QXX_Erase_Sector(0);          // 参数是扇区号，不是地址
W25QXX_Write_NoCheck(data, 0, 256);  // 无校验写入（已擦除，全 0xFF）
```

### 存储配置数据

```c
#define CFG_ADDR  0x000000
typedef struct { int id; char name[16]; } Config_t;

Config_t cfg = { .id = 1, .name = "V25_BSP" };

/* 保存配置 */
W25QXX_Write((uint8_t *)&cfg, CFG_ADDR, sizeof(cfg));

/* 读取配置 */
Config_t readCfg;
W25QXX_Read((uint8_t *)&readCfg, CFG_ADDR, sizeof(readCfg));
```

---

## 7. NFCAttend 工程实际应用

NFCAttend 考勤系统使用 W25Q128 作为持久化存储，保存设备配置和考勤记录。

### 初始化（freertos.c → StartDefaultTask）

```c
#include "w25qxx.h"

/* 初始化 W25Qxx */
W25QXX_Init();
uint16_t flash_id = W25QXX_ReadID();
printf("W25Qxx ID: 0x%04X\r\n", flash_id);
```

### 设备配置存储（nfc_storage.c）

NFCAttend 将设备编号、考勤模式等配置保存在 W25Qxx 固定地址：

```c
#define STORAGE_DEV_ID_ADDR     0x0000   /* 设备编号 (2字节) */
#define STORAGE_MODE_ADDR       0x0010   /* 考勤模式 (1字节) */

/* 保存设备编号 */
void NFC_STORAGE_SaveDevId(uint16_t dev_id)
{
    W25QXX_Erase_Sector(0);  /* 擦除扇区0 */
    W25QXX_Wait_Busy();
    W25QXX_Write_NoCheck((uint8_t *)&dev_id, STORAGE_DEV_ID_ADDR, 2);
    W25QXX_Wait_Busy();
}

/* 读取设备编号 */
uint16_t NFC_STORAGE_LoadDevId(void)
{
    uint16_t dev_id = 0;
    W25QXX_Read((uint8_t *)&dev_id, STORAGE_DEV_ID_ADDR, 2);
    return dev_id;
}
```

### 考勤记录存储（nfc_storage.c）

考勤记录批量写入 W25Qxx：

```c
#define RECORD_START_ADDR    0x1000   /* 考勤记录起始地址 */
#define RECORD_SIZE          16       /* 每条记录16字节 */
#define RECORD_MAX_ENTRIES   1024     /* 最多1024条 */

/* 添加一条考勤记录 */
int NFC_STORAGE_AddRecord(const uint8_t *record)
{
    uint16_t count;
    /* 读取当前记录数 */
    W25QXX_Read((uint8_t *)&count, RECORD_START_ADDR - 2, 2);

    if (count >= RECORD_MAX_ENTRIES) return -1;

    /* 写入记录 */
    uint32_t addr = RECORD_START_ADDR + count * RECORD_SIZE;
    W25QXX_Write((uint8_t *)record, addr, RECORD_SIZE);

    /* 更新记录数 */
    count++;
    W25QXX_Write_NoCheck((uint8_t *)&count, RECORD_START_ADDR - 2, 2);
    W25QXX_Wait_Busy();
    return 0;
}
```

> **设计要点**：NFCAttend 使用 W25Qxx 的扇区 0 存储配置参数（设备编号、考勤模式等），扇区 1~2 存储考勤记录。配置数据使用固定地址直接读写，考勤记录使用计数头+顺序写入方式。

---

## 6. 注意事项

- **擦除单位**：Flash 写入只能将 1 写为 0，需先擦除（将目标区域置为 0xFF）才能写入新数据。`W25QXX_Write()` 内部已自动处理擦除；`W25QXX_Write_NoCheck()` 不擦除，需确保目标区域已为 0xFF。
- **扇区擦除参数**：`W25QXX_Erase_Sector()` 的参数是**扇区号**（0, 1, 2...），内部会乘以 `SECTOR_SIZE` 转换为地址。
- **W25Q256 地址模式**：W25Q256 使用 4 字节地址，需调用 `W25QXX_4ByteAddr_Enable()` 使能；其他型号使用 3 字节地址，驱动内部会根据 `W25QXX_TYPE` 自动判断。
- **整片擦除耗时**：`W25QXX_Erase_Chip()` 耗时可达数十秒，期间会阻塞，不建议在实时任务中调用。
- **内部缓冲区**：`W25QXX_Write()` 使用了一个 `SECTOR_SIZE`（4096 字节）的静态缓冲区 `W25QXX_BUFFER`，会占用 RAM。
- **片选引脚**：片选引脚由宏 `W25QXX_CS0/CS1` 控制（默认 `SPI1_CS_GPIO_Port / SPI1_CS_Pin`），如引脚不同需修改 `w25qxx.h`。
- **SPI 速度**：初始化时设置为 `SPI_BAUDRATEPRESCALER_4`（约 21MHz），可通过修改 `W25QXX_Init()` 中的参数调整。
