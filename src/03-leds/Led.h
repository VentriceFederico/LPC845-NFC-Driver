/*
 * Led.h
 *
 *  Created on: 14 jul. 2023
 *      Author: Marcelo
 */

#ifndef SRC_03_LEDS_LED_H_
#define SRC_03_LEDS_LED_H_

#include "callback.h"
#include "systick.h"
#include "gpio.h"

#define BLINK_DEFAULT		500



class Led:public Callback
{
	public:
		enum led_t { OFF , ON , BLINK } ;
	private:
		led_t 		m_Estado;
		uint8_t 	m_NumeroDeLed;
		uint32_t 	m_Ticks;
		uint32_t 	m_SemiPeriodo;
		bool 		m_flag ;
		void 		(*m_CallbackLeds)(uint8_t , led_t );
	public:
		Led( uint8_t Nled , void (*callbackLeds)(uint8_t , led_t ) = nullptr,
				uint32_t periodo = BLINK_DEFAULT);
		void On( void );
		void Off( void );
		void Blink( void );
		void callback( void );
		void SemiPeriodo( uint32_t periodo );
		Led& operator=( led_t estado );

		virtual ~Led();
};


#endif /* SRC_03_LEDS_LED_H_ */
