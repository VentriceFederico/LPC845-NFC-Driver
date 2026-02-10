#include "nfc.h"

// AID (Application ID) que buscaremos en el celular.
// La App de Android debe estar configurada para escuchar este MISMO ID.
const uint8_t SELECT_APDU[] = {
    0x00, /* CLA */
    0xA4, /* INS (Select) */
    0x04, /* P1  (By Name) */
    0x00, /* P2 */
    0x07, /* Length del AID (7 bytes) */
    0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, /* EL AID */
    0x00  /* Le (Esperamos respuesta) */
};

Nfc::Nfc(uart* uart_instance) {
    m_uart = uart_instance;
    wakeUp();
}

bool Nfc::wakeUp(){
	const uint8_t wake[] = {
	        0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
	        0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00
	    };
    // Enviar crudo (sin headers, sin checksums)
    m_uart->Transmit(wake,  (uint8_t)sizeof(wake));

    return readAck();
}

bool Nfc::SAMConfig(){
    // Definimos el payload (Comando + Parámetros)
    uint8_t cmd[] = {
        PN532_COMMAND_SAMCONFIGURATION, // 0x14
        0x01,                           // Modo Normal
        0x14,                           // Timeout
        0x01                            // IRQ Enable
    };

    return sendCommand(cmd, sizeof(cmd)); // sizeof es 4 bytes
}

// Calcula el complemento a 2 para que (Sum + Checksum) & 0xFF == 0
uint8_t Nfc::getChecksum(uint8_t *data, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(~sum + 1);
}

bool Nfc::sendCommand(uint8_t *cmd, uint8_t cmdLen) {
    // 1. Preparar el buffer de salida
    uint8_t frame[64];
    uint8_t idx = 0;
    uint8_t checksumData = 0;

    // --- CABECERA ---
    frame[idx++] = PN532_PREAMBLE;      // 0x00
    frame[idx++] = PN532_STARTCODE1;    // 0x00
    frame[idx++] = PN532_STARTCODE2;    // 0xFF

    // --- LONGITUD (LEN) ---
    // LEN = TFI (1 byte) + cmdLen (bytes del comando)
    uint8_t length = cmdLen + 1;
    frame[idx++] = length;

    // --- LENGTH CHECKSUM (LCS) ---
    // Fórmula: ~LEN + 1
    frame[idx++] = (uint8_t)(~length + 1);

    // --- TFI (Direction) ---
    frame[idx++] = PN532_HOSTTOPN532;   // 0xD4
    checksumData += PN532_HOSTTOPN532;  // Sumamos al acumulador del checksum

    // --- DATA (Comando + Params) ---
    for (uint8_t i = 0; i < cmdLen; i++) {
        frame[idx++] = cmd[i];
        checksumData += cmd[i];         // Sumamos al acumulador
    }

    // --- DATA CHECKSUM (DCS) ---
    // Fórmula: ~Suma + 1
    frame[idx++] = (uint8_t)(~checksumData + 1);

    // --- POSTAMBLE ---
    frame[idx++] = PN532_POSTAMBLE;     // 0x00

    // 2. Enviar Trama Completa usando tu método Transmit
    m_uart->Transmit(frame, idx);

    // 3. Esperar confirmación (ACK) inmediatamente
    return readAck();
}

bool Nfc::readAck() {
    // El ACK es: 00 00 FF 00 FF 00
    uint8_t ackBuff[6];
    uint8_t readCount = 0;
    uint32_t timeout = NFC_TIMEOUT;

    while (readCount < 6 && timeout > 0) {
        uint8_t d;
        if (m_uart->Receive(d)) {
            ackBuff[readCount++] = d;
        }
        timeout--;
    }

    // buscamos el 00 FF 00 FF en el buffer)
    if (readCount >= 6) {
        if (ackBuff[2] == 0xFF && ackBuff[3] == 0x00 && ackBuff[4] == 0xFF) return true;
    }
    return false;
}

int16_t Nfc::readResponse(uint8_t *responseBuff, uint8_t maxLen) {
    uint8_t d;
    uint8_t len = 0;
    uint8_t idx = 0;
    uint32_t timeout = NFC_TIMEOUT * 5; // Mas tiempo para respuestas de datos

    // Estados internos del metodo
    enum { WAIT_00_1, WAIT_00_2, READ_DATA } state = WAIT_00_1;

    while (timeout--) {
        if (m_uart->Receive(d)) {
            switch (state) {
                case WAIT_00_1:
                    if (d == 0x00) state = WAIT_00_2;
                    break;

                case WAIT_00_2:
                    if (d == 0x00) {
                        state = WAIT_00_2;
                    } else if (d == 0xFF) {
                        state = READ_DATA;
                        idx = 0;
                    } else {
                        state = WAIT_00_1; // Ruido
                    }
                    break;

                case READ_DATA:

                    if (idx == 0) len = d; // Primer byte post 00 FF es LEN

                    responseBuff[idx++] = d;

                    // LEN + LCS(1) + TFI(1) + DATA(LEN-1) + DCS(1) + POST(1)
                    // Total a leer post-header = LEN + 4
                    // En PN532 LEN incluye el TFI.
                    // Estructura post FF: LEN, LCS, TFI, D0...Dn, DCS, 00

                    if (idx >= (len + 4)) {
                        // Trama completa.
                        // Validar TFI (responseBuff[2] debe ser 0xD5)
                        if (responseBuff[2] == PN532_PN532TOHOST) {
                            return len - 1;
                        }
                        return -1; // Error de trama
                    }
                    break;
            }
        }
    }
    return 0; // Timeout
}

bool Nfc::readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength, uint8_t *sak, uint16_t timeout) {
    // 1. Armar el comando
    // 0x4A: InListPassiveTarget
    // 0x01: MaxTg (Queremos leer solo 1 tarjeta a la vez)
    // cardbaudrate: 0x00 para Mifare 106kbps (lo estandar)
    uint8_t cmd[] = { PN532_COMMAND_INLISTPASSIVETARGET, 0x01, cardbaudrate };

    // 2. Enviar comando y esperar ACK
    if (!sendCommand(cmd, sizeof(cmd))) {
        return false; // No hubo ACK del modulo
    }

    // 3. Esperar la respuesta con los datos de la tarjeta
    uint8_t response[64];
    int16_t dataLen = readResponse(response, sizeof(response));

    // 4. Validar la respuesta
    if (dataLen < 0) {
        return false;
    }

    // Verificamos que sea la respuesta al comando 4A (debe ser 4B)
    if (response[3] != (PN532_COMMAND_INLISTPASSIVETARGET + 1)) {
        return false;
    }

    // Verificamos NbTg (Byte 4). Si es 0, no encontro tarjetas.
    if (response[4] == 0) {
        return false;
    }

    if(sak != nullptr){
    	*sak = response[8];
    }

    // Leemos la longitud del UID (Byte 9)
    *uidLength = response[9];

    // Validacion de seguridad para no desbordar buffer
    if (*uidLength > 7) *uidLength = 7;

    // Copiamos el UID al buffer del usuario
    for (uint8_t i = 0; i < *uidLength; i++) {
        uid[i] = response[10 + i];
    }

    return true;
}

bool Nfc::negotiateWithPhone(uint8_t* responseBuff, uint8_t* responseLen) {
    uint8_t cmd[30];
    cmd[0] = 0x40; // Command: InDataExchange
    cmd[1] = 0x01; // Target 1
    memcpy(&cmd[2], SELECT_APDU, sizeof(SELECT_APDU));

    // 1. Enviamos el comando
	if (!sendCommand(cmd, 15)) return false;

	// 2. Esperamos respuesta
	uint8_t rxBuffer[64];
	int16_t bytesRecibidos = readResponse(rxBuffer, sizeof(rxBuffer));

	if(bytesRecibidos != (rxBuffer[0] - 1)) return false;

	// 1. Chequeamos TFI (D5) y CMD (41) en posiciones fijas
	if (rxBuffer[2] != 0xD5 && rxBuffer[3] != 0x41) return false;

	// 2. Chequeamos Status del PN532 (debe ser 00)
	if (rxBuffer[4] != 0x00) return false;

	// 3. Calculamos índices usando el Byte de Longitud (rxBuffer[0])
	uint8_t largoTrama = rxBuffer[0];

	// Los SW1 y SW2 estan al final de la trama
	// Inicio Trama = indice 2 (D5)
	// Fin Trama util = indice 2 + largoTrama
	// SW1 esta en (inicio + largo - 2) -> 2 + 9 - 2 = 9
	// SW2 esta en (inicio + largo - 1) -> 2 + 9 - 1 = 10
	int indexSW1 = 2 + largoTrama - 2;
	int indexSW2 = 2 + largoTrama - 1;

	uint8_t sw1 = rxBuffer[indexSW1];
	uint8_t sw2 = rxBuffer[indexSW2];

	// 4. Verificamos que la App contesto OK (90 00)
	if (sw1 == 0x90 && sw2 == 0x00) {

		// Calculamos cuantos bytes de datos reales hay
		int datosUtiles = largoTrama - 5;

		if (datosUtiles <= 0) return false;

		*responseLen = datosUtiles;

		if (*responseLen > 7) *responseLen = 7;

		// Los datos siempre empiezan en la posición 5 (después del Status 00)
		memcpy(responseBuff, &rxBuffer[5], *responseLen);

		return true;
	}

	return false;
}
