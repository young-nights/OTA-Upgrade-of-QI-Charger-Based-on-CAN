#ifndef IO_SPI
#define IO_SPI

#include "stm32f10x.h"

// 定义 SPI GPIO 引脚映射
#define SPI_GPIO            GPIOB  
#define SPI_SCK_PIN      GPIO_Pin_12
#define SPI_MISO_PIN    GPIO_Pin_14
#define SPI_MOSI_PIN    GPIO_Pin_15
/*
#define  SPI_SCK_SET()              GPIO_SetBits(SPI_GPIO,SPI_SCK_PIN)       // SPI_GPIO->BSRR |= SPI_SCK_PIN
#define  SPI_SCK_RESET()          GPIO_ResetBits(SPI_GPIO,SPI_SCK_PIN)     //SPI_GPIO->BRR |= SPI_SCK_PIN
#define  SPI_SCK_REVERSEL()    SPI_GPIO->ODR= SPI_SCK_PIN

#define  SPI_MOSI_SET()           GPIO_SetBits(SPI_GPIO,SPI_MOSI_PIN)     //SPI_GPIO->BSRR |= SPI_MOSI_PIN
#define  SPI_MOSI_RESET()       GPIO_ResetBits(SPI_GPIO,SPI_MOSI_PIN)  //SPI_GPIO->BRR |= SPI_MOSI_PIN
*/
#define  SPI_SCK_SET()              SPI_GPIO->BSRR |= SPI_SCK_PIN
#define  SPI_SCK_RESET()          SPI_GPIO->BRR |= SPI_SCK_PIN
#define  SPI_SCK_REVERSEL()    SPI_GPIO->ODR= SPI_SCK_PIN

#define  SPI_MOSI_SET()           SPI_GPIO->BSRR |= SPI_MOSI_PIN
#define  SPI_MOSI_RESET()       SPI_GPIO->BRR |= SPI_MOSI_PIN

#define  GET_MISO()        GPIO_ReadInputDataBit(SPI_GPIO, SPI_MISO_PIN)

// 设置 SPI 工作模式
#define SPI_Mode_0 0x00 // CPOL = 0, CPHA = 0 (时钟低空闲，数据第一个时钟沿捕获)
#define SPI_Mode_1 0x01 // CPOL = 0, CPHA = 1 (时钟低空闲，数据第二个时钟沿捕获)
#define SPI_Mode_2 0x02 // CPOL = 1, CPHA = 0 (时钟高空闲，数据第一个时钟沿捕获)
#define SPI_Mode_3 0x03 // CPOL = 1, CPHA = 1 (时钟高空闲，数据第二个时钟沿捕获)


uint8_t SPI_SendReceiveByte(uint8_t byte);
void SPI_MasterInit(void);

#endif 
