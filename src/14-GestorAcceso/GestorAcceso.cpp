/*
 * GestorAcceso.cpp
 *
 *  Created on: 5 feb. 2026
 *      Author: ventr
 */

#include <14-GestorAcceso/GestorAcceso.h>

GestorAcceso::GestorAcceso() {
	Usuario admin;
	admin.len = 4;
	admin.uid[0] = 0xA9; admin.uid[1] = 0x26; admin.uid[2] = 0xCA; admin.uid[3] = 0x01;
	admin.esAdmin = true;

	listaAutorizados.push_back(admin);

}

GestorAcceso::~GestorAcceso() {
	// TODO Auto-generated destructor stub
}

// Metodos Publicos

bool GestorAcceso::validarAcceso(uint8_t* uid, uint8_t len){
	for(Usuario& u : listaAutorizados){
		if(u.len != len){
			continue;
		}

		if(memcmp(u.uid, uid, len) == 0){
			return true;
		}

	}
	return false;
}

bool GestorAcceso::esAdmin(uint8_t* uid, uint8_t len){
	for(Usuario& u : listaAutorizados){
		if(u.esAdmin && u.len == len && memcmp(u.uid, uid, len) == 0){
			return true;
		}
	}
	return false;
}

void GestorAcceso::agregarUsuario(uint8_t* nuevoUid, uint8_t len){
	if(validarAcceso(nuevoUid, len)){
		return;
	}

	Usuario u;

	if (len > 7) len = 7;

	u.len = len;
	memcpy(u.uid, nuevoUid, len); //Copia bytes
	u.esAdmin = false;

	listaAutorizados.push_back(u);
}

int GestorAcceso::cantidadUsuarios(){
	return listaAutorizados.size();
}

bool GestorAcceso::eliminarUsuario(uint8_t* uid, uint8_t len){
	size_t sizeIni = listaAutorizados.size();

	listaAutorizados.erase(std::remove_if(listaAutorizados.begin(), listaAutorizados.end(), [uid](const Usuario& u) {return std::memcmp(u.uid, uid, 4) == 0x00;}), listaAutorizados.end());
	return listaAutorizados.size() < sizeIni;
}
