#include "inicializarInfotronic.h"
#include "13-NFC/nfc.h"
#include "systick.h"
#include "14-GestorAcceso/GestorAcceso.h"

// --- DEFINICIONES Y CONSTANTES ---
#define T_VISUALIZACION   30  // 3.0 Segundos (Mensajes ACCESO/ERROR)
#define T_ESPERA_ADMIN    40  // 4.0 Segundos (Mensajes Admin)
#define T_REPOSO_LECTURA  2   // 0.2 Segundos (Pausa entre intentos fallidos)
#define T_TIMEOUT_ADMIN   50  // 5.0 Segundos (Tiempo máx para pasar tarjeta nueva)

// Estados del Sistema
enum EstadoSistema {
    ESTADO_ESPERANDO,       // "Acerque Tarjeta..."
    ESTADO_REPOSO_LECTURA,  // Pequeña pausa no bloqueante si falló lectura
    ESTADO_VALIDANDO,       // Procesando UID
    ESTADO_MOSTRANDO_INFO,  // Mostrando resultado (Verde/Rojo) sin bloquear
    ESTADO_GESTION_USER,    // Lógica de Admin
    ESTADO_ESPERA_ADMIN,    // Mostrando resultado de Admin sin bloquear
    ESTADO_TIMEOUT_ADMIN    // Esperando que pase nueva tarjeta en modo admin
};


uart uartPC(1, 0, 18, 0, 19, 9600);

// Función segura para actualizar LCD asegurando que el mensaje salga
void actualizarPantalla(const char* l1, const char* l2) {
	if (l1) lcd->Set(l1, 0, 0);
	if (l2) lcd->Set(l2, 1, 0);
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

// Envía los datos por partes para no usar sprintf y ahorrar memoria Flash
void loguearAcceso(const char* estado, uint8_t* uid, uint8_t len) {
    char uidStr[32];

    // 1. Convertimos el UID a texto
    formatUidForLcd(uid, len, uidStr);

    // 2. Enviamos las partes por separado directamente a la UART
    // Parte 1: Cabecera
    const char* p1 = "ACCESO: ";
    uartPC.Transmit(p1, strlen(p1));

    // Parte 2: El Estado (CONCEDIDO / DENEGADO)
    uartPC.Transmit(estado, strlen(estado));

    // Parte 3: Separador
    const char* p3 = " | UID: ";
    uartPC.Transmit(p3, strlen(p3));

    // Parte 4: El UID
    uartPC.Transmit(uidStr, strlen(uidStr));

    // Parte 5: Nueva linea
    const char* p5 = "\r\n";
    uartPC.Transmit(p5, strlen(p5));
}

int main(void) {
	// 1. Inicialización
	InicializarInfotronic();
	L2.Off(); L3.Off(); L4.Off();

	timer Cronometro(timer::DEC);

    actualizarPantalla("Sistema NFC", "Iniciando...");

    // 2. Drivers
	uart miUart(4, 0, 16, 0, 17, 115200);
	Nfc miNfc(&miUart);
	GestorAcceso controlAcceso;


	// 3. Configuración PN532
	miUart.clearRxBuffer();
	bool configOk = miNfc.SAMConfig();

	if (configOk) {
		L2.On();
		actualizarPantalla("Sistema NFC     ", "Listo!          ");
		Cronometro = 20; // 2 seg
		while(!Cronometro);
		L2.Off();
	} else {
		L3.On();
		actualizarPantalla("Error Config    ", "Reinicie Sistema");
		while(1);
	}

	// Variables de trabajo
	uint8_t uid[4];
	uint8_t uidLen;
	uint8_t sak;
	char lcdBuffer[17];

	EstadoSistema estado = ESTADO_ESPERANDO;
	EstadoSistema estadoSiguiente = ESTADO_ESPERANDO;
	bool mensajeMostrado = false; // Flag para no refrescar LCD innecesariamente

	bool nuevoUsuarioDetectado = false;

	while(1) {

		// IMPORTANTE: La librería 'timers' depende del SysTick.
		// Como quitamos el BloqueoLCD, el SysTick corre siempre y los timers funcionan bien.
		bool lecturaExitosa = false;
		switch(estado) {

			// --- 1. BUSCAR TARJETA ---
			case ESTADO_ESPERANDO:
				if (!mensajeMostrado) {
					actualizarPantalla("Acerque Tarjeta ", "  o Celular...  ");
					mensajeMostrado = true;
				}

				// Intentamos leer (Esto dura lo que dure el timeout del PN532, aprox 100ms)
				miUart.clearRxBuffer();
				lecturaExitosa = false;

				if (miNfc.readPassiveTargetID(0x00, uid, &uidLen, &sak)) {
					if (sak & 0x20) {
						// Es celular
						if (miNfc.negotiateWithPhone(uid, &uidLen)) {
							lecturaExitosa = true;
						}
					} else {
						// Es tarjeta
						lecturaExitosa = true;
					}
				}

				if (lecturaExitosa) {
					estado = ESTADO_VALIDANDO;
					mensajeMostrado = false;
				} else {
					// Si falló, vamos a un reposo NO BLOQUEANTE
					Cronometro = T_REPOSO_LECTURA;
					estado = ESTADO_REPOSO_LECTURA;
				}
				break;


			// --- 1.5 PAUSA ENTRE LECTURAS ---
			case ESTADO_REPOSO_LECTURA:
				// Si el timer expiró (operator! devuelve false cuando vence, segun tu lib)
				// OJO: Chequea cómo funciona tu operator!.
				// Asumo: if (!Cronometro) es "mientras cuenta".
				// Entonces el else (o validación positiva) es "venció".

				// Opción A: Si tu librería usa `if (Cronometro == 0)` para ver si terminó:
				// Opción B: Si tu librería usa el operador bool implícito.

				// Usaré esta lógica estándar: "Si NO está contando (!Cronometro da false), entonces terminó"
				// Ajusta esta línea según tu timer.cpp:
				if (Cronometro == 0) {
					estado = ESTADO_ESPERANDO;
				}
				break;


			// --- 2. VALIDAR PERMISOS ---
			case ESTADO_VALIDANDO:
				formatUidForLcd(uid, uidLen, lcdBuffer);

				if (controlAcceso.esAdmin(uid, uidLen)) {
					L4.Blink();
					actualizarPantalla("MODO ADMIN      ", "Suelte Dispositi");

					// Esperamos un momento para que saque la tarjeta admin
					Cronometro = T_ESPERA_ADMIN;
					estado = ESTADO_ESPERA_ADMIN;
					estadoSiguiente = ESTADO_TIMEOUT_ADMIN; // Siguiente paso lógico
				}
				else {
					// Usuario Normal
					if (controlAcceso.validarAcceso(uid, uidLen)) {
						L2.On();
						actualizarPantalla("ACCESO CONCEDIDO", "                ");
						loguearAcceso("CONCEDIDO", uid, uidLen);
					} else {
						L3.On();
						actualizarPantalla("ACCESO DENEGADO ", "                ");
						lcd->Set(lcdBuffer, 1, 0); // Mostrar UID al denegar
						loguearAcceso("DENEGADO", uid, uidLen);
					}

					// Mostramos el mensaje por 3 segundos
					Cronometro = T_VISUALIZACION;
					estado = ESTADO_MOSTRANDO_INFO;
					estadoSiguiente = ESTADO_ESPERANDO;
				}
				break;


			// --- 3. ESTADO VISUALIZADOR (GENÉRICO) ---
			// Sirve para congelar un mensaje en pantalla X segundos sin congelar la CPU
			case ESTADO_MOSTRANDO_INFO:
			case ESTADO_ESPERA_ADMIN:
				if (Cronometro == 0) {
					L2.Off(); L3.Off(); L4.Off();
					estado = estadoSiguiente;
					mensajeMostrado = false;
				}
				break;


			// --- 4. GESTIÓN ADMIN (Esperando nueva tarjeta) ---
			case ESTADO_TIMEOUT_ADMIN:
				if (!mensajeMostrado) {
					actualizarPantalla("MODO ADMIN      ", "Acerque Nuevo...");
					Cronometro = T_TIMEOUT_ADMIN; // 5 segundos para acercar algo
					mensajeMostrado = true;
					nuevoUsuarioDetectado = false;
				}

				// Chequeamos Timer de Salida
				if (Cronometro == 0) {
					actualizarPantalla("Tiempo Agotado  ", " Cancelando...  ");
					Cronometro = T_VISUALIZACION;
					estado = ESTADO_MOSTRANDO_INFO;
					estadoSiguiente = ESTADO_ESPERANDO;
					break;
				}

				// Intentamos leer (Polling rápido)
				miUart.clearRxBuffer();
				if (miNfc.readPassiveTargetID(0x00, uid, &uidLen, &sak)) {
					// Nota: Aquí simplifiqué la lógica celular/tarjeta para el ejemplo
					// pero deberías usar la misma negociación que arriba si quieres soportar Celus.
					 if (sak & 0x20) {
						 if (miNfc.negotiateWithPhone(uid, &uidLen)) nuevoUsuarioDetectado = true;
					 } else {
						 nuevoUsuarioDetectado = true;
					 }
				}

				if (nuevoUsuarioDetectado) {
					estado = ESTADO_GESTION_USER; // Vamos a procesar
				}
				break;


			// --- 5. PROCESAR ALTA/BAJA ---
			case ESTADO_GESTION_USER:
				// Aquí ya tenemos el UID nuevo en la variable 'uid'
				formatUidForLcd(uid, uidLen, lcdBuffer);

				if (controlAcceso.esAdmin(uid, uidLen)) {
					actualizarPantalla("Error:          ", "  Es el Admin!  ");
				}
				else if (controlAcceso.validarAcceso(uid, uidLen)) {
					controlAcceso.eliminarUsuario(uid, uidLen);
					L3.Blink();
					actualizarPantalla("Usuario:         ", "   ELIMINADO    ");
				}
				else {
					controlAcceso.agregarUsuario(uid, uidLen);
					L2.Blink();
					actualizarPantalla("Usuario:         ", "    AGREGADO    ");
				}

				// Mostramos resultado y volvemos al inicio
				Cronometro = T_ESPERA_ADMIN;
				estado = ESTADO_MOSTRANDO_INFO;
				estadoSiguiente = ESTADO_ESPERANDO;
				break;
		}
	}
}
