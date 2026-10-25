#pragma once

// EL ESTADO DE LA PARTIDA, compartido por los scripts del puzzle:
//
//   PuzzleDirector  lo carga, revela la luz y crea los objetos de las piezas
//   FigureBuilder   lo cambia: coloca, rota y quita piezas con el raton
//   GameHud         lo muestra y atiende sus botones y teclas
//   SurfaceLight    le reparte la luz a cada superficie
//
// Un estatico y no mensajes: los scripts viven en el mismo Scripts.dll. Sin
// motor: lo que hay aqui se prueba sin ventana.

#include "BeamPath.h"

#include <string>
#include <vector>

namespace gravitational {

// Que hace el clic izquierdo sobre el mundo. Poner lleva ademas QUE pieza.
enum class Tool { Place, Rotate, Remove };

// Un rectangulo de pantalla, sin motor (el HUD los publica para que el clic
// sobre un boton no sea tambien un clic sobre el mundo).
struct ScreenRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

struct PuzzleSession {
    Board board;
    Trace trace;
    BeamPath beam;
    bool loaded = false;

    // Cuanto de la luz se ve, en METROS de recorrido.
    float progress = 0.0f;
    bool paused = false;

    bool solved = false;
    float solvedSeconds = 0.0f;
    float elapsed = 0.0f;
    int moves = 0;

    Tool tool = Tool::Place;
    Shape shape = Shape::Sphere;

    // Sube con cada cambio del tablero: quien dibuja compara con la suya y se
    // rehace solo cuando hace falta.
    int revision = 0;

    // DESHACER: el tablero de antes de cada jugada. Con niveles de ocho o diez
    // piezas, equivocarse en la sexta y tener que REINICIAR era perder las
    // cinco buenas.
    std::vector<Board> history;

    // --- Lo que mira el HUD ---
    int levelIndex = 0, levelCount = 0;
    int hoverFigure = -1;                // la pieza bajo el cursor (FigureBuilder), o -1
    std::string notice;                  // un aviso corto ("IT DOES NOT FIT THERE")
    float noticeTime = 0.0f;
    std::vector<ScreenRect> hudRects;    // donde hay HUD: ahi el clic no es del mundo
    bool resetRequested = false;         // el director recarga el nivel

    // Recalcular todo y revelar desde la fuente: el jugador tiene que ver la
    // consecuencia entera de su jugada, no el recorrido nuevo a partir de donde
    // iba el viejo.
    void Recompute() {
        trace = TraceLight(board);
        beam = BuildBeamPath(board, trace);
        progress = 0.0f;
        solved = false;
        solvedSeconds = 0.0f;
        ++revision;
    }

    // Antes de cambiar el tablero: guardarlo para deshacer.
    void Remember() {
        history.push_back(board);
        if (history.size() > 128) history.erase(history.begin());
    }
    // Despues de cambiarlo.
    void Commit() {
        ++moves;
        Recompute();
    }
    bool Undo() {
        if (history.empty() || solved) return false;
        board = history.back();
        history.pop_back();
        ++moves;
        Recompute();
        return true;
    }

    void Notice(const std::string& text) {
        notice = text;
        noticeTime = 0.0f;
    }

    // La herramienta del principio de un nivel: la primera pieza que haya.
    void SelectFirstAvailable() {
        tool = Tool::Place;
        for (int i = 0; i < kShapeCount; ++i)
            if (board.inventory[i] > 0) {
                shape = static_cast<Shape>(i);
                return;
            }
    }

    bool PointerOverHud(float x, float y) const {
        for (const ScreenRect& r : hudRects)
            if (r.Contains(x, y)) return true;
        return false;
    }

    bool FullyRevealed() const { return progress >= beam.length; }
};

inline PuzzleSession& Session() {
    static PuzzleSession session;
    return session;
}

} // namespace gravitational
