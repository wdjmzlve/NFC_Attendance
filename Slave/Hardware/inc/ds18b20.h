#ifndef __DS18B20_H__
#define __DS18B20_H__

#include"main.h"

#define  SkipROM    0xCC  //����ROM
#define  SearchROM  0xF0  //����ROM
#define  ReadROM    0x33  //��ROM
#define  MatchROM   0x55  //ƥ��ROM
#define  AlarmROM   0xEC  //�澯ROM

#define  StartConvert    0x44  //��ʼ�¶�ת�������¶�ת���ڼ����������0��ת�����������1
#define  ReadScratchpad  0xBE  //���ݴ�����9���ֽ�
#define  WriteScratchpad 0x4E  //д�ݴ������¶ȸ澯TH��TL
#define  CopyScratchpad  0x48  //���ݴ������¶ȸ澯���Ƶ�EEPROM���ڸ����ڼ����������0������������1
#define  RecallEEPROM    0xB8  //��EEPROM���¶ȸ澯���Ƶ��ݴ����У������ڼ����0��������ɺ����1
#define  ReadPower       0xB4  //����Դ�Ĺ��緽ʽ��0Ϊ������Դ���磻1Ϊ�ⲿ��Դ����

void ds18b20_init(void);
float ds18b20_read(void);
//unsigned short ds18b20_read(void);

#endif
