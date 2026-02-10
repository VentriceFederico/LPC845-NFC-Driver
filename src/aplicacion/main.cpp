#include "inicializarInfotronic.h"
#include "13-NFC/nfc.h"
#include "systick.h"
#include "14-GestorAcceso/GestorAcceso.h"

// --- DEFINICIONES Y CONSTANTES ---
#define T_VISUALIZACION   	30  // 3.0 Segundos (Mensajes ACCESO/ERROR)
#define T_ESPERA_ADMIN    	20  // 4.0 Segundos (Mensajes Admin)
#define T_REPOSO_LECTURA  	2   // 0.2 Segundos (Pausa entre intentos fallidos)
#define T_TIMEOUT_ADMIN   	50  // 5.0 Segundos (Tiempo máx para pasar tarjeta nueva)
#define T_ESPERA_PC  		10

// Estados del Sistema
enum EstadoSistema {
    ESTADO_ESPERANDO_TARJETA,       	// Polling NFC
    ESTADO_COOLDOWN_LECTURA,        	// Pausa corta si falló
    ESTADO_ANALIZANDO_UID,          	// Decide si es Admin o Usuario
	ESTADO_CONSULTANDO_PC,				// Consulta si el usuario tiene acceso
    ESTADO_MOSTRANDO_RESULTADO_ACCESO, 	// Mantiene mensaje "Concedido/Denegado"
    ESTADO_ADMIN_ESPERANDO_RETIRO,  	// Espera que Admin saque tarjeta
    ESTADO_ADMIN_ESPERANDO_NUEVA,   	// Espera tarjeta para ABM
	ESTADO_ADMIN_CONSULTANDO_PC,		// Consulta si se agrego o borro usuario
    ESTADO_ADMIN_MOSTRANDO_FINAL    	// Muestra "Agregado/Eliminado" o "Cancelado"
};

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

char respuestaPC = 0;
uart uartPC(1, 0, 18, 0, 19, 9600);

int main(void) {
	// 1. Inicialización
	InicializarInfotronic();
	L2.Off(); L3.Off(); L4.Off();
	timer Cronometro(timer::DEC);
    actualizarPantalla("  Sistema NFC   ", "Iniciando...    ");

    // 2. Drivers
	uart miUart(4, 0, 16, 0, 17, 115200);
	Nfc miNfc(&miUart);
	GestorAcceso controlAcceso(&uartPC);

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
					estado = ESTADO_ANALIZANDO_UID;
				} else {
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
				if (controlAcceso.esAdmin(uid, uidLen)) {
					L4.Blink();
					actualizarPantalla("   MODO ADMIN   ", " Suelte Tarjeta ");
					Cronometro = T_ESPERA_ADMIN;
					estado = ESTADO_ADMIN_ESPERANDO_RETIRO;
				}
				else {
					// 2. CHEQUEO USUARIO
					actualizarPantalla(" Consultando... ", "   Espere...    ");

					controlAcceso.enviarSolicitudAcceso(uid, uidLen);

					Cronometro = T_ESPERA_PC;
					estado = ESTADO_CONSULTANDO_PC;
				}
				break;

			// ---------------------------------------------------------
			// 4. ESPERA ACTIVA
			// ---------------------------------------------------------
			case ESTADO_CONSULTANDO_PC:
				if (Cronometro == 0) {
					L3.On();
					actualizarPantalla("  Error de Red  ", " PC No Responde ");
					Cronometro = T_VISUALIZACION;
					estado = ESTADO_MOSTRANDO_RESULTADO_ACCESO;
				}
				else if (controlAcceso.leerRespuesta(respuestaPC)) {
					if (respuestaPC == '1') {
						L2.On();
						actualizarPantalla(" ACCESO ONLINE  ", "   CONCEDIDO    ");
					} else {
						L3.On();
						actualizarPantalla(" ACCESO ONLINE  ", "    DENEGADO    ");
					}
					Cronometro = T_VISUALIZACION;
					estado = ESTADO_MOSTRANDO_RESULTADO_ACCESO;
				}
				break;
			// ---------------------------------------------------------
			// 5. DISPLAY DE RESULTADO (USUARIO NORMAL)
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
			// 6. FLUJO ADMIN: ESPERAR QUE SAQUE LA TARJETA
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
			// 7. FLUJO ADMIN: ESPERAR NUEVA TARJETA O TIMEOUT
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
					actualizarPantalla(" Procesando...  ", " Consulte a PC  ");

					controlAcceso.enviarSolicitudGestion(uid, uidLen);

					Cronometro = T_ESPERA_PC;
					estado = ESTADO_ADMIN_CONSULTANDO_PC;
				}
				break;
			// ---------------------------------------------------------
			// 8. ADMIN: ESPERA RESPUESTA
			// ---------------------------------------------------------
			case ESTADO_ADMIN_CONSULTANDO_PC:
				 if (Cronometro == 0) {
					actualizarPantalla("    Error PC    ", " Sin Respuesta  ");
					L4.Off();
					Cronometro = T_ESPERA_ADMIN;
					estado = ESTADO_ADMIN_MOSTRANDO_FINAL;
				 }
				 else if (controlAcceso.leerRespuesta(respuestaPC)) {
					 if (respuestaPC == 'A') {
						 L2.Blink();
						 actualizarPantalla(" PC Base Datos: ", "  AGREGADO OK   ");
					 } else if (respuestaPC == 'B') {
						 L3.Blink();
						 actualizarPantalla(" PC Base Datos: ", "  ELIMINADO OK  ");
					 } else {
						 actualizarPantalla("    Error PC    ", "  Desconocido   ");
					 }
					 L4.Off();
					 Cronometro = T_ESPERA_ADMIN;
					 estado = ESTADO_ADMIN_MOSTRANDO_FINAL;
				 }
				 break;
			// ---------------------------------------------------------
			// 9. DISPLAY FINAL DE ADMIN (Agregado/Borrado/Cancelado)
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
