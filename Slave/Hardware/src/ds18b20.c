#include "ds18B20.h"
#include "delay_us.h"   /* 公共微秒延时服务 */
#include "FreeRTOS.h"   /* FreeRTOS 基础头文件，须在 task.h 之前 */
#include "task.h"       /* taskENTER_CRITICAL / taskEXIT_CRITICAL */
#include "main.h"

#define EnableINT()
#define DisableINT()

#define DS_PRECISION 		0x7f   //精度配置寄存器 1f=9位; 3f=10位; 5f=11位; 7f=12位;
#define DS_AlarmTH  		0x64
#define DS_AlarmTL  		0x8a
#define DS_CONVERT_TICK 1000

#define ResetDQ() HAL_GPIO_WritePin(DATA_GPIO_Port, DATA_Pin, GPIO_PIN_RESET)
#define SetDQ()  	HAL_GPIO_WritePin(DATA_GPIO_Port, DATA_Pin, GPIO_PIN_SET)
#define GetDQ()  	HAL_GPIO_ReadPin(DATA_GPIO_Port, DATA_Pin) 

unsigned char ResetDS18B20(void)
{
	unsigned char resport;
	SetDQ();
	delay_us(50);

	ResetDQ();
	delay_us(500);  //500us （该时间的时间范围可以从480到960微秒）
	SetDQ();

	/* 临界区：采样应答脉冲的窗口仅 15 µs，不能被抢占 */
	taskENTER_CRITICAL();

	delay_us(40);
	uint16_t cnt = 0;
	while(GetDQ() && cnt < 500)
	{
		++cnt;
		delay_us(1);
	}
	if (cnt >= 500)
		resport = 1;
	else
		resport = 0;

	taskEXIT_CRITICAL();

	delay_us(500);  //500us
	SetDQ();
	return resport;
}

void DS18B20WriteByte(unsigned char Dat)
{
	unsigned char i;
	for(i = 8; i > 0; i--)
	{
		/* 临界区：防止在写 '1' 时低电平被拉长超过 15 µs
		 * 保护时间约 5-7 µs/bit，8 bit 合计约 48 µs */
		taskENTER_CRITICAL();

		ResetDQ();
		delay_us(5);
		if(Dat & 0x01)
			SetDQ();
		else
			ResetDQ();

		taskEXIT_CRITICAL();

		delay_us(65);
		SetDQ();
		delay_us(2);
		Dat >>= 1;
	}
}


unsigned char DS18B20ReadByte(void)
{
	unsigned char i,Dat;
	SetDQ();
	delay_us(5);
	for(i = 8; i > 0; i--)
	{
		Dat >>= 1;

		/* 临界区：从拉低到采样必须在 15 µs 内完成
		 * 保护时间约 15 µs/bit，8 bit 合计约 120 µs */
		taskENTER_CRITICAL();

		ResetDQ();
		delay_us(5);
		SetDQ();
		delay_us(5);
		if(GetDQ())
			Dat |= 0x80;
		else
			Dat &= 0x7f;

		taskEXIT_CRITICAL();

		delay_us(65);
		SetDQ();
	}
	return Dat;
}

void ReadRom(unsigned char *Read_Addr)
{
	unsigned char i;

	DS18B20WriteByte(ReadROM);
	for(i = 8; i > 0; i--)
	{
		*Read_Addr = DS18B20ReadByte();
		Read_Addr++;
	}
}

void DS18B20Init(unsigned char Precision,unsigned char AlarmTH,unsigned char AlarmTL)
{
	DisableINT();
	ResetDS18B20();
	
	DS18B20WriteByte(SkipROM); 
	DS18B20WriteByte(WriteScratchpad);
	DS18B20WriteByte(AlarmTL);
	DS18B20WriteByte(AlarmTH);
	DS18B20WriteByte(Precision);

	ResetDS18B20();
	DS18B20WriteByte(SkipROM); 
	DS18B20WriteByte(CopyScratchpad);
	EnableINT();

	while(!GetDQ());  //等待复制完成 ///////////
}

void DS18B20StartConvert(void)
{
	DisableINT();
	ResetDS18B20();
	DS18B20WriteByte(SkipROM); 
	DS18B20WriteByte(StartConvert); 
	EnableINT();
}

void DS18B20_Configuration(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  GPIO_InitStruct.Pin = DATA_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
}


void ds18b20_init(void)
{
	delay_us_init();
	
	DS18B20_Configuration();
	ResetDS18B20();
	DS18B20Init(DS_PRECISION, DS_AlarmTH, DS_AlarmTL);
	DS18B20StartConvert();
}


float ds18b20_read(void)
{
	unsigned char DL, DH;
	unsigned short TemperatureData;
	float Temperature;

	DisableINT();
	DS18B20StartConvert();
	ResetDS18B20();
	DS18B20WriteByte(SkipROM); 
	DS18B20WriteByte(ReadScratchpad);
	DL = DS18B20ReadByte();
	DH = DS18B20ReadByte(); 
	EnableINT();

	TemperatureData = DH;
	TemperatureData <<= 8;
	TemperatureData |= DL;

	Temperature = (float)((float)TemperatureData * 0.0625); //分辨率为0.0625度

	return  Temperature;
}
