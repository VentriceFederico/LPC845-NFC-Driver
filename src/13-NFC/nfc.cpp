#include <13-NFC/nfc.h>
#include <string.h>

// Macros para depuracion rapida de tiempos
#define WAIT_TIMEOUT(x) for(volatile uint32_t i=0; i<(x*5000); i++)

// --- VARIABLES DE DEBUG GLOBAL ---
DebugTrace_t g_nfcTrace[64];
uint8_t g_traceHead = 0;

// Constructor: Inicializa la UART y los estados
nfc::nfc(uint8_t uartNum, uint8_t portTx, uint8_t pinTx, uint8_t portRx, uint8_t pinRx)
    : uart(uartNum, portTx, pinTx, portRx, pinRx, 115200, uart::ocho_bits, uart::NoParidad)
{
    m_nfcState = NfcState_t::IDLE;
    m_parserState = ParserState_t::PREAMBLE;
    m_rxIndex = 0;
    m_msgLen = 0;
    m_checksum = 0;

    m_cardPresent = false;
    m_uidLen = 0;
    m_lastCommandSent = 0;
    m_lastRxOverruns = 0;
    m_wakeupConfirmed = false;
    memset(m_uid, 0, sizeof(m_uid));
}

nfc::~nfc() { }

/**
 * @brief Máquina de Estados Principal.
 * Procesa todos los bytes pendientes en la cola de recepción de la UART.
 */
void nfc::Tick() {
    const uint32_t currentOverruns = getRxOverruns();
    if (currentOverruns != m_lastRxOverruns) {
        m_lastRxOverruns = currentOverruns;
       /*
        m_parserState = ParserState_t::PREAMBLE;
        m_rxIndex = 0;
        m_msgLen = 0;
        m_checksum = 0;
        clearRxBuffer();
    	*/
    }

    uint8_t byte;
    while (this->Receive(byte)) {
        processByte(byte);
    }
}

/**
 * @brief Parser byte a byte. Reconstruye la estructura de la trama NXP.
 */
void nfc::processByte(uint8_t byte) {

	// --- SNIFFER EN RAM (Inicio) ---
	// Guardamos qué está pasando antes de procesar
	g_nfcTrace[g_traceHead].receivedByte = byte;
	g_nfcTrace[g_traceHead].parserState  = (uint8_t)m_parserState;
	g_nfcTrace[g_traceHead].nfcState     = (uint8_t)m_nfcState;

	// Avanzamos índice circularmente
	g_traceHead = (g_traceHead + 1) % 64;
	// --- SNIFFER EN RAM (Fin) ---

    switch ( m_parserState ) {
        case ParserState_t::PREAMBLE:
            if (byte == PN532_PREAMBLE) {
            	m_parserState = ParserState_t::START_CODE1;
            }
            break;

        case ParserState_t::START_CODE1:
            if (byte == PN532_PREAMBLE) {
                // Sigue siendo preámbulo, nos quedamos aquí.
            } else if (byte == PN532_STARTCODE2) { // 0xFF
            	m_parserState = ParserState_t::LENGTH;
            } else {
            	m_parserState = ParserState_t::PREAMBLE; // Ruido, reiniciar.
            }
            break;

        case ParserState_t::LENGTH:
            // Aquí distinguimos ACK de Trama de Datos.
            // ACK estándar: ... 00 FF 00 FF 00 ...
            // LEN=00, LCS=FF.
            if (byte == 0x00) {
                // Posible ACK (LEN=0). Lo confirmaremos en el siguiente byte (LCS).
                m_msgLen = 0;
            } else {
                m_msgLen = byte;
            }
            m_parserState = ParserState_t::LENGTH_CS;
            break;

        case ParserState_t::LENGTH_CS: // LCS
			// 1. CORRECCIÓN CRÍTICA: Detectar ACK explícitamente primero.
			// El ACK (LEN=0, LCS=FF) no cumple la suma (0+FF != 0), por lo que fallaba antes.
			if (m_msgLen == 0 && byte == 0xFF) {
				onAckReceived();
				m_parserState = ParserState_t::POSTAMBLE; // El ACK termina en 00
				break; // Salimos del switch
			}

			// 2. Validación estándar para tramas de datos (LEN + LCS = 0)
			if ((uint8_t)(m_msgLen + byte) == 0x00) {
				// Es una trama de datos válida
				m_parserState = ParserState_t::TFI;
			} else {
				// Error de checksum real (ruido o trama rota)
				m_parserState = ParserState_t::PREAMBLE;
			}
			break;

        case ParserState_t::TFI:
            if (byte == PN532_PN532TOHOST) { // 0xD5
                m_rxIndex = 0;
                m_checksum = byte; // TFI es el primer byte del cálculo de checksum
                m_parserState = ParserState_t::DATA;
            } else {
            	m_parserState = ParserState_t::PREAMBLE;
            }
            break;

        case ParserState_t::DATA:
            m_rxBuffer[m_rxIndex++] = byte;
            m_checksum += byte;

            // m_msgLen incluía el TFI, así que restamos 1 para saber cuántos bytes de payload quedan
            if (m_rxIndex >= (m_msgLen - 1)) {
            	m_parserState = ParserState_t::DATA_CS;
            }
            break;

        case ParserState_t::DATA_CS: // DCS
            if ((uint8_t)(m_checksum + byte) == 0x00) {
                // Checksum de datos válido
            	m_parserState = ParserState_t::POSTAMBLE;
            } else {
            	m_parserState = ParserState_t::PREAMBLE; // Checksum fail
            }
            break;

        case ParserState_t::POSTAMBLE:
            if (byte == PN532_POSTAMBLE) {
                // Trama completada exitosamente
                if (m_msgLen > 0) { // Si no era un ACK
                    onFrameReceived();
                }
            }
            m_parserState = ParserState_t::PREAMBLE; // Listos para la siguiente
            break;
    }
}

/**
 * @brief Maneja la recepción de un ACK válido.
 */
void nfc::onAckReceived() {
    if (m_nfcState == NfcState_t::WAITING_ACK) {
        // CAMBIO AQUI: Agregamos SAMCONFIGURATION a la lista
        if (m_lastCommandSent == PN532_COMMAND_INLISTPASSIVE ||
            m_lastCommandSent == PN532_COMMAND_SAMCONFIGURATION) { // <--- 0x14
             m_nfcState = NfcState_t::WAITING_RESPONSE;
        } else {
             m_nfcState = NfcState_t::IDLE;
        }
    }
}

/**
 * @brief Maneja la recepción de una trama de datos válida.
 */
void nfc::onFrameReceived() {
    // 1. Respuesta a InListPassiveTarget
    if (m_nfcState == NfcState_t::WAITING_RESPONSE &&
        m_rxBuffer[0] == (PN532_COMMAND_INLISTPASSIVE + 1))
    {
        // ... (Tu código de lectura de UID sigue igual) ...
        // ...
        m_nfcState = NfcState_t::IDLE;
    }
    // 2. Respuesta a WakeUp (SAMConfiguration)
    // CAMBIO AQUI: Verificamos contra SAMCONFIGURATION (0x14 + 1 = 0x15)
    else if (m_nfcState == NfcState_t::WAITING_RESPONSE &&
             m_rxBuffer[0] == (PN532_COMMAND_SAMCONFIGURATION + 1))
    {
        // WakeUp exitoso confirmado
        m_wakeupConfirmed = true;
        m_nfcState = NfcState_t::IDLE;
    }
}

/**
 * @brief Envía comando WakeUp (0x55 0x55 ...).
 */
void nfc::sendWakeUp() {
    // Definimos el array
	const uint8_t wake[] = {
	        0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, // Long Preamble
	        0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00 // SAMConfig
	    };

    // --- CORRECCIÓN AQUÍ ---
    // Hacemos cast explícito a (uint8_t) en el segundo argumento (tamaño)
    // para que coincida con Transmit(uint8_t*, uint8_t)
	this->Transmit((uint8_t*)wake, (uint8_t)sizeof(wake));

	m_lastCommandSent = PN532_COMMAND_SAMCONFIGURATION;
	m_nfcState = NfcState_t::WAITING_ACK;
}

/**
 * @brief Envía WakeUp y espera la respuesta completa (ACK + trama SAMConfig).
 * Reintenta una vez si no se recibe la trama completa.
 */
bool nfc::wakeUp() {
    const uint8_t maxAttempts = 2;
    for (uint8_t attempt = 0; attempt < maxAttempts; ++attempt) {
        m_wakeupConfirmed = false;
        sendWakeUp();

        for (uint32_t guard = 0; guard < 200000; ++guard) {
            Tick();
            if (m_wakeupConfirmed) {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Inicia la búsqueda de tarjeta. NO BLOQUEA.
 * Debes llamar a isCardPresent() en el bucle principal para ver el resultado.
 */
void nfc::startReadPassiveTargetID() {
    uint8_t payload[] = { PN532_COMMAND_INLISTPASSIVE, 0x01, 0x00 }; // Max 1 tarjeta, 106kbps

    m_cardPresent = false; // Reseteamos estado anterior
    m_lastCommandSent = PN532_COMMAND_INLISTPASSIVE;

    sendCommand(payload, sizeof(payload));
    m_nfcState = NfcState_t::WAITING_ACK;
}

/**
 * @brief Empaqueta y envía un comando según protocolo NXP.
 */
void nfc::sendCommand(const uint8_t* cmd, uint8_t len) {
    uint8_t frame[64];
    uint8_t idx = 0;
    uint8_t checksum = 0;

    // 1. Preamble & Start
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_PREAMBLE; // A veces doble 00 ayuda a sincronizar
    frame[idx++] = PN532_STARTCODE2; // 0xFF

    // 2. Length (TFI + Data)
    uint8_t totalLen = len + 1;
    frame[idx++] = totalLen;
    frame[idx++] = (uint8_t)(~totalLen + 1); // LCS

    // 3. TFI & Data
    frame[idx++] = PN532_HOSTTOPN532;
    checksum += PN532_HOSTTOPN532;

    for(int i=0; i<len; i++) {
        frame[idx++] = cmd[i];
        checksum += cmd[i];
    }

    // 4. DCS & Postamble
    frame[idx++] = (uint8_t)(~checksum + 1);
    frame[idx++] = PN532_POSTAMBLE;

    this->Transmit(frame, idx);
}
