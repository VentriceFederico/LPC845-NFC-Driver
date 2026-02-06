#ifndef SRC_13_NFC_NFC_H_
#define SRC_13_NFC_NFC_H_

#include "uart.h"
#include <cstring>

// Comandos
#define PN532_COMMAND_SAMCONFIGURATION      0x14
#define PN532_COMMAND_INLISTPASSIVETARGET   0x4A
#define PN532_COMMAND_INDATAEXCHANGE        0x40
#define MIFARE_CMD_AUTH_A                   0x60
#define MIFARE_CMD_READ                     0x30

// Constantes de Protocolo
#define PN532_PREAMBLE                      0x00
#define PN532_STARTCODE1                    0x00
#define PN532_STARTCODE2                    0xFF
#define PN532_POSTAMBLE                     0x00
#define PN532_HOSTTOPN532                   0xD4  	// Dirección LPC -> Modulo
#define PN532_PN532TOHOST                   0xD5	// Direccion Modulo -> LPC

// Timeout simple para no colgar el programa si el modulo no responde
#define NFC_TIMEOUT                         2000000

class Nfc {
private:
    uart* m_uart;
    uint8_t m_packetBuffer[64]; // Buffer interno para armar tramas

    // Funciones internas
    uint8_t getChecksum(uint8_t *data, uint8_t len);
    bool readAck(); // Espera el ACK estándar: 00 00 FF 00 FF 00


public:
    Nfc(uart* uart_instance);

    bool wakeUp();
    int16_t readResponse(uint8_t *responseBuff, uint8_t maxLen);
    bool sendCommand(uint8_t *cmd, uint8_t cmdLen);     // Envia un comando crudo, calcula Checksums y espera el ACK
    // Configuración específica (SAM)
	bool SAMConfig();
	bool readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength, uint8_t *sak = nullptr, uint16_t timeout = 1000);
	bool authenticateBlock(uint8_t *uid, uint8_t uidLen, uint32_t blockNumber, uint8_t *keyData);
	bool readDataBlock(uint8_t blockNumber, uint8_t *data);
	bool negotiateWithPhone(uint8_t* response, uint8_t* responseLen);
};

#endif /* SRC_13_NFC_NFC_H_ */
