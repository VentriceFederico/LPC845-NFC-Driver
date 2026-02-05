#include "inicializarInfotronic.h"
#include "13-NFC/nfc.h"
#include "systick.h"
#include "14-GestorAcceso/GestorAcceso.h"

// --- DEFINICIONES Y CONSTANTES ---
#define TIEMPO_LECTURA_NFC     500000  // Pequeña pausa entre lecturas
#define TIEMPO_VISUALIZACION   4000000 // Tiempo para leer mensajes largos (Acceso OK/NO)
#define TIEMPO_ESPERA_ADMIN    3000000 // Tiempo esperando que el admin quite su tarjeta
#define TIMEOUT_NUEVA_TARJETA  50      // Intentos para leer nueva tarjeta

// Estados del Sistema
enum EstadoSistema {
    ESTADO_ESPERANDO,       // "Acerque Tarjeta"
    ESTADO_VALIDANDO,       // Tarjeta detectada, decidiendo qué hacer
    ESTADO_MODO_ADMIN,      // Menú de administrador
    ESTADO_GESTION_USER   	// Agregar/Borrar user
};

// Delay bloqueante aproximado (para sustituir los for loops crudos)
void delay_aprox(volatile uint32_t ciclos) {
    while(ciclos > 0) ciclos--;
}

// Función segura para actualizar LCD asegurando que el mensaje salga
void actualizarPantalla(const char* l1, const char* l2) {
	// 1. Limpiamos la pantalla (Hardware) y el buffer (Software)
	// Esto asegura que el LCD quede vacio y sin basura vieja.
	//lcd->Clear();

	// 2. Escribimos las lineas nuevas
	if (l1) lcd->Set(l1, 0, 0);
	if (l2) lcd->Set(l2, 1, 0);

	// 3. Encendemos el motor del LCD (SysTick) para que procese los comandos
	SysTick->CTRL |= (1 << 0);

	// Damos tiempo al LCD para procesar el comando Clear (aprox 2ms) + los caracteres
	delay_aprox(50000);
}

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

// Convierte array de bytes a String Hex: {0xDE, 0xAD} -> "DE:AD"
void formatUidForLcd(uint8_t *uid, uint8_t len, char *buffer) {
    const char hex[] = "0123456789ABCDEF";
    int idx = 0; // Índice manual para el buffer de salida

    for(int i = 0; i < len; i++) {
        // Nibble alto (ej: A)
        buffer[idx++] = hex[(uid[i] >> 4) & 0x0F];

        // Nibble bajo (ej: 9)
        buffer[idx++] = hex[uid[i] & 0x0F];

        // Agregamos ":" solo si NO es el último byte
        if (i < (len - 1)) {
            buffer[idx++] = ':';
        }
    }
    buffer[idx] = '\0'; // Terminador nulo al final
}

Led L2( 0 , Callback_Leds_gpio , 200 ) ;
Led L3( 1 , Callback_Leds_gpio , 100 ) ;
Led L4( 2 , Callback_Leds_gpio , 300 ) ;

int main(void) {
	// 1. Inicialización
	InicializarInfotronic();
	L2.Off(); L3.Off(); L4.Off();

    actualizarPantalla("Sistema NFC", "Iniciando...");

    // 2. Drivers
	uart miUart(4, 0, 16, 0, 17, 115200);
	Nfc miNfc(&miUart);
	GestorAcceso controlAcceso;


	// 3. Configuración PN532 (Modo Silencio)
	miUart.clearRxBuffer();
	SysTick->CTRL &= ~(1 << 0); // Apagar LCD/SysTick
	bool configOk = miNfc.SAMConfig();
	SysTick->CTRL |= (1 << 0);  // Encender LCD/SysTick

	if (configOk) {
		L2.On();
		actualizarPantalla("Sistema NFC     ", "Listo!          ");
		delay_aprox(TIEMPO_VISUALIZACION);
		L2.Off();
	} else {
		L3.On();
		actualizarPantalla("Error Config", "Reinicie Sistema");
		while(1);
	}

	// Variables de trabajo
	uint8_t uid[7];
	uint8_t uidLen;
	char lcdBuffer[17];
	EstadoSistema estado = ESTADO_ESPERANDO;
	bool mensajeMostrado = false; // Flag para no refrescar LCD innecesariamente

	while(1) {
		switch(estado) {

			// --- ESTADO 1: ESPERANDO TARJETA ---
			case ESTADO_ESPERANDO:
				if (!mensajeMostrado) {
					actualizarPantalla("Acerque Tarjeta ", "                ");
					mensajeMostrado = true;
				}

				// Preparar lectura NFC
				miUart.clearRxBuffer();
				SysTick->CTRL &= ~(1 << 0); // Apagar interrupciones (Modo Atómico)

				if (miNfc.readPassiveTargetID(0x00, uid, &uidLen)) {
					// ¡Tarjeta encontrada!
					estado = ESTADO_VALIDANDO;
					mensajeMostrado = false;
				} else {
					// No hay tarjeta, reactivamos SysTick brevemente para refrescar display si hiciera falta
					// y hacemos un pequeño delay para no saturar
					SysTick->CTRL |= (1 << 0);
					delay_aprox(TIEMPO_LECTURA_NFC);
				}
				break;


			// --- ESTADO 2: VALIDANDO TARJETA ---
			case ESTADO_VALIDANDO:
				SysTick->CTRL |= (1 << 0); // Reactivar LCD para mostrar info

				// Verificar si es Admin
				if (controlAcceso.esAdmin(uid, uidLen)) {
					L4.Blink();
					actualizarPantalla("MODO ADMIN      ", "Suelte Tarjeta..");
					delay_aprox(TIEMPO_ESPERA_ADMIN); // Dar tiempo a quitar la tarjeta
					estado = ESTADO_GESTION_USER;
				}
				else {
					// Usuario Normal
					bool accesoPermitido = controlAcceso.validarAcceso(uid, uidLen);
					formatUidForLcd(uid, uidLen, lcdBuffer);

					// Mostrar UID
					actualizarPantalla("MODO ADMIN      ", "Suelte Tarjeta..");
					lcd->Set("UID:", 0, 0);
					lcd->Set(lcdBuffer, 0, 5);

					if (accesoPermitido) {
						L2.On();
						lcd->Set("ACCESO CONCEDIDO", 1, 0);
					} else {
						L3.On();
						lcd->Set("ACCESO DENEGADO ", 1, 0);
					}

					// Esperar para que el usuario lea
					delay_aprox(TIEMPO_VISUALIZACION);

					// Limpieza
					L2.Off(); L3.Off();
					estado = ESTADO_ESPERANDO;
				}
				break;


			// --- ESTADO 3: AGREGANDO NUEVO USUARIO ---
			case ESTADO_GESTION_USER:
				actualizarPantalla("MODO ADMIN      ", "Acerque Nueva...");

				int intentos = TIMEOUT_NUEVA_TARJETA;
				bool tarjetaDetectada = false;

				while (intentos > 0 && !tarjetaDetectada) {
					miUart.clearRxBuffer();
					SysTick->CTRL &= ~(1 << 0); // Silencio

					if (miNfc.readPassiveTargetID(0x00, uid, &uidLen)) {
						tarjetaDetectada = true;
					}

					SysTick->CTRL |= (1 << 0); // LCD ON

					if (!tarjetaDetectada) {
						delay_aprox(TIEMPO_LECTURA_NFC);
						intentos--;
					}
				}

				if (tarjetaDetectada) {
					// 1. Verificar si es el mismo ADMIN (Error)
					if (controlAcceso.esAdmin(uid, uidLen)) {
						actualizarPantalla("Error:          ", "Es el Admin!    ");
						L4.Blink();
					}
					// 2. Verificar si YA EXISTE (Eliminar)
					else if (controlAcceso.validarAcceso(uid, uidLen)) {
						if (controlAcceso.eliminarUsuario(uid, uidLen)) {
							L3.Blink(); // Rojo parpadea (feedback de borrado)
							actualizarPantalla("Usuario:        ", "ELIMINADO       ");
						} else {
							actualizarPantalla("Error:          ", "No se elimino   ");
						}
					}
					// 3. Si NO EXISTE (Agregar)
					else {
						controlAcceso.agregarUsuario(uid, uidLen);
						L2.Blink(); // Verde parpadea (feedback de guardado)
						actualizarPantalla("Usuario:        ", "AGREGADO        ");
					}
				} else {
					actualizarPantalla("Tiempo Agotado  ", "Cancelando...   ");
				}

				delay_aprox(TIEMPO_ESPERA_ADMIN);
				L2.Off(); L3.Off(); L4.Off();
				estado = ESTADO_ESPERANDO;
				mensajeMostrado = false;
				break;
		}
	}
	return 0;
}
