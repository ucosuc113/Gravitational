#pragma once

// El dedo de la partida (Flow/Touch.h), con la UNICA respuesta a "esta sobre la
// interfaz": la UI de la escena o un rectangulo del HUD (casillas, zoom, menu).
// OrbitCamera y FigureBuilder lo piden los dos; el primero que corre en el
// frame lo actualiza, y los dos tienen que hacer la misma pregunta - si no, el
// resultado dependeria del orden de los scripts.

#include "../Flow/GameFlow.h"
#include "../Flow/Touch.h"
#include "../Puzzle/PuzzleSession.h"

#include "Engine/Input/InputService.h"
#include "Engine/UI/SceneUi.h"

namespace gravitational {

inline TouchGesture& PlayTouch() {
    using namespace strace;
    const InputActions& in = InputService::Actions();
    // Fuera de la partida (menu, paso entre niveles) todo cuenta como interfaz:
    // un dedo que apoyo en el menu no gira la camara ni pone una pieza al
    // levantarse ya en el juego.
    const bool overUi = !Flow().InGame() || Flow().passage.Active() || SceneUi::PointerOverUi() ||
                        Session().PointerOverHud(in.CursorX(), in.CursorY());
    TouchGesture& g = GameTouch();
    g.UpdateOnce(overUi);
    return g;
}

} // namespace gravitational
