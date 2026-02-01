/*******************************************************************************************************************************//**
 *
 * @file		myuart.h
 *
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** MODULO
 **********************************************************************************************************************************/

#ifndef _MY_USART_UART_H_
#define _MY_USART_UART_H_

/***********************************************************************************************************************************
 *** INCLUDES GLOBALES
 **********************************************************************************************************************************/

#include "LPC845.h"
#include "uart.h"


#define SZ_MYBUFF 20
class myuart: public uart
{
private:
	uint8_t m_buff[SZ_MYBUFF];		// no haría falta 2 buffers???
	int m_state;
	int m_idx;
	int verificoTrama();
public:
	myuart(uint8_t num,uint8_t portTx , uint8_t pinTx , uint8_t portRx , uint8_t pinRx ,
			uint32_t baudrate=9600, bits_datos_t BitsDeDatos=ocho_bits, paridad_t paridad=NoParidad):uart(num,portTx ,pinTx , portRx , pinRx , baudrate, BitsDeDatos, paridad)
	{
		m_state=0;
		m_idx=0;
	}
	int myReceive ( );
	friend int analisis (myuart *ua, int&cd,int &val);
};

#endif /* _MY_USART_UART_H_ */


