/*
 * lcd.cpp
 *
 *  Created on: 11 jul. 2023
 *      Author: Marcelo
 */

#include <Lcd.h>


Lcd::~Lcd() {
	// TODO Auto-generated destructor stub
}

void Lcd::Set( const char *string , uint8_t line , uint8_t pos )
{
	uint8_t i ;
	switch( line )
	{
		case 0:
			Push( pos + ( 0x80 | LINE_1_ADDRESS ), LCD_CONTROL );
			break;
		case 1:
			Push( pos + ( 0x80 | LINE_2_ADDRESS ), LCD_CONTROL );
			break;
	}
	for(i = 0; string[ i ] != '\0'; i++ )
		Push( string[ i ], LCD_DATA );
}

Lcd::Lcd(const vector <gpio*> &bus):m_bus( bus )
{
	uint8_t i;
	m_lcdInxIn = 0;
	m_lcdInxOut = 0;;
	m_lcdDataCount = 0;

	g_CallbackList.push_back( this );

	m_bus[LCD_EN]->ClrPin();		// EN = 0

	m_lcdDelay = 2000;				// Demora inicial
	while (m_lcdDelay);

	for( i = 0 ; i < 3 ; i++ )
	{
		// Ponemos el dato en el bus
		m_bus[LCD_D4]->SetPin();	// Configuracion en 8 bits
		m_bus[LCD_D5]->SetPin();
		m_bus[LCD_D6]->ClrPin();
		m_bus[LCD_D7]->ClrPin();

		m_bus[LCD_RS]->ClrPin();	// RS = 0 => comando
		m_bus[LCD_EN]->SetPin();	// EN = 1 => Habilito => entreo el dato
		m_bus[LCD_EN]->ClrPin();	// EN = 0

		m_lcdDelay = 1000;
		while (m_lcdDelay);
	}

	// Configuracion en 4 bits
	m_bus[LCD_D4]->ClrPin();	// Configuracion en 4 bits
	m_bus[LCD_D5]->SetPin();
	m_bus[LCD_D6]->ClrPin();
	m_bus[LCD_D7]->ClrPin();

	m_bus[LCD_RS]->ClrPin();
	m_bus[LCD_EN]->SetPin();
	m_bus[LCD_EN]->ClrPin();

	m_lcdDelay = 1000;	// Demora inicial
	while (m_lcdDelay);

	// A partir de aca el LCD pasa a 4 bits !!!
	// DL = 0: 4 bits de datos
	// N  = 1 : 2 lineas
	// F  = 0 : 5x7 puntos
	Push( 0x28 , LCD_CONTROL );
	// D = 0 : display OFF
	// C = 0 : Cursor OFF
	// B = 0 : Blink OFF
	Push( 0x08 , LCD_CONTROL);
	// I/D = 1 : Incrementa puntero
	// S   = 0 : NO Shift Display
	Push( 0x06 , LCD_CONTROL);
	// Activo el LCD y listo para usar !
	// D = 1 : display ON
	// C = 0 : Cursor OFF
	// B = 0 : Blink OFF
	Push( 0x0C , LCD_CONTROL);
	// clear display
	Push( 0x01 , LCD_CONTROL);
	// Calculamos el tiempo necesario para vaciar el buffer + ejecutar el comando.
	// Hemos hecho unos 5 Pushes finales. Cada Push son 2 nibbles = 2ms de transmisión.
	// 5 * 2ms = 10ms de transmisión.
	// + 2ms que tarda el LCD en ejecutar el "Clear Display".
	// Total seguro: ~15-20ms.

	m_lcdDelay = 20; // 20 ticks de SysTick (20ms)

	// Bloqueamos aquí. El SysTick seguirá llamando al callback(),
	// vaciando el buffer y enviando los comandos al LCD mientras esperamos.
	while(m_lcdDelay);
}


int8_t Lcd::Push( uint8_t data , uint8_t control )
{


	m_lcdBuffer [ m_lcdInxIn ] = ( data >> 4 ) & 0x0f;
	if ( control == LCD_CONTROL )
		m_lcdBuffer [ m_lcdInxIn ] |= 0x80;	// Conrol

	m_lcdInxIn ++;
	m_lcdInxIn %= LCD_BUFFER_SIZE;

	m_lcdBuffer [ m_lcdInxIn ] = data & 0x0f;
	if ( control == LCD_CONTROL )
		m_lcdBuffer [ m_lcdInxIn ] |= 0x80; // Conrol

	m_lcdDataCount += 2;

	m_lcdInxIn ++;
	m_lcdInxIn %= LCD_BUFFER_SIZE;

	return 0;
}


int16_t Lcd::Pop( void )
{
	uint8_t dato;

	if ( m_lcdInxOut == m_lcdInxIn )
		return -1;

	dato = m_lcdBuffer [ m_lcdInxOut ] ;

	m_lcdDataCount --;

	m_lcdInxOut ++;
	m_lcdInxOut %= LCD_BUFFER_SIZE;

	return dato;
}


void Lcd::callback( void )
{
	int16_t data;

	if ( m_lcdDelay )
		m_lcdDelay--;

	if ( ( data = Pop( ) ) == -1 )
		return;

	for (uint8_t i = 0 ; i < 4 ; i++ )
	{
		if ( ( ((uint8_t) data) >> i ) & 0x01 )
			m_bus[i]->SetPin();
		else
			m_bus[i]->ClrPin();
	}

	if( ( (uint8_t) data ) & 0x80 )
		m_bus[LCD_RS]->ClrPin();		// Control
	else
		m_bus[LCD_RS]->SetPin();		// Dato

	// Demora para asegurar el pulso de EN
//	m_bus[LCD_EN]->SetPin();
//	m_bus[LCD_EN]->SetPin();
//	m_bus[LCD_EN]->SetPin();
//	m_bus[LCD_EN]->SetPin();
//	m_bus[LCD_EN]->SetPin();
//	m_bus[LCD_EN]->SetPin();
	m_bus[LCD_EN]->SetPin();
	m_bus[LCD_EN]->SetPin();

//	m_bus[LCD_EN]->ClrPin();
//	m_bus[LCD_EN]->ClrPin();
//	m_bus[LCD_EN]->ClrPin();
//	m_bus[LCD_EN]->ClrPin();
//	m_bus[LCD_EN]->ClrPin();
//	m_bus[LCD_EN]->ClrPin();
	m_bus[LCD_EN]->ClrPin();
	m_bus[LCD_EN]->ClrPin();

}


void Lcd::Clear(){
	Push(0x01, LCD_CONTROL);
}
