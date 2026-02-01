#include "inicializarInfotronic.h"
#include "13-NFC/nfc.h"

nfc *my_nfc;
timer globalTimer(timer::DEC);

// Helper para visualizar el UID (copiado por seguridad si no estaba en tus libs)
void arrayToHexStr(uint8_t *data, uint8_t len, char *str) {
    const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < len; i++) {
        str[i * 2] = hex[(data[i] >> 4) & 0x0F];
        str[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    str[len * 2] = '\0';
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

int main(void) {
    // 1. Inicialización del Hardware
    InicializarInfotronic();

    // UART4: Tx P0.16, Rx P0.17
    // Instancia del driver NFC
    my_nfc = new nfc(4, 0, 16, 0, 17);

    lcd->Set("NFC PN532 FSM   ", 0, 0);
    lcd->Set("Iniciando...    ", 1, 0);

    // 1. Despertar al módulo
	bool state = my_nfc->wakeUp();

	// Espera inicial (500ms) usando el timer global
	globalTimer.TimerStart(5);
	while(!globalTimer) { // Esperamos mientras NO venza
		my_nfc->Tick();
	}

	if(state){
		lcd->Set("NFC Listo!      ", 1, 0);
		L1.On();
	}

    bool lecturaEnCurso = false;
	char lcdBuffer[17];
	uint8_t uidLength;

	// Timer configurado para vencer inmediatamente la primera vez
	globalTimer.TimerStart(0);

	while(1) {
		// --- MANTENER VIVO EL DRIVER ---
		my_nfc->Tick();

		if (!lecturaEnCurso) {
			// --- FASE 1: INICIAR LECTURA ---
			if (globalTimer) {
				my_nfc->startReadPassiveTargetID();
				lecturaEnCurso = true;
			}
		}
		else {
			// --- FASE 2: ESPERAR RESULTADO ---
			if (!my_nfc->isBusy()) {
				lecturaEnCurso = false; // Terminó la transacción

				if (my_nfc->isCardPresent()) {
					// --- TARJETA DETECTADA ---
					L2.On();

					const uint8_t* uid = my_nfc->getUid();
					uidLength = my_nfc->getUidLength();

					lcd->Set("UID Detectado:  ", 0, 0);
					arrayToHexStr((uint8_t*)uid, uidLength, lcdBuffer);
					lcd->Set("                ", 1, 0);
					lcd->Set(lcdBuffer, 1, 0);

					// Pausa visual de 1 segundo
					globalTimer.TimerStart(10);
					while(!globalTimer) { // Espera bloqueante pero con Tick()
						 my_nfc->Tick();
					}
					L2.Off();

					// Esperar 500ms antes de permitir nueva lectura
					globalTimer.TimerStart(5);
				}
				else {
					// --- SIN TARJETA ---
					// Reintentar en 200ms
					globalTimer.TimerStart(2);
				}
			}
		}
	}

	return 0;
}
