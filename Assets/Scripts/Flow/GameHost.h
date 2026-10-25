#pragma once

// LO QUE EL JUEGO NECESITA DE QUIEN LO CORRE: salir, donde guardar, y aplicar
// los ajustes al motor. Es la misma idea que Services/Host de The Freak Place:
// el motor todavia no tiene `Application::RequestQuit` ni una carpeta de datos
// del jugador, asi que viven aqui envueltas, y el dia que el motor las tenga
// cambia UN archivo.

#include "GameFlow.h"

#include <string>

namespace gravitational::host {

// true dentro del Editor (en Play): por el nombre del ejecutable.
bool IsEditor();

// En un Build cierra la ventana (PostQuitMessage). En el Editor no hace nada
// mas que avisar en el log - cerrar el Editor desde un script se llevaria una
// escena sin guardar - y devuelve false para que el menu lo diga en pantalla.
bool RequestQuit();

// El progreso y los ajustes, en %LOCALAPPDATA%\Gravitational\ (o
// Gravitational-Editor\ en el Editor: probar no pisa la partida de un Build).
void LoadProgress(GameFlow& flow);
void SaveProgress(const GameFlow& flow);

// Los ajustes que son del motor (antialias, vsync). Los del juego (velocidad
// de la luz, sensibilidad) los lee cada script de GameFlow.
void ApplySettings(const GameSettings& s);

} // namespace gravitational::host
