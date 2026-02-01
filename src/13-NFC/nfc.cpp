#include <13-NFC/nfc.h>
#include <string.h>

// Macros para depuracion rapida de tiempos
#define WAIT_TIMEOUT(x) for(volatile uint32_t i=0; i<(x*5000); i++)

// --- VARIABLES DE DEBUG GLOBAL ---
DebugTrace_t g_nfcTrace[64];
uint8_t g_traceHead = 0;
volatile uint8_t g_rawBuffer[64];
volatile uint32_t g_rawIdx = 0;
NfcFrameLog_t g_nfcFrameLog[16];
uint8_t g_frameLogHead = 0;

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
    m_wakeupStatus = WAKEUP_IDLE;
    m_wakeupRetries = 0;
    memset(m_uid, 0, sizeof(m_uid));

    // Inicialización de variables de reintento
	m_retryTimer = 0;
	m_lastCmdLen = 0;
	m_isWakeupCmd = false;
	memset(m_lastCmdBuffer, 0, sizeof(m_lastCmdBuffer));
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
		// El reinicio forzado aquí puede estar rompiendo la trama válida
		//m_parserState = ParserState_t::PREAMBLE;
		//m_rxIndex = 0;
		//m_checksum = 0;
	}


    uint8_t byte;
	while (this->Receive(byte)) {
		processByte(byte);

		// Si recibimos ALGO, reseteamos el timer de reintento para dar más tiempo
		if (m_nfcState != NfcState_t::IDLE) {
			 m_retryTimer = 0;
		}
	}

	//Lógica de Reintento Automático (Timeout)
    if (m_nfcState != NfcState_t::IDLE) {
        m_retryTimer++;
        if (m_retryTimer > RETRY_THRESHOLD) {
            // ¡Timeout! No llegó respuesta. Reenviamos.
            retransmitLastCommand();
            m_retryTimer = 0; // Reiniciar cuenta
        }
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
				logFrame(NfcFrameLogType::ERROR, &byte, 1);
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
            	logFrame(NfcFrameLogType::ERROR, &byte, 1);
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
    	logFrame(NfcFrameLogType::ACK, nullptr, 0);
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
        logFrame(NfcFrameLogType::DATA, m_rxBuffer, m_rxIndex);
        m_nfcState = NfcState_t::IDLE;
    }
    // 2. Respuesta a WakeUp (SAMConfiguration)
    // CAMBIO AQUI: Verificamos contra SAMCONFIGURATION (0x14 + 1 = 0x15)
    else if (m_nfcState == NfcState_t::WAITING_RESPONSE &&
             m_rxBuffer[0] == (PN532_COMMAND_SAMCONFIGURATION + 1))
    {
        // WakeUp exitoso confirmado
        m_wakeupConfirmed = true;
        m_wakeupStatus = WAKEUP_OK;
        logFrame(NfcFrameLogType::DATA, m_rxBuffer, m_rxIndex);
        m_nfcState = NfcState_t::IDLE;
    }
}

void nfc::logFrame(NfcFrameLogType type, const uint8_t* data, uint8_t len) {
	NfcFrameLog_t &entry = g_nfcFrameLog[g_frameLogHead];
	entry.type = type;
	entry.len = len;
	entry.parserState = static_cast<uint8_t>(m_parserState);
	entry.nfcState = static_cast<uint8_t>(m_nfcState);

	const uint8_t copyLen = (len > sizeof(entry.data)) ? sizeof(entry.data) : len;
	for (uint8_t i = 0; i < copyLen; i++) {
		entry.data[i] = data ? data[i] : 0x00;
	}
	for (uint8_t i = copyLen; i < sizeof(entry.data); i++) {
		entry.data[i] = 0x00;
	}

	g_frameLogHead = (g_frameLogHead + 1) % 16;
}

/**
 * @brief Envía comando WakeUp (0x55 0x55 ...).
 */
void nfc::sendWakeUp() {
    // Marcamos que el último comando fue WakeUp (secuencia especial)
    m_isWakeupCmd = true;
    m_retryTimer = 0;
    m_wakeupRetries = 0;

    // Secuencia WakeUp Raw
    const uint8_t wake[] = {
            0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00
    };

    this->Transmit((uint8_t*)wake, (uint8_t)sizeof(wake));

    m_lastCommandSent = PN532_COMMAND_SAMCONFIGURATION;
    m_nfcState = NfcState_t::WAITING_ACK;
}

void nfc::startWakeUp() {
	if (m_wakeupStatus == WAKEUP_IN_PROGRESS) {
		return;
	}

	m_wakeupConfirmed = false;
	m_wakeupStatus = WAKEUP_IN_PROGRESS;
	sendWakeUp();
}

/**
 * @brief Inicia WakeUp si está en idle y retorna el estado actual (no bloqueante).
 */
bool nfc::wakeUp() {
	if (m_wakeupStatus == WAKEUP_IDLE) {
		startWakeUp();
	}

	return (m_wakeupStatus == WAKEUP_OK);
}

void nfc::retransmitLastCommand() {
    if (m_isWakeupCmd) {
    	if (m_wakeupRetries >= WAKEUP_MAX_RETRIES) {
    		m_wakeupStatus = WAKEUP_FAILED;
    		m_nfcState = NfcState_t::IDLE;
    		m_isWakeupCmd = false;
    		return;
    	}
    	m_wakeupRetries++;
        // Si era WakeUp, reenviamos la secuencia especial
        const uint8_t wake[] = {
             0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00
        };
        this->Transmit((uint8_t*)wake, (uint8_t)sizeof(wake));
    } else {
        // Si era comando normal, usamos sendCommand (que volverá a armar la trama)
        // Nota: sendCommand reseteará el timer y volverá a guardar el buffer.
        // Es un poco redundante copiar el buffer sobre sí mismo, pero es seguro.
        sendCommand(m_lastCmdBuffer, m_lastCmdLen);
    }

    // Restauramos el estado esperado (por si acaso cambió erróneamente)
    m_nfcState = NfcState_t::WAITING_ACK;
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
    // 1. Guardar copia para reintentos
    if (len <= sizeof(m_lastCmdBuffer)) {
        memcpy(m_lastCmdBuffer, cmd, len);
        m_lastCmdLen = len;
    }
    m_isWakeupCmd = false;
    m_retryTimer = 0; // Reset timer

    // 2. Construcción y Envío de la Trama (Código original)
    uint8_t frame[64];
    uint8_t idx = 0;
    uint8_t checksum = 0;

    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE2;

    uint8_t totalLen = len + 1;
    frame[idx++] = totalLen;
    frame[idx++] = (uint8_t)(~totalLen + 1);

    frame[idx++] = PN532_HOSTTOPN532;
    checksum += PN532_HOSTTOPN532;

    for(int i=0; i<len; i++) {
        frame[idx++] = cmd[i];
        checksum += cmd[i];
    }

    frame[idx++] = (uint8_t)(~checksum + 1);
    frame[idx++] = PN532_POSTAMBLE;

    this->Transmit(frame, idx);
}
