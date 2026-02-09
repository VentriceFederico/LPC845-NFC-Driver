/*
 * GestorAcceso.h
 *
 *  Created on: 5 feb. 2026
 *      Author: ventr
 */

#ifndef SRC_14_GESTORACCESO_GESTORACCESO_H_
#define SRC_14_GESTORACCESO_GESTORACCESO_H_

#include "LPC845.h"
#include <cstring>
#include "uart.h"

class GestorAcceso {
private:
	uart* m_uartPC;

	void formatUID(uint8_t* uid, uint8_t len, char* buffer); //Convierte UID a Texto

public:
	GestorAcceso(uart* uartPC);
	virtual ~GestorAcceso();

	void 	enviarSolicitudAcceso(uint8_t* uid, uint8_t len);
	void 	enviarSolicitudGestion(uint8_t* uid, uint8_t len);
	bool 	leerRespuesta(char& respuesta);
	bool 	esAdmin(uint8_t* uid, uint8_t len);
};

#endif /* SRC_14_GESTORACCESO_GESTORACCESO_H_ */
