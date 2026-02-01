/*******************************************************************************************************************************//**
 *
 * @file		teclado.cpp
 * @brief		Descripcion del modulo
 * @date		24 jul. 2022
 * @author		Ing. Marcelo Trujillo
 *
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** INCLUDES
 **********************************************************************************************************************************/
#include "teclado.h"

/***********************************************************************************************************************************
 *** DEFINES PRIVADOS AL MODULO
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** MACROS PRIVADAS AL MODULO
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** TIPOS DE DATOS PRIVADOS AL MODULO
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** TABLAS PRIVADAS AL MODULO
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** VARIABLES GLOBALES PUBLICAS
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** VARIABLES GLOBALES PRIVADAS AL MODULO
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** IMPLEMENTACION DE LOS METODODS DE LA CLASE
 **********************************************************************************************************************************/

teclado::teclado ( vector <gpio *> &s , vector <gpio *> &r):m_scn(s),m_ret(r)
{
	// TODO Auto-generated constructor stub
	m_TeclaEstadoInicial = 0 ;
	m_TeclaCantidadDeRebotes  = NO_KEY;
	m_BufferTeclado = NO_KEY;
	m_MaxRebotes = 4 ;
	g_CallbackList.push_back( this );
}

teclado::~teclado() {
	// TODO Auto-generated destructor stub
}
///***********************************************************************************************************************************
// *** TABLAS PRIVADAS AL MODULO
// **********************************************************************************************************************************/
void teclado::callback ( void )
{
	uint8_t  tecla;
	tecla = TecladoHW ( );
	TecladoSW ( tecla );
}

uint8_t teclado::TecladoHW ( void )
{
	for ( uint8_t i = 0 ; i < m_scn.size() ; i++)
	{
		// Coloco la fila
		for ( uint8_t j = 0 ; j < m_scn.size() ; j++)
			m_scn[j]->SetPin() ; 	// pongo todos en estado neutro

		m_scn[i]->ClrPin(); 		// Activo el pin a chequear

		for ( uint8_t j = 0 ; j < m_ret.size() ; j++)
		{
			if ( m_ret[j]->GetPin( ) )
				return j + i * m_ret.size() ;
		}
	}
	return NO_KEY;
}


void teclado::TecladoSW ( uint8_t TeclaEstadoActual )
{
	if ( TeclaEstadoActual == NO_KEY )
	{
		// NoFue presionada o esta rebotando
		m_TeclaCantidadDeRebotes = 0;
		m_TeclaEstadoInicial = NO_KEY;
		return ;
	}

	if ( m_TeclaCantidadDeRebotes == 0 )
	{
		m_TeclaEstadoInicial = TeclaEstadoActual;
		m_TeclaCantidadDeRebotes ++;
		return;
	}

	if ( TeclaEstadoActual == m_TeclaEstadoInicial )
	{
		if ( m_TeclaCantidadDeRebotes < m_MaxRebotes )
		{
			m_TeclaCantidadDeRebotes ++;
			return;
		}

		if (m_TeclaCantidadDeRebotes == m_MaxRebotes )
		{
			m_BufferTeclado = TeclaEstadoActual;
			m_TeclaCantidadDeRebotes ++;
		}
	}
	else
	{
		m_TeclaCantidadDeRebotes = 0;
		m_TeclaEstadoInicial = NO_KEY;
	}
	return ;
}


uint8_t	teclado::GetKey( void )
{
	uint8_t key = m_BufferTeclado;
	m_BufferTeclado = NO_KEY;
	return key;
}
