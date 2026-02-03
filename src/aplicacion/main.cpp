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

// Muestra mensaje de error en LCD (sin sprintf para ahorrar memoria)
void lcdError(uint8_t err) {
    char buff[5];
    buff[0] = 'E'; buff[1] = 'r'; buff[2] = 'r';
    buff[3] = '0' + (err % 10); // Solo ultimo digito para simplificar
    buff[4] = '\0';
    lcd->Set(buff, 1, 12); // Mostrar en esquina inferior derecha
}

void buildErrorMsg(uint8_t err, char* buffer) {
    const char* prefix = "Err: ";
    const char* suffix = " (Retry)";
    uint8_t i = 0;

    // Copiar prefijo "Err: "
    while (*prefix) buffer[i++] = *prefix++;

    // Convertir número a ASCII (uint8_t 0-255)
    if (err >= 100) {
        buffer[i++] = '0' + (err / 100);
        buffer[i++] = '0' + ((err / 10) % 10);
    } else if (err >= 10) {
        buffer[i++] = '0' + (err / 10);
    }
    buffer[i++] = '0' + (err % 10);

    // Copiar sufijo " (Retry)"
    while (*suffix) buffer[i++] = *suffix++;

    // Terminador nulo
    buffer[i] = '\0';
}

Led L2( 0 , Callback_Leds_gpio , 200 ) ;
Led L3( 1 , Callback_Leds_gpio , 100 ) ;
Led L4( 2 , Callback_Leds_gpio , 300 ) ;

#define MAX_BUFFER 64
uint8_t rxBuffer[MAX_BUFFER];
uint8_t rxIndex = 0;

// Estado simple para detectar tramas
enum Estado_t { ESPERANDO_00_1, ESPERANDO_00_2, RECIBIENDO_DATOS };
Estado_t estado = ESPERANDO_00_1;

int main(void) {
    // 1. Inicialización del Hardware
    InicializarInfotronic();

    // Apagamos todo
    L2.Off(); L3.Off(); L4.Blink();

	// 2. Instanciar el objeto UART
	// uart(num, portTx, pinTx, portRx, pinRx, baudrate)
	// UART 4 | TX: Puerto 0, Pin 16 | RX: Puerto 0, Pin 17 | 115200 Baudios
	uart miUart(4, 0, 16, 0, 17, 115200);

	// Limpieza inicial por si quedó basura en el buffer
	miUart.clearRxBuffer();

	uint8_t dato;
	uint8_t longitudTrama = 0;

	while(1){
		if(miUart.Receive(dato)) {
			miUart.Transmit(dato);
			switch(estado) {
				case ESPERANDO_00_1:
					if (dato == 0x00) estado = ESPERANDO_00_2;
					break;

				// --- AQUÍ ESTÁ LA MAGIA ---
				case ESPERANDO_00_2:
					if (dato == 0x00) {
						// Si llega otro 00 (sea fantasma o real), NOS QUEDAMOS AQUÍ.
						// Esto se "come" todos los ceros extra hasta que llegue el FF.
						estado = ESPERANDO_00_2;
					}
					else if (dato == 0xFF) {
						 // ¡Llegó el FF! Ahora sí empezamos a guardar datos
						 estado = RECIBIENDO_DATOS;
						 rxIndex = 0;
					}
					else {
						// Si es cualquier otra cosa (ruido), reiniciamos
						estado = ESPERANDO_00_1;
					}
					break;

				// Nota: Borramos "case ESPERANDO_FF" porque ya lo manejamos arriba

				case RECIBIENDO_DATOS:
					rxBuffer[rxIndex++] = dato;

					if (rxIndex == 1) longitudTrama = dato;

					// Verificamos longitud (cast a uint16_t para seguridad)
					if (rxIndex >= ((uint16_t)longitudTrama + 4)) {
						 L2.On(); // EXITO!
						 L3.Off();
						 L4.Off();
						 estado = ESPERANDO_00_1;
						 //miUart.Transmit("OK\n");
					}
					if (rxIndex >= MAX_BUFFER) estado = ESPERANDO_00_1;
					break;
			}
		}

		// Si L3 prende ahora, es brujería (o el cable).
		if (miUart.getRxOverruns() > 0) L3.On();
	}

	return 0;
}
