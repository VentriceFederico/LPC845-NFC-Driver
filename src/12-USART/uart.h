/*******************************************************************************************************************************//**
 *
 * @file		uart.h
 *
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** MODULO
 **********************************************************************************************************************************/

#ifndef I1_USART_UART_H_
#define I1_USART_UART_H_

/***********************************************************************************************************************************
 *** INCLUDES GLOBALES
 **********************************************************************************************************************************/
//#include <12-USART/ComunicacionAsincronica.h>
#include "LPC845.h"
#include "ColaCircular.h"

//#include "tipos.h"

/***********************************************************************************************************************************
 *** DEFINES GLOBALES
 **********************************************************************************************************************************/

#define TX_READY 			(1<< 2)		//0x0004
#define DATAREADY			(1<< 0)		//0x0001

#define MASK_BREAK			(1<<10)		//0x0400
#define MASK_BREAKINT		(1<<11)		//0x0800
#define MASK_OVERRUNINT  	(1<< 8)		//0x0100
#define MASK_FRAMERRINT		(1<<13)		//0x2000
#define MASK_PARITYERRINT	(1<<14)		//0x4000

#define MASK_UART_ERROR (MASK_OVERRUNINT | MASK_FRAMERRINT | MASK_PARITYERRINT | MASK_BREAKINT)


/***********************************************************************************************************************************
 *** MACROS GLOBALES
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** TIPO DE DATOS GLOBALES
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** VARIABLES GLOBALES
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** IMPLANTACION DE UNA CLASE
 **********************************************************************************************************************************/
#if defined (__cplusplus)
    extern "C" {
    void UART0_IRQHandler(void);
    void UART1_IRQHandler(void);
    void UART2_IRQHandler(void);
    void PININT6_IRQHandler(void); // UART3
    void PININT7_IRQHandler(void); // UART4
    }
#endif



class uart
{
protected:
		USART_Type * m_usart;
		//Se definio un tamanio fijo
		ColaCircular <uint8_t, 256> m_buffRx;
		ColaCircular <uint8_t, 256> m_buffTx;

		volatile bool m_flagTx;
		uint8_t x_num;

		// Contadores de diagnóstico
		volatile uint32_t m_rxOverruns;
		volatile uint32_t m_rxDropped;
		volatile uint32_t m_rxErrors;

public:
		typedef enum  { NoParidad = 0, par = 2, impar = 3 } paridad_t;
		enum bits_datos_t { siete_bits , ocho_bits };

		// Nota: Quitamos los parámetros de tamaño de buffer del constructor
		uart(uint8_t num, uint8_t portTx, uint8_t pinTx, uint8_t portRx, uint8_t pinRx,
			 uint32_t baudrate = 9600, bits_datos_t BitsDeDatos = ocho_bits, paridad_t paridad = NoParidad);

		~uart();

		bool Transmit(const char * msg);
		bool Transmit(const void * msg, uint32_t n);
		bool Transmit(const uint8_t * frame, uint8_t n);
		bool Transmit(char val);

		bool Receive(uint8_t & val) { return m_buffRx.pop(val); }
		char *Receive(char * msg, int n);

		void setStringMode(newLineType_t mode) { m_buffRx.enableNewLine(mode); }

		void UART_IRQHandler(void);

		uint32_t getRxOverruns() const { return m_rxOverruns; }
		uint32_t getRxDropped() const { return m_rxDropped; }
		uint32_t getRxErrors() const { return m_rxErrors; }
		void clearRxBuffer() { m_buffRx.clear(); }
		int getTxPending() const { return m_buffTx.qtty(); }

private:
		inline void Tx_EnableInterupt(void) { m_usart->INTENSET = (1 << 2); }
		inline void Tx_DisableInterupt(void) { m_usart->INTENCLR = (1 << 2); }
};

#endif /* I1_USART_UART_H_ */
