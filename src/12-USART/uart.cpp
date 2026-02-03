/*******************************************************************************************************************************//**
 *
 * @file		uart.cpp
 *
 **********************************************************************************************************************************/

#include "ColaCircular.h"

/***********************************************************************************************************************************
 *** INCLUDES
 **********************************************************************************************************************************/
#include "uart.h"
#include "swm.h"

/***********************************************************************************************************************************
 *** VARIABLES GLOBALES PRIVADAS AL MODULO
 **********************************************************************************************************************************/
static uart *g_usart[ 5 ] = {nullptr};

/***********************************************************************************************************************************
 *** IMPLEMENTACION DE LOS METODODS DE LA CLASE
 **********************************************************************************************************************************/

// Definición de buffers
#define TX_BUFF_SZ 256
#define RX_BUFF_SZ 256

uart::uart(uint8_t numUart, uint8_t portTx, uint8_t pinTx, uint8_t portRx, uint8_t pinRx,
           uint32_t baudrate, bits_datos_t BitsDeDatos, paridad_t paridad)
           // NOTA: Ya no llamamos a constructores con parámetros para m_buffRx/Tx
{
    const struct {
        uint8_t ctrl;
        uint8_t iser;
        uint8_t pa_txd;
        uint8_t pa_rxd;
        USART_Type *pUart;
    } prxUart[] = {
            {14, 3,  PA_U0_TXD, PA_U0_RXD, USART0},
            {15, 4,  PA_U1_TXD, PA_U1_RXD, USART1},
            {16, 5,  PA_U2_TXD, PA_U2_RXD, USART2},
            {30, 30, PA_U3_TXD, PA_U3_RXD, USART3},
            {31, 31, PA_U4_TXD, PA_U4_RXD, USART4}
    };

    if (numUart > 4) return;

    x_num = numUart;
    m_rxOverruns = 0;
    m_rxDropped = 0;
    m_rxErrors = 0;
    m_flagTx = false;

    // 1. Habilitar Clock y Reset
    SYSCON->SYSAHBCLKCTRL0 |= (1 << prxUart[numUart].ctrl);
    SYSCON->PRESETCTRL0 &= ~(1 << prxUart[numUart].ctrl);
    SYSCON->PRESETCTRL0 |= (1 << prxUart[numUart].ctrl);

    // 2. Clock Source
    SYSCON->FCLKSEL[numUart] = 1;

    // 3. Configuración de Pines
    PINASSIGN_Config(prxUart[numUart].pa_txd, portTx, pinTx);
    PINASSIGN_Config(prxUart[numUart].pa_rxd, portRx, pinRx);

    m_usart = prxUart[numUart].pUart;
    g_usart[numUart] = this;

    // 4. Configuración UART
    m_usart->CFG = (BitsDeDatos << 2) | (paridad << 4);

    // 5. Configuración Baudrate
    uint32_t clk = 24000000UL;
    uint32_t osrVal = m_usart->OSR + 1;
    uint32_t div = baudrate * osrVal;
    m_usart->BRG = ((clk + (div / 2)) / div) - 1;

    // 6. Interrupciones (Solo RX Data Ready al inicio)
    m_usart->INTENSET = DATAREADY;

    // Prioridad en NVIC
    NVIC->ISER[0] = (1 << prxUart[numUart].iser);
    const uint8_t irq = prxUart[numUart].iser;
    const uint8_t index = irq >> 2;
    const uint8_t shift = (irq & 0x03u) * 8u;
    const uint32_t mask = 0xFFu << shift;
    NVIC->IP[index] = (NVIC->IP[index] & ~mask) | (0u << shift);

    // 7. Habilitar UART
    m_usart->CFG |= (1 << 0);
}


uart::~uart() {
    m_usart->CFG &= ~(1 << 0);
    g_usart[x_num] = nullptr;
}


// --------------------------------------------------------------------------
// MÉTODOS DE TRANSMISIÓN
// --------------------------------------------------------------------------

bool uart::Transmit(char val) {
    while (m_buffTx.isFull()) {
        if (!m_flagTx) {
            m_flagTx = true;
            Tx_EnableInterupt();
        }
    }
    m_buffTx.push((uint8_t)val);
    if (!m_flagTx) {
        m_flagTx = true;
        Tx_EnableInterupt();
    }
    return true;
}

bool uart::Transmit(const char * msg) {
    while (*msg) {
        Transmit(*msg++); // Reutilizamos lógica para simplicidad
    }
    return true;
}

bool uart::Transmit(const void * msg, uint32_t n) {
    const uint8_t *pData = (const uint8_t*)msg;
    for (uint32_t i = 0; i < n; i++) {
        Transmit((char)pData[i]);
    }
    return true;
}

bool uart::Transmit(const uint8_t * frame, uint8_t n) {
    return Transmit((const void*)frame, (uint32_t)n);
}

// --------------------------------------------------------------------------
// IRQ HANDLER OPTIMIZADO PARA PREVENIR OVERRUN
// --------------------------------------------------------------------------

void uart::UART_IRQHandler(void) {
    uint32_t stat = m_usart->STAT;

    // 1. GESTIÓN DE ERRORES (Hardware Error / Overrun)
    // Es crítico limpiar el flag para que la IRQ no se quede pegada
    if (stat & MASK_UART_ERROR) {
        if (stat & MASK_OVERRUNINT) m_rxOverruns++;
        else m_rxErrors++;

        m_usart->STAT = MASK_UART_ERROR; // Escribir 1 limpia los flags
        stat = m_usart->STAT;            // Refrescamos stat local
    }

    // 2. RECEPCIÓN (RX) - Bucle de drenado rápido
    // Usamos el registro hardware directamente en el while para máxima velocidad
    while ((stat = m_usart->STAT) & DATAREADY) {
        // Leer RXDATSTAT limpia el flag DATAREADY automáticamente
        uint8_t datoRx = (uint8_t)m_usart->RXDATSTAT;

        if (!m_buffRx.pushFromIRQ(datoRx)) {
            m_rxDropped++; // Buffer de software lleno
        }
    }

    // 3. TRANSMISIÓN (TX)
    if ((stat & TX_READY) && m_flagTx) {
        uint8_t datoTx;
        if (m_buffTx.pop(datoTx)) {
            m_usart->TXDAT = datoTx;
        } else {
            Tx_DisableInterupt();
            m_flagTx = false;
        }
    }

    // 4. DOUBLE CHECK RX (Truco Anti-Overrun)
    // Si mientras atendíamos TX llegó otro byte, lo leemos YA.
    // Esto evita salir de la ISR y tener que volver a entrar (overhead).
    if (m_usart->STAT & DATAREADY) {
         uint8_t datoRx = (uint8_t)m_usart->RXDATSTAT;
         if (!m_buffRx.pushFromIRQ(datoRx)) {
             m_rxDropped++;
         }
    }
}


// --------------------------------------------------------------------------
// MÉTODOS DE RECEPCIÓN
// --------------------------------------------------------------------------

char * uart::Receive(char * msg, int n) {
    if (m_buffRx.isNewLine() || m_buffRx.qtty() >= (n - 1)) {
        int i;
        uint8_t dato;

        for (i = 0; i < (n - 1); i++) {
            if (m_buffRx.pop(dato)) {
                msg[i] = (char)dato;
            } else {
                break;
            }
        }
        msg[i] = 0;
        m_buffRx.newLineClear();
        return msg;
    }
    return nullptr;
}

// Handlers C
void UART0_IRQHandler(void) { if(g_usart[0]) g_usart[0]->UART_IRQHandler(); }
void UART1_IRQHandler(void) { if(g_usart[1]) g_usart[1]->UART_IRQHandler(); }
void UART2_IRQHandler(void) { if(g_usart[2]) g_usart[2]->UART_IRQHandler(); }
void PININT6_IRQHandler(void) { if(g_usart[3]) g_usart[3]->UART_IRQHandler(); }
void PININT7_IRQHandler(void) { if(g_usart[4]) g_usart[4]->UART_IRQHandler(); }
