#include "IO_SPI.h"
#include "stm32f10x.h"
#include "delay.h"

void SPI_MasterInit(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
        
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);     //使能PB 端口时钟

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;          //推挽输出
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;         //IO口速度为50MHz

    GPIO_SetBits(SPI_GPIO,SPI_MOSI_PIN); 
    SPI_SCK_RESET();          
    GPIO_InitStructure.GPIO_Pin = SPI_MOSI_PIN|SPI_SCK_PIN;                
    GPIO_Init(SPI_GPIO, &GPIO_InitStructure);                                                  
                
    GPIO_InitStructure.GPIO_Pin = SPI_MISO_PIN;                 //MISO 上拉输入
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;   
    GPIO_Init(SPI_GPIO, &GPIO_InitStructure);                      

}

void _delay_(uint16_t t)
{
    while(t--);
}


uint8_t SPI_SendReceiveByte(uint8_t byte)
{
    uint8_t i = 0,   ret= 0;       
    for(i = 0; i < 8; i++)
    {
        SPI_SCK_SET();
        if(byte & 0x80) 
            SPI_MOSI_SET();
        else 
            SPI_MOSI_RESET();         
        byte <<= 1;       
                ret <<= 1;
        if(GET_MISO()) { ret |= 0x01; }
        SPI_SCK_RESET();
    }
 //   _delay_(10);
    return ret;
} 



