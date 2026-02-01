#ifndef SRC_13_NFC_NFC_H_
#define SRC_13_NFC_NFC_H_

#include "LPC845.h"
#include "uart.h"
#include "timer.h"

// --- Constantes del Protocolo PN532 ---
#define PN532_PREAMBLE      0x00
#define PN532_STARTCODE1    0x00
#define PN532_STARTCODE2    0xFF
#define PN532_POSTAMBLE     0x00

#define PN532_HOSTTOPN532 							(0xD4)    ///< Host-to-PN532
#define PN532_PN532TOHOST 							(0xD5)    ///< PN532-to-host

// PN532 Commands
#define PN532_COMMAND_WAKEUP						(0x55)	  ///< WakeUp
#define PN532_COMMAND_INLISTPASSIVE					(0x4A)	  ///<
#define PN532_COMMAND_DIAGNOSE 						(0x00)    ///< Diagnose
#define PN532_COMMAND_GETFIRMWAREVERSION 			(0x02)    ///< Get firmware version
#define PN532_COMMAND_GETGENERALSTATUS 				(0x04)    ///< Get general status
#define PN532_COMMAND_READREGISTER 					(0x06)    ///< Read register
#define PN532_COMMAND_WRITEREGISTER 				(0x08)    ///< Write register
#define PN532_COMMAND_READGPIO 						(0x0C)    ///< Read GPIO
#define PN532_COMMAND_WRITEGPIO 					(0x0E)    ///< Write GPIO
#define PN532_COMMAND_SETSERIALBAUDRATE 			(0x10)    ///< Set serial baud rate
#define PN532_COMMAND_SETPARAMETERS 				(0x12)    ///< Set parameters
#define PN532_COMMAND_SAMCONFIGURATION 				(0x14)    ///< SAM configuration
#define PN532_COMMAND_POWERDOWN 					(0x16)    ///< Power down
#define PN532_COMMAND_RFCONFIGURATION 				(0x32)    ///< RF config
#define PN532_COMMAND_RFREGULATIONTEST 				(0x58)    ///< RF regulation test
#define PN532_COMMAND_INJUMPFORDEP 					(0x56)    ///< Jump for DEP
#define PN532_COMMAND_INJUMPFORPSL 					(0x46)    ///< Jump for PSL
#define PN532_COMMAND_INLISTPASSIVETARGET 			(0x4A)    ///< List passive target
#define PN532_COMMAND_INATR 						(0x50)    ///< ATR
#define PN532_COMMAND_INPSL 						(0x4E)	  ///< PSL
#define PN532_COMMAND_INDATAEXCHANGE 				(0x40)    ///< Data exchange
#define PN532_COMMAND_INCOMMUNICATETHRU 			(0x42)    ///< Communicate through
#define PN532_COMMAND_INDESELECT 					(0x44)    ///< Deselect
#define PN532_COMMAND_INRELEASE 					(0x52)    ///< Release
#define PN532_COMMAND_INSELECT 						(0x54)    ///< Select
#define PN532_COMMAND_INAUTOPOLL 					(0x60)    ///< Auto poll
#define PN532_COMMAND_TGINITASTARGET 				(0x8C)    ///< Init as target
#define PN532_COMMAND_TGSETGENERALBYTES 			(0x92)    ///< Set general bytes
#define PN532_COMMAND_TGGETDATA 					(0x86)    ///< Get data
#define PN532_COMMAND_TGSETDATA 					(0x8E)    ///< Set data
#define PN532_COMMAND_TGSETMETADATA 				(0x94)    ///< Set metadata
#define PN532_COMMAND_TGGETINITIATORCOMMAND 		(0x88) 	  ///< Get initiator command
#define PN532_COMMAND_TGRESPONSETOINITIATOR 		(0x90)    ///< Response to initiator
#define PN532_COMMAND_TGGETTARGETSTATUS 			(0x8A)    ///< Get target status

#define PN532_RESPONSE_INDATAEXCHANGE 				(0x41)    ///< Data exchange
#define PN532_RESPONSE_INLISTPASSIVETARGET 			(0x4B)    ///< List passive target

#define PN532_WAKEUP_SIZE							(17)
#define PN532_ACK_SIZE								(6)

const uint8_t g_pn532_ack[] 			= {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
const uint8_t g_pn532_wakeup[] 			= {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00};

struct DebugTrace_t {
    uint8_t receivedByte;  // El byte que llegó por UART
    uint8_t parserState;   // En qué paso del parser estábamos (PREAMBLE, LENGTH, etc)
    uint8_t nfcState;      // En qué estado lógico estábamos (IDLE, WAITING_ACK)
};
// Decláralo como externo o global para poder verlo en la vista de expresiones
extern DebugTrace_t g_nfcTrace[64];
extern uint8_t g_traceHead;

// Gestion de Comandos
typedef enum {
	IDLE,
	WAITING_ACK,        // Esperando confirmación del comando enviado
	WAITING_RESPONSE    // Esperando la trama de datos con la respuesta
} NfcState_t;

	// Analisis de Trama Byte a Byte
typedef enum {
	PREAMBLE,
	START_CODE1,
	START_CODE2,
	LENGTH,
	LENGTH_CS,
	TFI,
	DATA,
	DATA_CS,
	POSTAMBLE
} ParserState_t;

class nfc : public uart {
private:
	// Variables de Estado FSM
	NfcState_t 		m_nfcState;
	ParserState_t 	m_parserState;

	// Buffer de Recepción de Trama (Payload)
	// El buffer crudo ya está en la clase base uart (m_buffRx)
	uint8_t 	m_rxBuffer[128] = {0};
	uint8_t 	m_rxIndex;
	uint8_t 	m_msgLen;      // Longitud de datos esperada (LEN-1)
	uint8_t 	m_checksum;    // Acumulador para DCS

	// Datos de la última tarjeta leída
	bool 		m_cardPresent;
	uint8_t 	m_uid[7];
	uint8_t 	m_uidLen;
	uint8_t 	m_lastCommandSent; // Para saber qué respuesta esperamos

	// Métodos Internos
	void processByte(uint8_t byte);
	void onFrameReceived(); // Se llama cuando llega una trama de datos válida
	void onAckReceived();   // Se llama cuando llega un ACK válido
	void sendCommand(const uint8_t* cmd, uint8_t len);

public:

	nfc(uint8_t uartNum, uint8_t portTx, uint8_t pinTx, uint8_t portRx, uint8_t pinRx);
	virtual ~nfc();

	// --- Métodos de Control (FSM) ---
	void 			Tick();

	// --- Comandos de Usuario (No bloqueantes) ---
	// Solo inician la transmisión. El resultado se verifica después con isCardPresent().
	void 			sendWakeUp();
	void 			startReadPassiveTargetID();

	// --- Getters de Estado ---
	bool 			isCardPresent() 	const { return m_cardPresent; }
	uint8_t 		getUidLength() 		const { return m_uidLen; }
	const uint8_t* 	getUid() 			const { return m_uid; }

    bool 			wakeUp();
    uint32_t 		getFirmwareVersion();
    bool 			readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength);

    // Si necesitas saber si la FSM está ocupada esperando algo
	bool 			isBusy() 			const { return m_nfcState != NfcState_t::IDLE; }
};

#endif /* SRC_13_NFC_NFC_H_ */
