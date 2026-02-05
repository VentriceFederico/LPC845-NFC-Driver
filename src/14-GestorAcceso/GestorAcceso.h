/*
 * GestorAcceso.h
 *
 *  Created on: 5 feb. 2026
 *      Author: ventr
 */

#ifndef SRC_14_GESTORACCESO_GESTORACCESO_H_
#define SRC_14_GESTORACCESO_GESTORACCESO_H_

#include "LPC845.h"
#include <vector>
#include <cstring>
#include <algorithm>

// Estructura para representar una tarjeta/usuario
struct Usuario {
    uint8_t uid[4];
    uint8_t len;
    bool esAdmin; //Para distinguir Admin de usuarios normales
};

class GestorAcceso {
private:
	std::vector<Usuario> listaAutorizados;

public:
	GestorAcceso();
	virtual ~GestorAcceso();

	bool validarAcceso(uint8_t* uid, uint8_t len);
	bool esAdmin(uint8_t* uid, uint8_t len);
	void agregarUsuario(uint8_t* uid, uint8_t len);
	bool eliminarUsuario(uint8_t* uid, uint8_t len);
	int cantidadUsuarios();
};

#endif /* SRC_14_GESTORACCESO_GESTORACCESO_H_ */
