#include "GameHost.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Rendering/RenderSettings.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// Scripts.dll se enlaza solo contra EngineCore.lib y lo basico del CRT;
// PostQuitMessage vive en user32 (ver Services/Host.cpp de The Freak Place).
#if defined(_MSC_VER)
#pragma comment(lib, "user32.lib")
#endif
#endif

namespace fs = std::filesystem;
using namespace strace;

namespace gravitational::host {

bool IsEditor() {
#if defined(_WIN32)
    static int cached = -1;
    if (cached < 0) {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring name = fs::path(path).filename().wstring();
        for (wchar_t& c : name) c = static_cast<wchar_t>(towlower(c));
        cached = name == L"editor.exe" ? 1 : 0;
    }
    return cached == 1;
#else
    return false;
#endif
}

bool RequestQuit() {
    if (IsEditor()) {
        Log::Info(L"Gravitational: SALIR en el Editor no cierra nada (deten el Play). En un Build cierra el juego.");
        return false;
    }
#if defined(_WIN32)
    Log::Info(L"Gravitational: salir del juego.");
    PostQuitMessage(0);
    return true;
#else
    Log::Warn(L"Gravitational: esta plataforma todavia no deja que el juego se cierre solo.");
    return false;
#endif
}

static fs::path DataFile() {
    fs::path base;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    base = n > 0 && n < MAX_PATH ? fs::path(buf) : fs::temp_directory_path();
#else
    base = fs::temp_directory_path();
#endif
    base /= IsEditor() ? L"Gravitational-Editor" : L"Gravitational";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base / L"progreso.txt";
}

void LoadProgress(GameFlow& flow) {
    std::ifstream in(DataFile());
    if (!in) return;
    std::string key;
    GameSettings& s = flow.settings;
    while (in >> key) {
        int v = 0;
        if (!(in >> v)) break;
        if (key == "solved") flow.solvedMask = static_cast<unsigned>(v);
        else if (key == "lightSpeed" && v >= 0 && v < 6) s.lightSpeed = v;
        else if (key == "sensitivity" && v >= 0 && v < 6) s.sensitivity = v;
        else if (key == "invertOrbit") s.invertOrbit = v != 0;
        else if (key == "antialias" && v >= 0 && v < 3) s.antialias = v;
        else if (key == "vsync") s.vsync = v != 0;
    }
}

void SaveProgress(const GameFlow& flow) {
    // A un .tmp y despues se renombra: un corte a mitad deja el anterior.
    const fs::path final = DataFile();
    const fs::path tmp = final.wstring() + L".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            Log::Warn(L"Gravitational: no se pudo guardar el progreso en " + final.wstring());
            return;
        }
        const GameSettings& s = flow.settings;
        out << "solved " << flow.solvedMask << "\nlightSpeed " << s.lightSpeed << "\nsensitivity " << s.sensitivity
            << "\ninvertOrbit " << (s.invertOrbit ? 1 : 0) << "\nantialias " << s.antialias << "\nvsync " << (s.vsync ? 1 : 0)
            << "\n";
    }
    std::error_code ec;
    fs::remove(final, ec);
    fs::rename(tmp, final, ec);
}

void ApplySettings(const GameSettings& s) {
    const AntialiasMode aa[] = {AntialiasMode::Off, AntialiasMode::X2, AntialiasMode::X4};
    RenderSettings::SetAntialias(aa[s.antialias]);
    RenderSettings::SetVsync(s.vsync);
}

} // namespace gravitational::host
