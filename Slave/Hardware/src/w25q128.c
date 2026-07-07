#include "w25q128.h"
#include "spi.h"

uint16_t W25QXX_TYPE=W25Q128;	//Ĭ����W25Q256

//4KbytesΪһ��Sector
//16������Ϊ1��Block
//W25Q128
//����Ϊ16M�ֽ�,����256��Block,4096��Sector
static void delayus(uint32_t nus)
{
    volatile uint32_t i;
    while (nus-- > 0)
    {
        i = 26;
        while (i-- > 0);
    }
}

void SPI1_SetSpeed(uint8_t SPI_BaudRatePrescaler)
{
    assert_param(IS_SPI_BAUDRATE_PRESCALER(SPI_BaudRatePrescaler));//�ж���Ч��
    __HAL_SPI_DISABLE(&hspi1);            				//�ر�SPI
    hspi1.Instance->CR1 &= 0XFFC7;          			//λ3-5���㣬�������ò�����
    hspi1.Instance->CR1 |= SPI_BaudRatePrescaler;	//����SPI�ٶ�
    __HAL_SPI_ENABLE(&hspi1);             				//ʹ��SPI
}

//SPI1 ��дһ���ֽ�
//TxData:Ҫд����ֽ�
//����ֵ:��ȡ�����ֽ�
uint8_t SPI1_ReadWriteByte(uint8_t TxData)
{
    uint8_t Rxdata;
    HAL_SPI_TransmitReceive(&hspi1, &TxData, &Rxdata, 1, 1000);
    return Rxdata;          		    //�����յ�������
}

//��ʼ��SPI FLASH��IO��
void W25QXX_Init(void)
{
    uint8_t temp;
//    GPIO_InitTypeDef GPIO_Initure;

//    __HAL_RCC_GPIOC_CLK_ENABLE();           //ʹ��GPIOBʱ��
//
//    //PB14
//    GPIO_Initure.Pin=GPIO_PIN_4;            //PB14
//    GPIO_Initure.Mode=GPIO_MODE_OUTPUT_PP;  //�������
//    GPIO_Initure.Pull=GPIO_PULLUP;          //����
//    GPIO_Initure.Speed=GPIO_SPEED_HIGH;     //����
//    HAL_GPIO_Init(GPIOC,&GPIO_Initure);     //��ʼ��

    W25QXX_CS1;			                //SPI FLASH��ѡ��
//	SPI1_Init();		   			        //��ʼ��SPI
    SPI1_SetSpeed(SPI_BAUDRATEPRESCALER_4); //����Ϊ21Mʱ��,����ģʽ
    W25QXX_TYPE=W25QXX_ReadID();	        //��ȡFLASH ID.
}

//��ȡW25QXX��״̬�Ĵ�����W25QXXһ����3��״̬�Ĵ���
//״̬�Ĵ���1��
//BIT7  6   5   4   3   2   1   0
//SPR   RV  TB BP2 BP1 BP0 WEL BUSY
//SPR:Ĭ��0,״̬�Ĵ�������λ,���WPʹ��
//TB,BP2,BP1,BP0:FLASH����д��������
//WEL:дʹ������
//BUSY:æ���λ(1,æ;0,����)
//Ĭ��:0x00
//״̬�Ĵ���2��
//BIT7  6   5   4   3   2   1   0
//SUS   CMP LB3 LB2 LB1 (R) QE  SRP1
//״̬�Ĵ���3��
//BIT7      6    5    4   3   2   1   0
//HOLD/RST  DRV1 DRV0 (R) (R) WPS ADP ADS
//regno:״̬�Ĵ����ţ���:1~3
//����ֵ:״̬�Ĵ���ֵ
uint8_t W25QXX_ReadSR(uint8_t regno)
{
    uint8_t byte=0,command=0;
    switch(regno)
    {
        case 1:
            command=W25X_ReadStatusReg1;    //��״̬�Ĵ���1ָ��
            break;
        case 2:
            command=W25X_ReadStatusReg2;    //��״̬�Ĵ���2ָ��
            break;
        case 3:
            command=W25X_ReadStatusReg3;    //��״̬�Ĵ���3ָ��
            break;
        default:
            command=W25X_ReadStatusReg1;
            break;
    }
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(command);            //���Ͷ�ȡ״̬�Ĵ�������
    byte=SPI1_ReadWriteByte(0Xff);          //��ȡһ���ֽ�
    W25QXX_CS1;                            //ȡ��Ƭѡ
    return byte;
}
//дW25QXX״̬�Ĵ���
void W25QXX_Write_SR(uint8_t regno,uint8_t sr)
{
    uint8_t command=0;
    switch(regno)
    {
        case 1:
            command=W25X_WriteStatusReg1;    //д״̬�Ĵ���1ָ��
            break;
        case 2:
            command=W25X_WriteStatusReg2;    //д״̬�Ĵ���2ָ��
            break;
        case 3:
            command=W25X_WriteStatusReg3;    //д״̬�Ĵ���3ָ��
            break;
        default:
            command=W25X_WriteStatusReg1;
            break;
    }
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(command);            //����дȡ״̬�Ĵ�������
    SPI1_ReadWriteByte(sr);                 //д��һ���ֽ�
    W25QXX_CS1;                            //ȡ��Ƭѡ
}
//W25QXXдʹ��
//��WEL��λ
void W25QXX_Write_Enable(void)
{
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_WriteEnable);   //����дʹ��
    W25QXX_CS1;                            //ȡ��Ƭѡ
}
//W25QXXд��ֹ
//��WEL����
void W25QXX_Write_Disable(void)
{
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_WriteDisable);  //����д��ָֹ��
    W25QXX_CS1;                            //ȡ��Ƭѡ
}

//��ȡоƬID
//����ֵ����:
//0XEF13,��ʾоƬ�ͺ�ΪW25Q80
//0XEF14,��ʾоƬ�ͺ�ΪW25Q16
//0XEF15,��ʾоƬ�ͺ�ΪW25Q32
//0XEF16,��ʾоƬ�ͺ�ΪW25Q64
//0XEF17,��ʾоƬ�ͺ�ΪW25Q128
//0XEF18,��ʾоƬ�ͺ�ΪW25Q256
uint16_t W25QXX_ReadID(void)
{
    uint16_t Temp = 0;
    W25QXX_CS0;
    SPI1_ReadWriteByte(0x90);//���Ͷ�ȡID����
    SPI1_ReadWriteByte(0x00);
    SPI1_ReadWriteByte(0x00);
    SPI1_ReadWriteByte(0x00);
    Temp|=SPI1_ReadWriteByte(0xFF)<<8;
    Temp|=SPI1_ReadWriteByte(0xFF);
    W25QXX_CS1;
    return Temp;
}
//��ȡSPI FLASH
//��ָ����ַ��ʼ��ȡָ�����ȵ�����
//pBuffer:���ݴ洢��
//ReadAddr:��ʼ��ȡ�ĵ�ַ(24bit)
//NumByteToRead:Ҫ��ȡ���ֽ���(���65535)
void W25QXX_Read(uint8_t* pBuffer,uint32_t ReadAddr,uint32_t NumByteToRead)
{
    uint32_t i;
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_ReadData);      //���Ͷ�ȡ����
    if(W25QXX_TYPE==W25Q256)                //�����W25Q256�Ļ���ַΪ4�ֽڵģ�Ҫ�������8λ
    {
        SPI1_ReadWriteByte((uint8_t)((ReadAddr)>>24));
    }
    SPI1_ReadWriteByte((uint8_t)((ReadAddr)>>16));   //����24bit��ַ
    SPI1_ReadWriteByte((uint8_t)((ReadAddr)>>8));
    SPI1_ReadWriteByte((uint8_t)ReadAddr);
    for(i=0; i<NumByteToRead; i++)
    {
        pBuffer[i]=SPI1_ReadWriteByte(0XFF);    //ѭ������
    }
    W25QXX_CS1;
}
//SPI��һҳ(0~65535)��д������256���ֽڵ�����
//��ָ����ַ��ʼд�����256�ֽڵ�����
//pBuffer:���ݴ洢��
//WriteAddr:��ʼд��ĵ�ַ(24bit)
//NumByteToWrite:Ҫд����ֽ���(���256),������Ӧ�ó�����ҳ��ʣ���ֽ���!!!
void W25QXX_Write_Page(uint8_t* pBuffer,uint32_t WriteAddr,uint16_t NumByteToWrite)
{
    uint16_t i;
    W25QXX_Write_Enable();                  //SET WEL
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_PageProgram);   //����дҳ����
    if(W25QXX_TYPE==W25Q256)                //�����W25Q256�Ļ���ַΪ4�ֽڵģ�Ҫ�������8λ
    {
        SPI1_ReadWriteByte((uint8_t)((WriteAddr)>>24));
    }
    SPI1_ReadWriteByte((uint8_t)((WriteAddr)>>16)); //����24bit��ַ
    SPI1_ReadWriteByte((uint8_t)((WriteAddr)>>8));
    SPI1_ReadWriteByte((uint8_t)WriteAddr);
    for(i=0; i<NumByteToWrite; i++)SPI1_ReadWriteByte(pBuffer[i]); //ѭ��д��
    W25QXX_CS1;                            //ȡ��Ƭѡ
    W25QXX_Wait_Busy();					   //�ȴ�д�����
}
//�޼���дSPI FLASH
//����ȷ����д�ĵ�ַ��Χ�ڵ�����ȫ��Ϊ0XFF,�����ڷ�0XFF��д������ݽ�ʧ��!
//�����Զ���ҳ����
//��ָ����ַ��ʼд��ָ�����ȵ�����,����Ҫȷ����ַ��Խ��!
//pBuffer:���ݴ洢��
//WriteAddr:��ʼд��ĵ�ַ(24bit)
//NumByteToWrite:Ҫд����ֽ���(���65535)
//CHECK OK
void W25QXX_Write_NoCheck(uint8_t* pBuffer,uint32_t WriteAddr,uint32_t NumByteToWrite)
{
    uint16_t pageremain;
    pageremain=256-WriteAddr%256; //��ҳʣ����ֽ���
    if(NumByteToWrite<=pageremain)pageremain=NumByteToWrite;//������256���ֽ�
    while(1)
    {
        W25QXX_Write_Page(pBuffer,WriteAddr,pageremain);
        if(NumByteToWrite==pageremain)break;//д�������
        else //NumByteToWrite>pageremain
        {
            pBuffer+=pageremain;
            WriteAddr+=pageremain;

            NumByteToWrite-=pageremain;			  //��ȥ�Ѿ�д���˵��ֽ���
            if(NumByteToWrite>256)pageremain=256; //һ�ο���д��256���ֽ�
            else pageremain=NumByteToWrite; 	  //����256���ֽ���
        }
    };
}
//дSPI FLASH
//��ָ����ַ��ʼд��ָ�����ȵ�����
//�ú�������������!
//pBuffer:���ݴ洢��
//WriteAddr:��ʼд��ĵ�ַ(24bit)
//NumByteToWrite:Ҫд����ֽ���(���65535)
uint8_t W25QXX_BUFFER[SECTOR_SIZE];
void W25QXX_Write(uint8_t* pBuffer,uint32_t WriteAddr,uint32_t NumByteToWrite)
{
    uint32_t secpos;
    uint16_t secoff;
    uint16_t secremain;
    uint16_t i;
    uint8_t * W25QXX_BUF;
    W25QXX_BUF=W25QXX_BUFFER;
    secpos=WriteAddr/SECTOR_SIZE;//������ַ
    secoff=WriteAddr%SECTOR_SIZE;//�������ڵ�ƫ��
    secremain=SECTOR_SIZE-secoff;//����ʣ��ռ��С
// 	printf("ad:%X,nb:%X\r\n",WriteAddr,NumByteToWrite);//������
    if(NumByteToWrite<=secremain)secremain=NumByteToWrite;//������4096���ֽ�
    while(1)
    {
        W25QXX_Read(W25QXX_BUF,secpos*SECTOR_SIZE,SECTOR_SIZE);//������������������
        for(i=0; i<secremain; i++) //У������
        {
            if(W25QXX_BUF[secoff+i]!=0XFF)break;//��Ҫ����
        }
        if(i<secremain)//��Ҫ����
        {
            W25QXX_Erase_Sector(secpos);//�����������
            for(i=0; i<secremain; i++)	 //����
            {
                W25QXX_BUF[i+secoff]=pBuffer[i];
            }
            W25QXX_Write_NoCheck(W25QXX_BUF,secpos*SECTOR_SIZE,SECTOR_SIZE);//д����������
        }
        else W25QXX_Write_NoCheck(pBuffer,WriteAddr,secremain); //д�Ѿ������˵�,ֱ��д������ʣ������.
        if(NumByteToWrite==secremain)break;//д�������
        else//д��δ����
        {
            secpos++;//������ַ��1
            secoff=0;//ƫ��λ��Ϊ0

            pBuffer+=secremain;  //ָ��ƫ��
            WriteAddr+=secremain;//д��ַƫ��
            NumByteToWrite-=secremain;				//�ֽ����ݼ�
            if(NumByteToWrite>SECTOR_SIZE)secremain=SECTOR_SIZE;	//��һ����������д����
            else secremain=NumByteToWrite;			//��һ����������д����
        }
    };
}
//��������оƬ
//�ȴ�ʱ�䳬��...
void W25QXX_Erase_Chip(void)
{
    W25QXX_Write_Enable();                  //SET WEL
    W25QXX_Wait_Busy();
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_ChipErase);        //����Ƭ��������
    W25QXX_CS1;                            //ȡ��Ƭѡ
    W25QXX_Wait_Busy();   				   //�ȴ�оƬ��������
}
//����һ������
//Dst_Addr:������ַ ����ʵ����������
//����һ������������ʱ��:150ms
void W25QXX_Erase_Sector(uint32_t Dst_Addr)
{
    //����falsh�������,������
    //printf("fe:%x\r\n",Dst_Addr);
    Dst_Addr*=SECTOR_SIZE;
    W25QXX_Write_Enable();                  //SET WEL
    W25QXX_Wait_Busy();
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_SectorErase);   //������������ָ��
    if(W25QXX_TYPE==W25Q256)                //�����W25Q256�Ļ���ַΪ4�ֽڵģ�Ҫ�������8λ
    {
        SPI1_ReadWriteByte((uint8_t)((Dst_Addr)>>24));
    }
    SPI1_ReadWriteByte((uint8_t)((Dst_Addr)>>16));  //����24bit��ַ
    SPI1_ReadWriteByte((uint8_t)((Dst_Addr)>>8));
    SPI1_ReadWriteByte((uint8_t)Dst_Addr);
    W25QXX_CS1;                            //ȡ��Ƭѡ
    W25QXX_Wait_Busy();   				    //�ȴ��������
}
//�ȴ�����
void W25QXX_Wait_Busy(void)
{
    while((W25QXX_ReadSR(1)&0x01)==0x01);   // �ȴ�BUSYλ���
}
//������ģʽ
void W25QXX_PowerDown(void)
{
    W25QXX_CS0;                            //ʹ������
    SPI1_ReadWriteByte(W25X_PowerDown);     //���͵������
    W25QXX_CS1;                            //ȡ��Ƭѡ
    delayus(3);                            //�ȴ�TPD
}
//����
void W25QXX_WAKEUP(void)
{
    W25QXX_CS0;                                //ʹ������
    SPI1_ReadWriteByte(W25X_ReleasePowerDown);  //  send W25X_PowerDown command 0xAB
    W25QXX_CS1;                                //ȡ��Ƭѡ
    delayus(3);                                //�ȴ�TRES1
}
