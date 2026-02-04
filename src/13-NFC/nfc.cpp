#include "nfc.h"

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
    // El TFI (0xD4) y los Checksums se agregan solos en sendCommand
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
    // 1. Preparar el buffer de salida (Max 64 bytes para comandos simples)
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
    // Transmit(const uint8_t * frame, uint8_t n)
    m_uart->Transmit(frame, idx);

    // 3. Esperar confirmación (ACK) inmediatamente
    return readAck();
}

bool Nfc::readAck() {
    // El ACK es: 00 00 FF 00 FF 00
    // Usamos una version simplificada de tu maquina de estados para detectarlo rapido
    uint8_t ackBuff[6];
    uint8_t readCount = 0;
    uint32_t timeout = NFC_TIMEOUT;

    // Vaciamos basura previa
    // Opcional: m_uart->clearRxBuffer();
    // Cuidado: si borramos buffer aquí podriamos borrar el ACK si llegó muy rápido.

    while (readCount < 6 && timeout > 0) {
        uint8_t d;
        if (m_uart->Receive(d)) {
            ackBuff[readCount++] = d;
        }
        timeout--;
    }

    // Verificación laxa (buscamos el 00 FF 00 FF en el buffer)
    // Nota: Para producción, aplicar aquí también la lógica Sticky Zero si el ACK falla.
    if (readCount >= 6) {
        if (ackBuff[2] == 0xFF && ackBuff[3] == 0x00 && ackBuff[4] == 0xFF) return true;
    }
    return false;
}

// Implementación de TU máquina de estados "Sticky Zero" encapsulada
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
                        state = WAIT_00_2; // Sticky Zero Logic
                    } else if (d == 0xFF) {
                        state = READ_DATA;
                        idx = 0;
                    } else {
                        state = WAIT_00_1; // Ruido
                    }
                    break;

                case READ_DATA:
                    // Aca leemos el resto: LEN, LCS, TFI, DATA..., DCS, POST
                    // Simplificacion: guardamos todo en buffer y luego parseamos
                    // O procesamos al vuelo como en tu main.

                    if (idx == 0) len = d; // Primer byte post 00 FF es LEN

                    responseBuff[idx++] = d;

                    // LEN + LCS(1) + TFI(1) + DATA(LEN-1) + DCS(1) + POST(1)
                    // Total a leer post-header = LEN + 4 (aprox, depende si LEN incluye TFI)
                    // En PN532 LEN incluye el TFI.
                    // Estructura post FF: LEN, LCS, TFI, D0...Dn, DCS, 00

                    if (idx >= (len + 4)) {
                        // Trama completa.
                        // Validar TFI (responseBuff[2] debe ser 0xD5)
                        if (responseBuff[2] == PN532_PN532TOHOST) {
                            // Copiar solo los datos utiles (desde responseBuff[3]) al inicio
                            // Retornar longitud de datos utiles (LEN - 1)
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

bool Nfc::readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength, uint16_t timeout) {
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
    // La respuesta puede tardar si no hay tarjeta presente, asi que usamos el timeout
    uint8_t response[64];
    int16_t dataLen = readResponse(response, sizeof(response));

    // 4. Validar la respuesta
    // Si dataLen < 0, hubo error o timeout en readResponse
    if (dataLen < 0) {
        return false;
    }

    /* Estructura de la Respuesta del PN532 (offset en response[]):
       [0]=LEN, [1]=LCS, [2]=TFI(D5), [3]=CMD+1(4B)
       [4]=NbTg (Numero de tarjetas encontradas)

       Si NbTg > 0, siguen los datos de la tarjeta 1:
       [5]=Tg (Target ID logico)
       [6]=SENS_RES byte1
       [7]=SENS_RES byte2
       [8]=SEL_RES
       [9]=NFCIDLength (Longitud del UID)
       [10...]=NFCID bytes (El UID real)
    */

    // Verificamos que sea la respuesta al comando 4A (debe ser 4B)
    if (response[3] != (PN532_COMMAND_INLISTPASSIVETARGET + 1)) {
        return false;
    }

    // Verificamos NbTg (Byte 4). Si es 0, no encontro tarjetas.
    if (response[4] == 0) {
        return false;
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
