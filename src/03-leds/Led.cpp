/*
 * Led.cpp
 *
 *  Created on: 14 jul. 2023
 *      Author: Marcelo
 */

#include "Led.h"

Led::Led( uint8_t NumeroDeLed ,
		void (*CallbackLeds)( uint8_t , led_t ) ,
		uint32_t Semi_Periodo )
{
	m_NumeroDeLed = NumeroDeLed ;
	m_SemiPeriodo = Semi_Periodo ;
	m_Estado = OFF ;
	m_CallbackLeds = CallbackLeds ;
	m_Ticks = 0 ;
	m_flag = true ;

	g_CallbackList.push_back( this ) ;
	return ;
}

void Led::On(void)
{
	m_Estado = ON ;
	m_Ticks = 0 ;
	return ;
}

void Led::Off( void )
{
	m_Estado = OFF ;
	m_Ticks = 0 ;
	return ;
}

void Led::Blink( void )
{
	m_Estado = BLINK ;
	if ( !m_Ticks )
		m_Ticks = m_SemiPeriodo ;
	return ;
}

void Led::SemiPeriodo( uint32_t Semi_Periodo )
{
	m_SemiPeriodo = Semi_Periodo ;
	return ;
}

Led& Led::operator=( led_t Estado )
{
	m_Estado = Estado ;
	if ( Estado == BLINK )
	{
		if ( !m_Ticks )
				m_Ticks = m_SemiPeriodo ;
	}
	else
		m_Ticks = 0 ;

	return *this ;
}

void Led::callback(void)
{
	if ( m_CallbackLeds )
	{
		if ( m_Estado == BLINK )
		{
			m_Ticks --;

			if ( !m_Ticks )
			{
				m_Ticks = m_SemiPeriodo ;
				if( m_flag )
					m_CallbackLeds( m_NumeroDeLed , ON);
				else
					m_CallbackLeds( m_NumeroDeLed , OFF);
				m_flag = !m_flag;
			}
			return;
		}
		m_CallbackLeds( m_NumeroDeLed , m_Estado);
	}
	return ;
}


Led::~Led()
{
	// TODO Auto-generated destructor stub
}

