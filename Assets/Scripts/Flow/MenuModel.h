#pragma once

// QUE TIENE EL MENU, sin decir COMO se ve: las entradas del menu principal,
// las opciones con sus valores y los creditos. El menu presenta ESTO: un menu
// nuevo (ya se probaron tres) no tiene que reescribir que hace "VSYNC".

#include "GameFlow.h"
#include "GameHost.h"

#include <algorithm>
#include <cstdio>

#include <string>
#include <vector>

namespace gravitational::menu {

enum class MainItem { Play, Levels, Options, Credits, Quit };
inline constexpr int kMainCount = 5;

inline std::string MainLabel(MainItem item, const GameFlow& f) {
    switch (item) {
        case MainItem::Play: return f.HasProgress() ? "CONTINUE" : "PLAY";
        case MainItem::Levels: return "LEVELS";
        case MainItem::Options: return "OPTIONS";
        case MainItem::Credits: return "CREDITS";
        case MainItem::Quit: return "QUIT";
    }
    return "";
}

// --- Opciones ---

inline constexpr int kOptionCount = 6;
inline const char* OptionLabel(int i) {
    static const char* labels[kOptionCount] = {"LIGHT SPEED", "CAMERA SENSITIVITY", "INVERT ORBIT",
                                               "ANTIALIASING", "VSYNC", "ERASE PROGRESS"};
    return i >= 0 && i < kOptionCount ? labels[i] : "";
}

// El estado de "ERASE PROGRESS": la primera vez pide confirmacion. Se
// desarma solo al moverse a otra fila (lo hace quien presenta, con Disarm).
struct OptionsState {
    bool armedErase = false;
    void Disarm() { armedErase = false; }
};

inline std::string OptionValue(int i, const GameFlow& f, const OptionsState& st) {
    const GameSettings& s = f.settings;
    switch (i) {
        case 0: return kLightSpeedNames[s.lightSpeed];
        case 1: return kSensitivityNames[s.sensitivity];
        case 2: return s.invertOrbit ? "SI" : "NO";
        case 3: return kAntialiasNames[s.antialias];
        case 4: return s.vsync ? "SI" : "NO";
        case 5: return st.armedErase ? "CONFIRM?" : (f.HasProgress() ? std::to_string(f.SolvedCount()) + " SOLVED" : "EMPTY");
    }
    return "";
}

// Las de valor se mueven con izquierda/derecha (dir = -1/+1) Y con aceptar
// (dir = 0: avanza y da la vuelta), para que el raton solo necesite un clic.
inline bool OptionIsAction(int i) { return i == 5; }

inline void ChangeOption(int i, int dir, GameFlow& f, OptionsState& st) {
    GameSettings& s = f.settings;
    auto step = [&](int& v, int count) {
        if (dir == 0) v = (v + 1) % count;
        else v = std::max(0, std::min(count - 1, v + dir));
    };
    switch (i) {
        case 0: step(s.lightSpeed, 6); break;
        case 1: step(s.sensitivity, 6); break;
        case 2: s.invertOrbit = !s.invertOrbit; break;
        case 3: step(s.antialias, 3); break;
        case 4: s.vsync = !s.vsync; break;
        case 5:
            if (dir != 0) return;
            if (!st.armedErase) {
                st.armedErase = true;
                return;
            }
            f.solvedMask = 0;
            st.armedErase = false;
            break;
    }
    host::ApplySettings(s);
    host::SaveProgress(f);
}

// --- Creditos ---

inline const std::vector<std::string>& CreditLines() {
    static const std::vector<std::string> lines = {
        "GRAVITATIONAL",
        "",
        "A PUZZLE ABOUT THE SPACE",
        "THAT LIGHT TRAVELS THROUGH",
        "",
        "DESIGN AND PROGRAMMING",
        "UCOSUC113",
        "",
        "ENGINE",
        "S.T.R.AC.E",
        "",
        "THANKS FOR PLAYING",
    };
    return lines;
}

inline std::string LevelName(const GameFlow& f, int i) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d", i + 1);
    return buf;
}

} // namespace gravitational::menu
