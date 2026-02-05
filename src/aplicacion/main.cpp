#include "inicializarInfotronic.h"
#include "13-NFC/nfc.h"
#include "systick.h"

// Convierte un byte (ej: 0xFA) a texto "[FA]"
// No usa librerías estándar, costo de memoria casi nulo.
void byteToHexAndFormat(uint8_t byte, char* buffer) {
    const char hexChars[] = "0123456789ABCDEF";

    buffer[0] = '[';
    // Nibble alto (0xFA -> F)
    buffer[1] = hexChars[(byte >> 4) & 0x0F];
    // Nibble bajo (0xFA -> A)
    buffer[2] = hexChars[byte & 0x0F];
    buffer[3] = ']';
    buffer[4] = '\0'; // Terminador nulo
}

// Convierte array de bytes a String Hex: {0xDE, 0xAD} -> "DEAD"
void formatUidForLcd(uint8_t *uid, uint8_t len, char *buffer) {
    const char hex[] = "0123456789ABCDEF";
    for(int i=0; i<len; i++) {
        // Nibble alto
        buffer[i*2] = hex[(uid[i] >> 4) & 0x0F];
        // Nibble bajo
        buffer[i*2+1] = hex[uid[i] & 0x0F];
    }
    buffer[len*2] = '\0'; // Terminador nulo
}

Led L2( 0 , Callback_Leds_gpio , 200 ) ;
Led L3( 1 , Callback_Leds_gpio , 100 ) ;
Led L4( 2 , Callback_Leds_gpio , 300 ) ;

#define MAX_BUFFER 64
uint8_t rxBuffer[MAX_BUFFER];
uint8_t rxIndex = 0;
// Clave por defecto de fábrica (FF FF FF FF FF FF)
uint8_t KEY_DEFAULT[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Estado simple para detectar tramas
enum Estado_t { ESPERANDO_00_1, ESPERANDO_00_2, RECIBIENDO_DATOS };
Estado_t estado = ESPERANDO_00_1;

int main(void) {
    // 1. Inicialización del Hardware
    InicializarInfotronic();

    // Apagamos todo
    L2.Off(); L3.Off(); L4.Off();

    // Mensaje de bienvenida
	lcd->Set("Sistema NFC", 0, 0);
	lcd->Set("Iniciando...", 1, 0);

	// 2. Instanciar el objeto UART
	// uart(num, portTx, pinTx, portRx, pinRx, baudrate)
	// UART 4 | TX: Puerto 0, Pin 16 | RX: Puerto 0, Pin 17 | 115200 Baudios
	uart miUart(4, 0, 16, 0, 17, 115200);
	Nfc miNfc (&miUart);

	// Espera de estabilización
	for(volatile int i=0; i<500000; i++);
	miUart.clearRxBuffer();

	// --- FASE 1: CONFIGURACION (Modo Silencio) ---
	SysTick->CTRL &= ~(1 << 0); // Apagar SysTick (LCD Congelado)
	bool configOk = miNfc.SAMConfig();
	SysTick->CTRL |= (1 << 0);  // Encender SysTick (LCD Activo)

	// 1. Configurar Modulo
	if (configOk) { // O sendCommand() si no lo renombraste
		L2.On(); // Configurado OK
		lcd->Set("Listo!          ", 1, 0);
	} else {
		L3.On(); // Fallo Config
		while(1); // Detener
	}

	uint8_t uid[7];
	uint8_t uidLen;
	char hexStr[5]; // Buffer para texto "[XX]"
	char lcdBuffer[17];

	while(1){
		// A. LIMPIEZA PREVENTIVA
		// Antes de pedir nada, borramos cualquier basura vieja del buffer.
		// Esto evita que se acumulen bytes hasta dar el error m_rxDropped.
		miUart.clearRxBuffer();

		// B. APAGAR INTERRUPCIONES LCD
		// Necesitamos toda la atención en la UART para no perder ni un bit.
		SysTick->CTRL &= ~(1 << 0);

		// C. INTENTAR LEER TARJETA
		bool cardFound = miNfc.readPassiveTargetID(0x00, uid, &uidLen);

		// D. ENCENDER INTERRUPCIONES LCD
		// Ya terminamos de usar la UART intensiva, prendemos para refrescar pantalla
		SysTick->CTRL |= (1 << 0);

		if (cardFound) {
			// Procesar datos
			formatUidForLcd(uid, uidLen, lcdBuffer);
			L4.Blink();

			// Mostrar en LCD
			lcd->Set("UID Detectado:  ", 0, 0);
			lcd->Set(lcdBuffer, 1, 0);

			// Delay visual (Con SysTick prendido para que el LCD se vea)
			for(volatile int i=0; i<4000000; i++);

			// Restaurar pantalla
			lcd->Set("Acerque Tarjeta ", 0, 0);
			lcd->Set("                ", 1, 0);
			L4.Off();
		}
		else {
			// Si no hay tarjeta, esperamos un poco antes de preguntar de nuevo.
			// Es vital que este delay sea corto o inexistente si el buffer se llena rápido,
			// pero como limpiamos el buffer al inicio del while, estamos seguros.
			for(volatile int i=0; i<500000; i++);
		}
	}

	return 0;
}
