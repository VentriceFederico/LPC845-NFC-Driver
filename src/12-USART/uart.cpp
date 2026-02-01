/*******************************************************************************************************************************//**
 *
 * @file		uart.cpp
 *
 **********************************************************************************************************************************/

#include "ColaCircular.h"

/***********************************************************************************************************************************
 *** INCLUDES
 **********************************************************************************************************************************/
#include "uart.h"
#include "swm.h"



/***********************************************************************************************************************************
 *** VARIABLES GLOBALES PRIVADAS AL MODULO
 **********************************************************************************************************************************/
static uart *g_usart[ 5 ];

/***********************************************************************************************************************************
 *** IMPLEMENTACION DE LOS METODODS DE LA CLASE
 **********************************************************************************************************************************/

#define TX_BUFF_SZ 32
#define RX_BUFF_SZ 128

uart::uart(	uint8_t  numUart, uint8_t portTx ,uint8_t pinTx , uint8_t portRx , uint8_t pinRx ,
            uint32_t baudrate, bits_datos_t BitsDeDatos, paridad_t paridad)
           : m_buffRx (RX_BUFF_SZ),m_buffTx (TX_BUFF_SZ)
{

	const struct {
		uint8_t ctrl;
		uint8_t iser;
		uint8_t pa_txd;
		uint8_t pa_rxd;
		USART_Type *pUart;
	} prxUart[]={
			{14, 3,PA_U0_TXD,PA_U0_RXD,USART0},
			{15, 4,PA_U1_TXD,PA_U1_RXD,USART1},
			{16, 5,PA_U2_TXD,PA_U2_RXD,USART2},
			{30,30,PA_U3_TXD,PA_U3_RXD,USART3},
			{31,31,PA_U4_TXD,PA_U4_RXD,USART4}
	};

	x_num=numUart;
	m_rxOverruns = 0;
	m_rxDropped = 0;
	m_rxErrors = 0;

//==================================================
// ---- configuracion de parametros intrinsecos ----
//    parametros predefinidos por número de UART
// -------------------------------------------------

	SYSCON->SYSAHBCLKCTRL0 |= ( 1 << prxUart[numUart].ctrl );		// habilita el clock

	SYSCON->PRESETCTRL0&= ~( 1 <<  prxUart[numUart].ctrl );			// se reinicia
	SYSCON->PRESETCTRL0|=  ( 1 <<  prxUart[numUart].ctrl );

	SYSCON->FCLKSEL[ numUart ] = 1;									// selecciona el clock ppal.

	PINASSIGN_Config(prxUart[numUart].pa_txd, portTx, pinTx);		// asignación de pines
	PINASSIGN_Config(prxUart[numUart].pa_rxd, portRx, pinRx);

	m_usart=prxUart[numUart].pUart; 								// USART seleccionada
	g_usart[ numUart ] = this ;	 	 //  interrupción

//==================================================
//---------- parametros de comunicación,
//                	interrupciones y otros -----------
// -------------------------------------------------

	m_flagTx = false ;			// flag de fin de Tx

	m_usart->CFG = ( BitsDeDatos << 2 )	| ( paridad << 4 );

	m_usart->BRG = 24000000UL / (baudrate * (m_usart->OSR + 1)) - 1;

	m_usart->INTENSET = DATAREADY;			// RX interrupcion

	NVIC->ISER[0] = ( 1 << prxUart[numUart].iser ); 	// habilitamos UART_IRQ

	// Asegura mayor prioridad para RX y reducir overruns en ráfagas.
	{
		const uint8_t irq = prxUart[numUart].iser;
		const uint8_t index = irq >> 2;
		const uint8_t shift = (irq & 0x03u) * 8u;
		const uint32_t mask = 0xFFu << shift;
		NVIC->IP[index] = (NVIC->IP[index] & ~mask) | (0u << shift);
	}

	m_usart->CFG |= ( 1 << 0 );			// habilitamos USART



}


uart::~uart() {
	// TODO Auto-generated destructor stub
}








void uart::Transmit ( char val)
{

	m_buffTx.push(val );

	if ( m_flagTx == false )
	{
		m_flagTx = true ;
		Tx_EnableInterupt (  );
	}

}



void uart::Transmit ( const char * msg)
{
	while ( *msg )
	{
		m_buffTx.push(*msg );
		msg++;

		if ( m_flagTx == false )
		{
			m_flagTx = true ;
			Tx_EnableInterupt (  );
		}
	}
}


void uart::Transmit ( void * msg , uint32_t n )
{
	for ( uint32_t i = 0 ; i < n ; i++ )
	{
		m_buffTx.push ( ((uint8_t*)msg)[ i ] );

		if ( m_flagTx == false )
		{
			m_flagTx = true ;
			Tx_EnableInterupt (  );
		}
	}
}

void uart::Transmit ( uint8_t * frame , uint8_t n )
{
	for ( uint32_t i = 0 ; i < n ; i++ )
	{
		m_buffTx.push ( ((uint8_t*)frame)[ i ] );

		if ( m_flagTx == false )
		{
			m_flagTx = true ;
			Tx_EnableInterupt (  );
		}
	}
}


void uart::Tx_EnableInterupt (  void )
{
	m_usart->INTENSET = (1 << 2);
}

void uart::Tx_DisableInterupt (  void )
{
	m_usart->INTENCLR = (1 << 2);
}



void uart::UART_IRQHandler ( void )
{
uint8_t datoRx;
uint32_t stat = m_usart->STAT ;

	// 1. GESTIÓN DE ERRORES DE HARDWARE (CRÍTICO)
    // Si hay Overrun, Framing o Parity error, DEBEMOS limpiarlos escribiendo 1.
    // Si no se limpian, la UART puede quedar en estado de error.

	if (stat & MASK_UART_ERROR) // MASK_UART_ERROR incluye OVERRUN, FRAMERR, PARITY, BREAK
		{
			m_rxErrors++;
			if (stat & MASK_OVERRUNINT) {
				m_rxOverruns++;
			}
			m_usart->STAT = MASK_UART_ERROR; // Escribir 1 para limpiar flags W1C

			// Opcional: Podrías contar errores aquí para debug
			// g_uartErrors++;

			// Continuamos para intentar salvar datos válidos si los hay
		}

	//--- Proceso de Recepcion
	while (stat & (DATAREADY))
	{
		datoRx = ( uint8_t ) m_usart->RXDAT;

		if (!m_buffRx.push(datoRx)) {
			m_rxDropped++;
		}
		stat = m_usart->STAT;
	}

	//--- Proceso de Transmision

	if( stat & TX_READY ){
		// Importante: Verificar si realmente tenemos activada la transmisión por soft
		if (m_flagTx) {
			uint8_t datoTx;
			if( m_buffTx.pop(datoTx) ) {
				m_usart->TXDAT = datoTx;
			}
			else {
				Tx_DisableInterupt();
				m_flagTx = false;
			}
		}
	}

}


char * uart::Receive ( char * msg , int n)
{

	if (m_buffRx.isNewLine()||m_buffRx.qtty()>= n-1)
	{
		int i;
		uint8_t dato;

		for (i=0;i<n-1;i++)
		{

			if ( m_buffRx.pop ( dato ) )
				msg [i] = dato;
			else
				break;


		}


		m_buffRx.newLineClear();	//Corresponde hacerlo siempre??

		msg[i]=0;
		return msg;
	}
return nullptr;
}






void UART0_IRQHandler ( void )
{
	g_usart[ 0 ]->UART_IRQHandler();
}
void UART1_IRQHandler ( void )
{
	g_usart[ 1 ]->UART_IRQHandler();
}
void UART2_IRQHandler ( void )
{
	g_usart[ 2 ]->UART_IRQHandler();
}
void PININT6_IRQHandler ( void )
{
	g_usart[ 3 ]->UART_IRQHandler();
}
void  PININT7_IRQHandler ( void )
{
	g_usart[ 4 ]->UART_IRQHandler();

}
