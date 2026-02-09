/*
 * GestorAcceso.cpp
 *
 *  Created on: 5 feb. 2026
 *      Author: ventr
 */

#include <14-GestorAcceso/GestorAcceso.h>

GestorAcceso::GestorAcceso (uart* uartPC) {
	m_uartPC = uartPC;
}

GestorAcceso::~GestorAcceso() {
	// TODO Auto-generated destructor stub
}

void GestorAcceso::formatUID(uint8_t* uid, uint8_t len, char* buffer) {
    const char hex[] = "0123456789ABCDEF";
    int idx = 0;
    for(int i = 0; i < len; i++) {
        buffer[idx++] = hex[(uid[i] >> 4) & 0x0F];
        buffer[idx++] = hex[uid[i] & 0x0F];
        if (i < (len - 1)) buffer[idx++] = ':';
    }
    buffer[idx] = '\0';
}

// Metodos Publicos

void GestorAcceso::enviarSolicitudAcceso(uint8_t* uid, uint8_t len) {
    char bufferUID[32];
    formatUID(uid, len, bufferUID);

    m_uartPC->clearRxBuffer();
    m_uartPC->Transmit((char*)"REQ:", 4);
    m_uartPC->Transmit(bufferUID, strlen(bufferUID));
    m_uartPC->Transmit((char*)"\r\n", 2);
}

void GestorAcceso::enviarSolicitudGestion(uint8_t* uid, uint8_t len) {
    char bufferUID[32];
    formatUID(uid, len, bufferUID);

    m_uartPC->clearRxBuffer();
    m_uartPC->Transmit((char*)"ADM:", 4);
    m_uartPC->Transmit(bufferUID, strlen(bufferUID));
    m_uartPC->Transmit((char*)"\r\n", 2);
}

bool GestorAcceso::leerRespuesta(char& respuesta) {
    uint8_t rxByte;
    // Usamos tu driver UART que ya es non-blocking
    if (m_uartPC->Receive(rxByte)) {
        respuesta = (char)rxByte;
        return true;
    }
    return false;
}

bool GestorAcceso::esAdmin(uint8_t* uid, uint8_t len){

	uint8_t uidAdmin[] = {0xFE, 0xDE, 0x19, 0x94}; // UID FIJO PARA EL ADMIN

	if (len != 4){
		return false;
	}
	for(int i=0; i<4; i++) {
		if (uid[i] != uidAdmin[i]){
			return false;
		}
	}
	return true;
}

