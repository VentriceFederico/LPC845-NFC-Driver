#include <13-NFC/nfc.h>
#include <string.h>

// Constructor
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

    // Limpieza inicial de buffers
    memset(m_rxBuffer, 0, sizeof(m_rxBuffer));
    memset(m_uid, 0, sizeof(m_uid));
}

nfc::~nfc() { }

/**
 * @brief Motor principal. Procesa los bytes que llegan a la UART.
 */
void nfc::Tick() {
    uint8_t byte;
    // Mientras haya datos en la cola de recepción UART, procésalos
    while (this->Receive(byte)) {
        processByte(byte);
    }
}

/**
 * @brief Parser byte a byte. Reconstruye la estructura de la trama NXP.
 */
/**
 * @brief Máquina de Estados del Parser (Nivel Bajo).
 * Analiza byte a byte para encontrar tramas válidas o ACKs.
 */
void nfc::processByte(uint8_t byte) {
    switch (m_parserState) {
        case ParserState_t::PREAMBLE:
            if (byte == PN532_PREAMBLE)
                m_parserState = ParserState_t::START_CODE1;
            break;

        case ParserState_t::START_CODE1:
            if (byte == PN532_STARTCODE1)
                m_parserState = ParserState_t::START_CODE2;
            else if (byte != PN532_PREAMBLE) // Si no es 00, reiniciamos
                m_parserState = ParserState_t::PREAMBLE;
            break;

        case ParserState_t::START_CODE2:
            if (byte == PN532_STARTCODE2)
                m_parserState = ParserState_t::LENGTH;
            else
                m_parserState = ParserState_t::PREAMBLE;
            break;

        case ParserState_t::LENGTH:
            m_msgLen = byte;
            m_parserState = ParserState_t::LENGTH_CS;
            break;

        case ParserState_t::LENGTH_CS:
            // CASO ESPECIAL: El ACK (00 FF) matemáticamente falla el checksum normal.
            // Hay que detectarlo explícitamente aquí.
            if (m_msgLen == 0 && byte == 0xFF) {
                onAckReceived();
                m_parserState = ParserState_t::POSTAMBLE;
                break;
            }

            // Validación normal: LEN + LCS debe ser 0x00
            if ((uint8_t)(m_msgLen + byte) == 0x00) {
                m_parserState = ParserState_t::TFI;
            } else {
                m_parserState = ParserState_t::PREAMBLE; // Error de trama
            }
            break;

        case ParserState_t::TFI:
            if (byte == PN532_PN532TOHOST) {
                m_rxIndex = 0;
                m_checksum = PN532_PN532TOHOST; // Iniciamos checksum con el TFI
                m_parserState = ParserState_t::DATA;
            } else {
                m_parserState = ParserState_t::PREAMBLE;
            }
            break;

        case ParserState_t::DATA:
            m_rxBuffer[m_rxIndex++] = byte;
            m_checksum += byte; // Acumulamos para validar al final

            // Nota: m_msgLen incluía el TFI, por eso restamos 1
            if (m_rxIndex >= (m_msgLen - 1)) {
                m_parserState = ParserState_t::DATA_CS;
            }
            break;

        case ParserState_t::DATA_CS:
            m_checksum += byte; // La suma total (Datos + DCS) debe dar 0x00
            if (m_checksum == 0x00) {
                onFrameReceived(); // ¡Trama Válida!
            }
            m_parserState = ParserState_t::POSTAMBLE;
            break;

        case ParserState_t::POSTAMBLE:
            // Siempre volvemos a buscar la siguiente trama
            m_parserState = ParserState_t::PREAMBLE;
            break;
    }
}

/**
 * @brief Se llama cuando recibimos un ACK (00 00 FF 00 FF 00)
 */
void nfc::onAckReceived() {
    // Si estábamos esperando confirmación, avanzamos el estado lógico
    if (m_nfcState == NfcState_t::WAITING_ACK) {
        m_nfcState = NfcState_t::WAITING_RESPONSE;
    }
}

/**
 * @brief Se llama cuando recibimos una trama de datos completa y verificada.
 */
void nfc::onFrameReceived() {
    // Verificamos que la respuesta corresponda al último comando enviado

    // 1. Respuesta a Lectura de Tarjeta (InListPassiveTarget)
    if (m_nfcState == NfcState_t::WAITING_RESPONSE &&
        m_rxBuffer[0] == (PN532_COMMAND_INLISTPASSIVE + 1))
    {
        uint8_t tagsFound = m_rxBuffer[1];
        if (tagsFound > 0) {
            m_uidLen = m_rxBuffer[6]; // Longitud del UID
            // Copiamos el UID (comienza en el byte 7)
            for (uint8_t i = 0; i < m_uidLen && i < 7; i++) {
                m_uid[i] = m_rxBuffer[7 + i];
            }
            m_cardPresent = true;
        }
        m_nfcState = NfcState_t::IDLE;
    }
    // 2. Respuesta a WakeUp (SAMConfiguration)
    else if (m_nfcState == NfcState_t::WAITING_RESPONSE &&
             m_rxBuffer[0] == (PN532_COMMAND_SAMCONFIGURATION + 1))
    {
        // El módulo despertó correctamente
        m_nfcState = NfcState_t::IDLE;
    }
}

// --- Comandos Públicos ---

void nfc::sendWakeUp() {
    // Trama especial larga para despertar al módulo (contiene SAMConfiguration 0x14)
    const uint8_t wake[] = {
        0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x14, 0x01, 0x02, 0x00
    };

    this->Transmit((uint8_t*)wake, (uint8_t)sizeof(wake));

    m_lastCommandSent = PN532_COMMAND_SAMCONFIGURATION;
    m_nfcState = NfcState_t::WAITING_ACK;
}

void nfc::startReadPassiveTargetID() {
    // Comando InListPassiveTarget (0x4A), Max 1 tarjeta (0x01), 106kbps (0x00)
    uint8_t payload[] = { PN532_COMMAND_INLISTPASSIVE, 0x01, 0x00 };

    m_cardPresent = false;
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

    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_PREAMBLE;
    frame[idx++] = PN532_STARTCODE2; // 0xFF Start Code 2

    frame[idx++] = len + 1;         // LEN (Datos + TFI)
    frame[idx++] = ~(len + 1) + 1;  // LCS (Complemento a 2)

    frame[idx++] = PN532_HOSTTOPN532; // TFI
    checksum += PN532_HOSTTOPN532;

    for (uint8_t i = 0; i < len; i++) {
        frame[idx++] = cmd[i];
        checksum += cmd[i];
    }

    frame[idx++] = ~checksum + 1;   // DCS
    frame[idx++] = PN532_POSTAMBLE;

    this->Transmit(frame, idx);
}

// --- Getters ---

bool nfc::isBusy() {
    return (m_nfcState != NfcState_t::IDLE);
}

bool nfc::isCardPresent() {
    return m_cardPresent;
}

const uint8_t* nfc::getUid() {
    return m_uid;
}

uint8_t nfc::getUidLength() {
    return m_uidLen;
}
