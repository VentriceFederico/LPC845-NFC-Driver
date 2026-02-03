#ifndef SRC_13_NFC_NFC_H_
#define SRC_13_NFC_NFC_H_

#include "LPC845.h"
#include "uart.h"

// --- Constantes del Protocolo NXP PN532 ---
#define PN532_PREAMBLE              0x00
#define PN532_STARTCODE1            0x00
#define PN532_STARTCODE2            0xFF
#define PN532_POSTAMBLE             0x00

#define PN532_HOSTTOPN532           0xD4
#define PN532_PN532TOHOST           0xD5

// --- Comandos PN532 ---
#define PN532_COMMAND_DIAGNOSE              0x00
#define PN532_COMMAND_GETFIRMWAREVERSION    0x02
#define PN532_COMMAND_GETGENERALSTATUS      0x04
#define PN532_COMMAND_READREGISTER          0x06
#define PN532_COMMAND_WRITEREGISTER         0x08
#define PN532_COMMAND_READGPIO              0x0C
#define PN532_COMMAND_WRITEGPIO             0x0E
#define PN532_COMMAND_SETSERIALBAUDRATE     0x10
#define PN532_COMMAND_SETPARAMETERS         0x12
#define PN532_COMMAND_SAMCONFIGURATION      0x14
#define PN532_COMMAND_POWERDOWN             0x16
#define PN532_COMMAND_RFCONFIGURATION       0x32
#define PN532_COMMAND_INLISTPASSIVE         0x4A

// --- Estados de la Lógica de Negocio (Alto Nivel) ---
enum class NfcState_t {
    IDLE,               // Esperando orden del usuario
    WAITING_ACK,        // Comando enviado, esperando confirmación (ACK)
    WAITING_RESPONSE    // ACK recibido, esperando respuesta de datos (ej. tarjeta leída)
};

// --- Estados del Parser de Trama (Bajo Nivel) ---
enum class ParserState_t {
    PREAMBLE,       // Esperando 0x00
    START_CODE1,    // Esperando 0x00
    START_CODE2,    // Esperando 0xFF
    LENGTH,         // Leyendo longitud (LEN)
    LENGTH_CS,      // Verificando Checksum de longitud (LCS)
    TFI,            // Frame Identifier (D5)
    DATA,           // Leyendo datos del mensaje
    DATA_CS,        // Verificando Checksum de datos (DCS)
    POSTAMBLE       // Esperando 0x00 final
};

class nfc : public uart {
private:
	// Máquinas de Estados
	NfcState_t      m_nfcState;
	ParserState_t   m_parserState;

	static const uint32_t RETRY_THRESHOLD = 100000;

	// Buffers y Contadores de Recepción
	uint8_t     m_rxBuffer[64]; // Buffer para el payload de la trama
	uint8_t     m_rxIndex;
	uint8_t     m_msgLen;       // Longitud esperada de la trama actual
	uint8_t     m_checksum;     // Acumulador para calcular checksum

	// Datos de la última tarjeta leída
	bool        m_cardPresent;
	uint8_t     m_uid[7];
	uint8_t     m_uidLen;
	uint8_t     m_lastCommandSent; // Para saber qué respuesta esperar

	// Métodos Internos del Driver
	void processByte(uint8_t byte);
	void onFrameReceived(); // Se dispara al completar una trama válida
	void onAckReceived();   // Se dispara al recibir un ACK válido
	void sendCommand(const uint8_t* cmd, uint8_t len);

public:

	nfc(uint8_t uartNum, uint8_t portTx, uint8_t pinTx, uint8_t portRx, uint8_t pinRx);
	virtual ~nfc();

	// --- Métodos de Control (FSM) ---
	void Tick();

	// Comandos de Usuario
	void sendWakeUp();
	void startReadPassiveTargetID();

	// Getters de estado
	bool isBusy();          // True si está esperando respuesta del módulo
	bool isCardPresent();   // True si ya leyó una tarjeta
	const uint8_t* getUid();
	uint8_t getUidLength();
};

#endif /* SRC_13_NFC_NFC_H_ */
