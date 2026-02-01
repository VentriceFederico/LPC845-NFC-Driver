/*******************************************************************************************************************************//**
 *
 * @file		inicializar.cpp
 * @brief		Descripcion del modulo
 * @date		5 jul. 2022
 * @author		Ing. Marcelo Trujillo
 *
 **********************************************************************************************************************************/

/***********************************************************************************************************************************
 *** INCLUDES
 **********************************************************************************************************************************/

#include "inicializarInfotronic.h"

/***********************************************************************************************************************************
 *** DEFINES PRIVADOS AL MODULO
 **********************************************************************************************************************************/
#define KIT2022
#define FREQ_SYSTICK	(1)		// en ms

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
 *** OBJETOS GLOBALES PUBLICOS
 *********************************************************************************************************************************/
list <Callback*> g_CallbackList;

//!< GPIO ENTRADAS SALIDAS DE PROPOSITO GENERAL
gpio g_out0( gpio::port1 , 0 , gpio::pushpull ,  gpio::output , gpio::high );
gpio g_out1( gpio::port1 , 1 , gpio::pushpull ,  gpio::output , gpio::high );
gpio g_out2( gpio::port1 , 2 , gpio::pushpull ,  gpio::output , gpio::high );

//!<	ENTRADAS DIGITALES
#ifdef KIT2022
gpio g_in0( gpio::port0 , 4 , gpio::pushpull ,  gpio::input , gpio::low );
gpio g_in1( gpio::port0 , 26 , gpio::pushpull ,  gpio::input , gpio::low );
#else
gpio g_in0( gpio::port0 , 4 , gpio::pullup ,  gpio::input , gpio::low );
gpio g_in1( gpio::port0 , 3 , gpio::pullup ,  gpio::input , gpio::high);
#endif

// DISPLAY
vector <gpio *> bcd;
vector <gpio *> cnt;
vector <gruposdedigitos *> grupos;
I4017 *i4017 ;
I4511 *i4511 ;
uint8_t	PosicionRelativa[] = {2,1,0,5,4,3};
display7Segmentos *Display;

//!<	TECLADO
vector <gpio *> scn;
vector <gpio *> ret;
teclado *Teclado;

#ifdef KIT2022

	gpio *bcdA = (new gpio ( gpio::port0 ,  20 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *bcdB = (new gpio ( gpio::port0 ,  23 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *bcdC = (new gpio ( gpio::port0 ,  22 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *bcdD = (new gpio ( gpio::port0 ,  21 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *dp   = (new gpio ( gpio::port0 ,  12 , gpio::pushpull ,  gpio::output , gpio::high ));

#else
	gpio *bcdA = (new gpio ( gpio::port0 ,  23 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *bcdB = (new gpio ( gpio::port0 ,  22 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *bcdC = (new gpio ( gpio::port0 ,  21 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *bcdD = (new gpio ( gpio::port0 ,  20 , gpio::pushpull ,  gpio::output , gpio::high ));
	gpio *dp   = (new gpio ( gpio::port0 ,  12 , gpio::pushpull ,  gpio::output , gpio::high ));

#endif

// barrido

gpio *rst = (new gpio ( gpio::port0 ,  18 , gpio::pushpull ,  gpio::output , gpio::high )); // RST
gpio *clk = (new gpio ( gpio::port0 ,  19 , gpio::pushpull ,  gpio::output , gpio::high )); // CLK

#ifdef KIT2022
	gpio *scn0 = new gpio ( gpio::port0 ,  27 , gpio::pushpull ,  gpio::output , gpio::high );
	gpio *scn1 = new gpio ( gpio::port0 ,  28 , gpio::pushpull ,  gpio::output , gpio::high );

	gpio *ret0 = new gpio ( gpio::port0 ,  8 , gpio::pullup ,  gpio::input , gpio::low );
	gpio *ret1 = new gpio ( gpio::port0 , 31 , gpio::pullup ,  gpio::input , gpio::low );
	gpio *ret2 = new gpio ( gpio::port0 , 30 , gpio::pullup ,  gpio::input , gpio::low );

#else
	gpio *scn0 = new gpio ( gpio::port0 ,  27 , gpio::pushpull ,  gpio::output , gpio::high );
	gpio *scn1 = new gpio ( gpio::port0 ,  28 , gpio::pushpull ,  gpio::output , gpio::high );

	gpio *ret0 = new gpio ( gpio::port0 , 26 , gpio::pullup ,  gpio::input , gpio::low );
	gpio *ret1 = new gpio ( gpio::port0 , 15 , gpio::pullup ,  gpio::input , gpio::low );
	gpio *ret2 = new gpio ( gpio::port0 ,  8 , gpio::pullup ,  gpio::input , gpio::low );

#endif

//!<	LCD
gpio *d4 = new gpio ( gpio::port0 ,  9 , gpio::pushpull ,  gpio::output , gpio::high );
gpio *d5 = new gpio ( gpio::port0 , 10 , gpio::pushpull ,  gpio::output , gpio::high );
gpio *d6 = new gpio ( gpio::port0 , 11 , gpio::pushpull ,  gpio::output , gpio::high );
gpio *d7 = new gpio ( gpio::port0 , 13 , gpio::pushpull ,  gpio::output , gpio::high );
gpio *rs = new gpio ( gpio::port0 ,  1 , gpio::pushpull ,  gpio::output , gpio::high );
gpio *en = new gpio ( gpio::port0 , 14 , gpio::pushpull ,  gpio::output , gpio::high );

vector <gpio *> BusLcd;
Lcd *lcd;

vector <gpio *> VectorDeLeds;

Led L1( 0 , Callback_Leds_gpio , 200 ) ;
Led L2( 1 , Callback_Leds_gpio , 100 ) ;
Led L3( 2 , Callback_Leds_gpio ) ;

uart *uartX ;

// ADC
//adc canal1( 1 , adc::Promedio );
//adc canal1( 1 , TablaNTC );
//adc canal1( 1  );

// Termistor a GND
static const int32_t Tabla_NTC[] = { 971,956,938,917,891,862,828,790,749,704,657,609,560,512,464,419,381,337,300,267,237,210,186,165,146,130,115,102,91,81,72,64,58,0};

/***********************************************************************************************************************************
 *** VARIABLES GLOBALES PUBLICAS
 **********************************************************************************************************************************/
void systick_callback ( void  )
{
	for (Callback* q : g_CallbackList )
		q->callback();
}

void Callback_Leds_gpio ( uint8_t NumeroDeLed , Led::led_t Estado )
{
	if ( NumeroDeLed >= 0 && NumeroDeLed < VectorDeLeds.size( ) )
	{
		if ( Estado )
			VectorDeLeds[ NumeroDeLed ]->SetPin();
		else
			VectorDeLeds[ NumeroDeLed ]->ClrPin();
	}
}


int32_t TablaNTC( int32_t dato)
{
	uint8_t j;
	dato /= 4;// lo paso a 10 bits

	for(j = 0 ; j < 33; j++)
	{
		if( dato > Tabla_NTC[j] )
			break;
	}

	dato = (int) ( ( ( (unsigned long)( Tabla_NTC[j-1] - dato ) * 50000l ) / (Tabla_NTC[j-1] - Tabla_NTC[j]) ) / 10000 );
	dato = dato + 5 * (j - 1) - 40;

	return dato;
}

int32_t TablaPote( int32_t dato )
{
	dato = (( dato * 33000 ) / 4096)/1000;
	return dato;
}
void InicializarInfotronic ( void )
{
	Inicializar_PLL( );
	//Inic_OSC();
	// ## LEDS #######################################################################################
	VectorDeLeds.push_back( &g_out0 ) ;
	VectorDeLeds.push_back( &g_out1 ) ;
	VectorDeLeds.push_back( &g_out2 ) ;

	// ## TECLADO ####################################################################################
	scn.push_back( scn0 );
	scn.push_back( scn1 );

	ret.push_back( ret0 );
	ret.push_back( ret1 );
	ret.push_back( ret2 );

	Teclado = new teclado ( scn , ret );
	// ###############################################################################################

	// ## DISPLAY 7 SEGMENTOS ########################################################################
	// segmentos - Integrado 4051 --------------------------------------------------------------------
	bcd.push_back( bcdA );
	bcd.push_back( bcdB );
	bcd.push_back( bcdC );
	bcd.push_back( bcdD );
	bcd.push_back( dp );
	i4511 = new I4511( bcd );

	// Driver de digitos - Integrado 4017 ------------------------------------------------------------
	cnt.push_back( rst );
	cnt.push_back( clk );
	i4017 = new I4017( cnt , 6 );

	// Configuracion del formato del display ---------------------------------------------------------
	//                                    posicion relativa    cantidad
	grupos.push_back(new gruposdedigitos (       0 ,              3     ));
	grupos.push_back(new gruposdedigitos (       3 ,              3     ));
	//  display de 7 segmentos -----------------------------------------------------------------------
	//                      		  digitos
	//                               agrupados segmentos   barrido    correccion       codificacion
	Display = new display7Segmentos ( grupos ,  i4511 ,      i4017 , PosicionRelativa , digito::BCD );

	// ## DISPLAY LCD ###############################################################################
	BusLcd.push_back( d4 );
	BusLcd.push_back( d5 );
	BusLcd.push_back( d6 );
	BusLcd.push_back( d7 );
	BusLcd.push_back( rs );
	BusLcd.push_back( en );

	// salida a RS232 - terminal MCUXpresso

/*
	 uartX = new uart(
						4 ,					// uart seleccionada UART4
						0 , 				// PortTx
						16, 				// Pin Tx
						0 ,					// PortRx
						17, 				// Pin Rx
						115200 ,			// baudios
						uart::ocho_bits ,	// bits de datos
						uart::NoParidad	// tipo de paridad
						);
*/


	// ###############################################################################################
	SysTick_CallBack_Install( systick_callback );

	// por precaucion lo ultimo es el systick
	Inicializar_SysTick( FREQ_SYSTICK );
	lcd = new Lcd(BusLcd);

}
