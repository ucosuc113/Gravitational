#pragma once

// EL FLUJO DEL JUEGO: menu o partida, que nivel pedir, el paso de un nivel a
// otro, el progreso y los ajustes. El menu (Scripts/Menu/MainMenu.cpp) es
// PRESENTACION sobre esto, y el juego (PuzzleDirector, FigureBuilder,
// OrbitCamera, SurfaceLight) solo mira este archivo, nunca al menu: cambiar de
// menu no toca el juego (ya paso: hubo tres prototipos, ver
// Archive/menus-descartados).
//
// Un estatico y no mensajes, como PuzzleSession: todos los scripts viven en el
// mismo Scripts.dll.
//
// Sin menu en la escena, nadie llama a EnterMenu y el modo es Game desde el
// primer frame; sin LevelTransition, los niveles se suceden de golpe como al
// principio. Las dos piezas son opcionales para el juego.
//
// La escena NO se cambia: el motor no deja cargar otra escena desde un script
// (docs/API-motor.md, seccion 12). El menu y la partida viven en la misma
// escena y se turnan: el director esconde su mundo y su HUD mientras hay menu,
// y el menu esconde el suyo mientras hay partida.

#include <cstdio>
#include <string>
#include <vector>

namespace gravitational {

enum class FlowMode { Game, Menu };

// La pantalla del menu a la que se vuelve. Cada menu la interpreta a su
// manera (A: un panel; B: una lamina; C: una cara de la caja).
enum class MenuScreen { Title, Main, Levels, Options, Credits };

struct GameSettings {
    int lightSpeed = 2;      // indice en kLightSpeeds
    int sensitivity = 2;     // indice en kSensitivities
    bool invertOrbit = false;
    int antialias = 0;       // 0 no, 1 x2, 2 x4
    bool vsync = true;
};

inline constexpr float kLightSpeeds[] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f};
inline constexpr const char* kLightSpeedNames[] = {"X0.5", "X0.75", "X1", "X1.5", "X2", "X3"};
inline constexpr float kSensitivities[] = {0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
inline constexpr const char* kSensitivityNames[] = {"50%", "75%", "100%", "125%", "150%", "200%"};
inline constexpr const char* kAntialiasNames[] = {"NO", "X2", "X4"};

// EL PASO ENTRE NIVELES (Flow/LevelTransition.cpp): el nivel resuelto pierde
// sus dimensiones (caja -> plano -> linea -> punto), del punto sale la linea
// que escribe el nombre del siguiente, y el siguiente crece al reves. Mientras
// dura, el juego no avanza la luz ni acepta jugadas.
//
//   Out   el nivel se aplasta. Al terminar: el siguiente se pide (Card), o -
//         si era el ultimo - se vuelve al menu, a los creditos.
//   Card  el punto, la linea y el nombre. El nivel ya esta cargado, en cero.
//   In    el nivel crece. Al terminar, None: su luz empieza a salir.
enum class PassageStage { None, Out, Card, In };

struct Passage {
    PassageStage stage = PassageStage::None;
    float t = 0.0f;          // segundos en esta etapa (lo lleva LevelTransition)
    int next = 0;            // el nivel que viene
    bool toCredits = false;  // era el ultimo: Out termina en el menu
    bool Active() const { return stage != PassageStage::None; }
};

struct GameFlow {
    FlowMode mode = FlowMode::Game;
    bool hasMenu = false;            // una escena con menu lo pone al empezar

    // --- Del menu al juego ---
    int pendingLevel = -1;           // el director lo carga y lo vuelve a -1
    // --- Del juego al menu ---
    bool returnRequested = false;    // el menu lo atiende y lo vuelve a false
    MenuScreen returnScreen = MenuScreen::Main;
    bool finishedAll = false;        // se volvio porque se resolvio el ultimo

    // --- Lo que el director publica ---
    int levelCount = 0;
    int currentLevel = 0;
    std::vector<std::string> levelNames;

    // --- La camara del juego: donde empieza la orbita (OrbitCamera lo publica
    // cada frame). Un menu que termina su animacion AQUI le entrega la camara
    // sin un salto. ---
    float homePos[3] = {0, 0, -8};
    float homeRot[3] = {30, 0, 0};
    bool homeKnown = false;
    bool resetOrbit = false;         // OrbitCamera vuelve a su yaw/pitch/distancia de inicio
    // Cuanto se aleja la camara para el nivel cargado (1 = un tablero de
    // 3x3x3). Lo pone el director al cargar; OrbitCamera multiplica su
    // distancia por esto, asi el zoom de la persona se conserva entre niveles.
    float cameraScale = 1.0f;
    // El zoom de los botones + y - del HUD en un telefono (no hay rueda ni
    // teclas), en las mismas "muescas" que la rueda: positivo aleja. El HUD
    // suma, OrbitCamera lo consume y lo vuelve a cero.
    float touchZoom = 0.0f;

    // --- El paso entre niveles ---
    bool hasPassage = false;         // hay un LevelTransition en la escena
    Passage passage;

    // --- Progreso y ajustes (se guardan) ---
    unsigned solvedMask = 0;
    GameSettings settings;

    bool InGame() const { return mode == FlowMode::Game; }
    bool Solved(int i) const { return i >= 0 && i < 32 && (solvedMask >> i) & 1u; }
    int SolvedCount() const {
        int n = 0;
        for (int i = 0; i < levelCount; ++i) n += Solved(i) ? 1 : 0;
        return n;
    }
    // Un nivel se puede jugar si es el primero o si el anterior esta resuelto.
    // Rejugar uno resuelto siempre se puede.
    bool Unlocked(int i) const { return i == 0 || Solved(i) || Solved(i - 1); }
    // "Continuar" va al primer nivel sin resolver (o al ultimo si estan todos).
    int ContinueLevel() const {
        for (int i = 0; i < levelCount; ++i)
            if (!Solved(i)) return i;
        return levelCount > 0 ? levelCount - 1 : 0;
    }
    bool HasProgress() const { return solvedMask != 0; }

    float LightSpeed() const { return kLightSpeeds[settings.lightSpeed]; }
    float Sensitivity() const { return kSensitivities[settings.sensitivity]; }

    // El menu decide; el director carga en su proximo frame. Con transicion,
    // el nivel nace en cero y se ve el punto y su nombre antes de crecer.
    void StartLevel(int i) {
        pendingLevel = i;
        mode = FlowMode::Game;
        resetOrbit = true;
        if (hasPassage) passage = Passage{PassageStage::Card, 0.0f, i, false};
    }

    // El director, al terminar el festejo de un nivel resuelto.
    void BeginPassage(int nextLevel, bool last) { passage = Passage{PassageStage::Out, 0.0f, nextLevel, last}; }
    void ReturnToMenu(MenuScreen screen, bool finished = false) {
        if (!hasMenu) return;
        mode = FlowMode::Menu;
        passage = Passage{};
        returnRequested = true;
        returnScreen = screen;
        finishedAll = finished;
    }
    void EnterMenu() {
        hasMenu = true;
        mode = FlowMode::Menu;
    }
};

inline GameFlow& Flow() {
    static GameFlow flow;
    return flow;
}

} // namespace gravitational
