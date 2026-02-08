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
    ESTADO_ESPERANDO_TARJETA,       	// Polling NFC
    ESTADO_COOLDOWN_LECTURA,        	// Pausa corta si falló
    ESTADO_ANALIZANDO_UID,          	// Decide si es Admin o Usuario
    ESTADO_MOSTRANDO_RESULTADO_ACCESO, 	// Mantiene mensaje "Concedido/Denegado"
    ESTADO_ADMIN_ESPERANDO_RETIRO,  	// Espera que Admin saque tarjeta
    ESTADO_ADMIN_ESPERANDO_NUEVA,   	// Espera tarjeta para ABM
    ESTADO_ADMIN_MOSTRANDO_FINAL    	// Muestra "Agregado/Eliminado" o "Cancelado"
};


uart uartPC(1, 0, 18, 0, 19, 9600);

// Función segura para actualizar LCD asegurando que el mensaje salga
void actualizarPantalla(const char* l1, const char* l2) {
	if (l1) lcd->Set(l1, 0, 0);
	if (l2) lcd->Set(l2, 1, 0);
}

// Convierte array de bytes a String Hex: {0xDE, 0xAD} -> "DE:AD"
void formatUidForLcd(uint8_t *uid, uint8_t len, char *buffer) {
    const char hex[] = "0123456789ABCDEF";
    int idx = 0;

    if(len > 7) len = 7;

    for(int i = 0; i < len; i++) {
        buffer[idx++] = hex[(uid[i] >> 4) & 0x0F];
        buffer[idx++] = hex[uid[i] & 0x0F];
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
    actualizarPantalla("  Sistema NFC   ", "Iniciando...    ");

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
	uint8_t uid[12] = {0};
	uint8_t uidLen;
	uint8_t sak;
	char lcdBuffer[32] = {0};
	bool lecturaExitosa = false;
	bool nuevoUsr = false;

	actualizarPantalla("Acerque Tarjeta ", "  o Celular...  ");
	EstadoSistema estado = ESTADO_ESPERANDO_TARJETA;

	while(1) {

		switch(estado) {

			// ---------------------------------------------------------
			// 1. ESPERA ACTIVA (POLLING)
			// ---------------------------------------------------------
			case ESTADO_ESPERANDO_TARJETA: {
				// Nota: La pantalla ya se actualizó antes de entrar a este estado

				miUart.clearRxBuffer();
				lecturaExitosa = false;

				if (miNfc.readPassiveTargetID(0x00, uid, &uidLen, &sak)) {
					if (sak & 0x20) {
						if (miNfc.negotiateWithPhone(uid, &uidLen)) lecturaExitosa = true;
					} else {
						lecturaExitosa = true;
					}
				}

				if (lecturaExitosa) {
					// Transición -> ANALIZAR
					estado = ESTADO_ANALIZANDO_UID;
				} else {
					// Transición -> PAUSA CORTA (Para no saturar)
					Cronometro = T_REPOSO_LECTURA;
					estado = ESTADO_COOLDOWN_LECTURA;
				}
				break;
			}

			// ---------------------------------------------------------
			// 2. PAUSA CORTA (Debounce/Polling Rate)
			// ---------------------------------------------------------
			case ESTADO_COOLDOWN_LECTURA:
				if (Cronometro == 0) {
					estado = ESTADO_ESPERANDO_TARJETA;
				}
				break;

			// ---------------------------------------------------------
			// 3. LÓGICA DE DECISIÓN
			// ---------------------------------------------------------
			case ESTADO_ANALIZANDO_UID:
				formatUidForLcd(uid, uidLen, lcdBuffer);

				if (controlAcceso.esAdmin(uid, uidLen)) {
					// === ES ADMIN ===
					L4.Blink();
					actualizarPantalla("   MODO ADMIN   ", " Suelte Tarjeta ");

					// Configuración para el siguiente estado
					Cronometro = T_ESPERA_ADMIN;
					estado = ESTADO_ADMIN_ESPERANDO_RETIRO;
				}
				else {
					// === ES USUARIO ===
					if (controlAcceso.validarAcceso(uid, uidLen)) {
						L2.On();
						actualizarPantalla("ACCESO CONCEDIDO", "                ");
						loguearAcceso("CONCEDIDO", uid, uidLen);
					} else {
						L3.On();
						actualizarPantalla("ACCESO DENEGADO ", "                ");
						lcd->Set(lcdBuffer, 1, 0); // Muestra UID
						loguearAcceso("DENEGADO", uid, uidLen);
					}

					// Configuración para el siguiente estado (Visualización)
					Cronometro = T_VISUALIZACION;
					estado = ESTADO_MOSTRANDO_RESULTADO_ACCESO;
				}
				break;

			// ---------------------------------------------------------
			// 4. DISPLAY DE RESULTADO (USUARIO NORMAL)
			// ---------------------------------------------------------
			case ESTADO_MOSTRANDO_RESULTADO_ACCESO:
				if (Cronometro == 0) {
					// Limpieza y vuelta al inicio
					L2.Off(); L3.Off();
					actualizarPantalla("Acerque Tarjeta ", "  o Celular...  ");
					estado = ESTADO_ESPERANDO_TARJETA;
				}
				break;

			// ---------------------------------------------------------
			// 5. FLUJO ADMIN: ESPERAR QUE SAQUE LA TARJETA
			// ---------------------------------------------------------
			case ESTADO_ADMIN_ESPERANDO_RETIRO:
				if (Cronometro == 0) {
					// Tiempo cumplido, ahora pedimos la nueva
					actualizarPantalla("   MODO ADMIN   ", "Acerque Nuevo...");
					L4.On(); // Dejar fijo para indicar "Esperando"

					Cronometro = T_TIMEOUT_ADMIN;
					estado = ESTADO_ADMIN_ESPERANDO_NUEVA;
				}
				break;

			// ---------------------------------------------------------
			// 6. FLUJO ADMIN: ESPERAR NUEVA TARJETA O TIMEOUT
			// ---------------------------------------------------------
			case ESTADO_ADMIN_ESPERANDO_NUEVA:
				// A. Chequeo de Timeout
				if (Cronometro == 0) {
					actualizarPantalla("Tiempo Agotado  ", " Cancelando...  ");
					L4.Off();

					Cronometro = T_VISUALIZACION; // Tiempo para leer "Cancelando"
					estado = ESTADO_ADMIN_MOSTRANDO_FINAL;
					break;
				}

				// B. Chequeo de Lectura
				miUart.clearRxBuffer();
				nuevoUsr = false;
				// Lectura rápida (simplificada sin negotiate para este paso)
				if (miNfc.readPassiveTargetID(0x00, uid, &uidLen, &sak)) nuevoUsr = true;
				if (sak & 0x20){
					miNfc.negotiateWithPhone(uid, &uidLen);
				}
				if (nuevoUsr) {
					// PROCESAMIENTO ABM (Alta/Baja)
					formatUidForLcd(uid, uidLen, lcdBuffer);

					if (controlAcceso.esAdmin(uid, uidLen)) {
						 actualizarPantalla("Error:          ", "Es el Admin!    ");
					}
					else if (controlAcceso.validarAcceso(uid, uidLen)) {
						controlAcceso.eliminarUsuario(uid, uidLen);
						L3.Blink();
						actualizarPantalla("Usuario:        ", "ELIMINADO       ");
					} else {
						controlAcceso.agregarUsuario(uid, uidLen);
						L2.Blink();
						actualizarPantalla("Usuario:        ", "AGREGADO        ");
					}

					// Configuración para mostrar resultado
					L4.Off();
					Cronometro = T_ESPERA_ADMIN; // Un poco más de tiempo para ver que pasó
					estado = ESTADO_ADMIN_MOSTRANDO_FINAL;
				}
				break;

			// ---------------------------------------------------------
			// 7. DISPLAY FINAL DE ADMIN (Agregado/Borrado/Cancelado)
			// ---------------------------------------------------------
			case ESTADO_ADMIN_MOSTRANDO_FINAL:
				if (Cronometro == 0) {
					// Vuelta a casa
					L2.Off(); L3.Off(); L4.Off();
					actualizarPantalla("Acerque Tarjeta ", "  o Celular...  ");
					estado = ESTADO_ESPERANDO_TARJETA;
				}
				break;
		}
	}
}
