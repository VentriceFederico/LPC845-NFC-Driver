/*******************************************************************************************************************************//**
 *
 * @file		uart.cpp
 *
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** INCLUDES
 **********************************************************************************************************************************/
#include "myuart.h"
#include "swm.h"
#if 0
- Agregar a la clase UART un método preparado para recibir strings, considerando que los terminadores pueden ser '\n' '\0'. y en algunos casos también '\r'.
- Hacer una clase derivada que este preparada para recibir una trama específica. A modo de ejemplo, implemetar la siguiente trama:
La trama será en modo ASCII.
Esta inicia con el caracter '#' y finaliza con '$'
El 2do caracter es el comando a ejecutar. Dependiendo del tipo de comando, es como continua la trama:
Comandos ON/OFF
- 'A' - accion sobre led azul
- 'R' - accion sobre led rojo
- 'V' - accion sobre led verde
Accion:
- ON - enciende el led
- OFF - apaga el led
- TILT - led titilando

Comando Numerico
- "D1-" - accion sobre el display 7 segmentos - número 1
- "D2-" - accion sobre el display 7 segmentos - número 2
- "DX-" - accion sobre el display 7 segmentos - completo
- "TX-" - Define el tiempo en ms del parpadeo de los led (señal cuadrada)

Accion:
- "xx..xx" - digitos en ASCII que conforman el valor numerico. Cantidad de digitos/finalizador del número a definir por el progrmador

Además, antes del caracter de finalización, se debe agregar uno o más caracteres de Verificacion. Esto significa que estos caracteres deben validar el comando + acción recibida.

#endif
/***********************************************************************************************************************************
 *** VARIABLES GLOBALES PRIVADAS AL MODULO
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** IMPLEMENTACION DE LOS METODODS DE LA CLASE
 **********************************************************************************************************************************/

/* Condicion de diseño -
 *  Los caracteres de inicio y fin son exclusivos para esta tarea.
 *  La verificación son dos digitos Hexa en ASCII y representan el valor que queda dentro de un byte sumando todos los
 *  valores desde # hasta el byte previo al los bytes de verificación

*/


int myuart::verificoTrama ( )
{

	// Se verifica cantidad mínima y máxima de bytes

	// minima #AONxx          6 digitos
	// maxima #DX-ddddddxx   12 digitos

	if (m_idx<5 || m_idx>12)
		return -10;	// por que -10?

	uint8_t c1,c2;
	int i=0;
	c1=0;
	for ( i=0; i <m_idx-2 ; i++)
		c1+=m_buff[i];

	c2= (m_buff[i]>='0' && m_buff[i]<='9')? m_buff[i]-'0':m_buff[i]-'A'+10;
	c2<<=4;
	i++;
	c2+= (m_buff[i]>='0' && m_buff[i]<='9')? m_buff[i]-'0':m_buff[i]-'A'+10;

	if (c1==c2)
		return 1;

return -11;

}


int myuart::myReceive ( )
{
uint8_t dato;

	if (  !m_buffRx.pop ( dato ) )
		return 0;

	switch (m_state)
	{
	case 0:			// inicio de trama.
		if (dato=='#')
		{
			m_state=1;
			m_buff[0]=dato; //en rigor se podria omitir.
			m_idx=1;
		}
		break;

	case 1:	// inicio de trama.
		if (dato=='#')	// Nuevo inicio de trama ??
		{
			m_state=1;
			m_buff[0]=dato;
			m_idx=1;
			return -2;	// por que -2???
		}

		if (dato=='$')	// fin de trama.
		{				// Habria que insertarlo en el Array???
			m_state=0;
			return verificoTrama();
		}

		m_buff[m_idx]=dato;
		m_idx++;



		if (m_idx >= SZ_MYBUFF)  // desborde
		{
			m_state=0;
			return -3;
		}

		break;

	default:	// Hace falta la MdE???
		m_state=0;
		return -10;


	}
return 0;
}

