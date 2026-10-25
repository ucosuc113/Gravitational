// LAS PRUEBAS DE SurfaceGrid: corren una vez al darle a Play y escriben una
// linea por comprobacion en el log, con OK o FALLA y lo medido al lado de lo
// esperado (la Consola las junta con el filtro "Solo pruebas").
//
// Va en un vacio "Pruebas" de la escena. No necesita frame ni GPU: es un
// script solo porque el juego todavia no tiene un binario de pruebas propio, y
// el dia que lo tenga esto se muda tal cual (SurfaceGrid no conoce el motor).

#include "Puzzle/SurfaceGrid.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptRegistry.h"

#include <string>

using namespace strace;
using namespace gravitational;

namespace {

const wchar_t* Name(Dir d) {
    switch (d) {
        case Dir::North: return L"Norte";
        case Dir::East: return L"Este";
        case Dir::South: return L"Sur";
        case Dir::West: return L"Oeste";
    }
    return L"?";
}

std::wstring Text(Cell c) { return L"(" + std::to_wstring(c.x) + L"," + std::to_wstring(c.y) + L")"; }

std::wstring Text(const Step& s) {
    return s.valid ? Text(s.cell) + L" " + Name(s.heading) : std::wstring(L"fuera");
}

} // namespace

class GridCheck : public IScript {
public:
    void OnStart() override {
        // --- Direcciones ---
        const Dir all[] = {Dir::North, Dir::East, Dir::South, Dir::West};
        for (Dir d : all) {
            Expect(Opposite(Opposite(d)) == d, std::wstring(L"Opposite dos veces vuelve a ") + Name(d), Name(Opposite(Opposite(d))));
            Expect(Opposite(d) != d, std::wstring(L"Opposite(") + Name(d) + L") no es el mismo", Name(Opposite(d)));
            Expect(TurnRight(TurnRight(TurnRight(TurnRight(d)))) == d,
                   std::wstring(L"cuatro TurnRight vuelven a ") + Name(d), Name(TurnRight(TurnRight(TurnRight(TurnRight(d))))));
            Expect(TurnLeft(TurnRight(d)) == d, std::wstring(L"TurnLeft deshace TurnRight desde ") + Name(d),
                   Name(TurnLeft(TurnRight(d))));
            Expect(TurnRight(TurnRight(d)) == Opposite(d), std::wstring(L"dos TurnRight = Opposite desde ") + Name(d),
                   Name(TurnRight(TurnRight(d))));
        }
        Expect(TurnRight(Dir::North) == Dir::East, L"TurnRight(Norte) = Este (agujas del reloj)", Name(TurnRight(Dir::North)));

        // --- Un paso desde el centro ---
        const SurfaceGrid grid(5);
        const Cell center{2, 2};
        struct Case { Dir d; Cell want; };
        const Case cases[] = {{Dir::North, {2, 3}}, {Dir::East, {3, 2}}, {Dir::South, {2, 1}}, {Dir::West, {1, 2}}};
        for (const Case& c : cases) {
            const Step s = grid.Neighbor(center, c.d);
            Expect(s.valid && s.cell == c.want && s.heading == c.d,
                   std::wstring(L"desde (2,2) hacia ") + Name(c.d) + L" llega a " + Text(c.want), Text(s));
        }

        // --- Bordes ---
        Expect(!grid.Neighbor({0, 0}, Dir::South).valid, L"esquina (0,0) hacia Sur se sale", Text(grid.Neighbor({0, 0}, Dir::South)));
        Expect(!grid.Neighbor({0, 0}, Dir::West).valid, L"esquina (0,0) hacia Oeste se sale", Text(grid.Neighbor({0, 0}, Dir::West)));
        Expect(grid.Neighbor({0, 0}, Dir::East).valid, L"esquina (0,0) hacia Este sigue dentro", Text(grid.Neighbor({0, 0}, Dir::East)));
        Expect(grid.Neighbor({0, 0}, Dir::North).valid, L"esquina (0,0) hacia Norte sigue dentro", Text(grid.Neighbor({0, 0}, Dir::North)));
        Expect(!grid.Neighbor({4, 4}, Dir::East).valid, L"esquina (4,4) hacia Este se sale", Text(grid.Neighbor({4, 4}, Dir::East)));
        Expect(!grid.Neighbor({4, 4}, Dir::North).valid, L"esquina (4,4) hacia Norte se sale", Text(grid.Neighbor({4, 4}, Dir::North)));

        // --- Una fila entera: Size()-1 pasos validos y el siguiente se sale ---
        Cell at{0, 2};
        int steps = 0;
        while (true) {
            const Step s = grid.Neighbor(at, Dir::East);
            if (!s.valid) break;
            at = s.cell;
            if (++steps > 100) break; // tope: un Neighbor roto no puede colgar el Play
        }
        Expect(steps == grid.Size() - 1, L"recorrer la fila hacia el Este da Size()-1 = 4 pasos",
               std::to_wstring(steps) + L" pasos");

        // --- Tamano invalido ---
        Expect(SurfaceGrid(0).Size() == 1, L"un tamano 0 se corrige a 1", std::to_wstring(SurfaceGrid(0).Size()));

        Log::Info(L"[grid] " + std::to_wstring(m_ok) + L" OK, " + std::to_wstring(m_fail) + L" FALLA");
    }

private:
    void Expect(bool ok, const std::wstring& what, const std::wstring& got) {
        if (ok) {
            ++m_ok;
            Log::Info(L"[grid] OK     " + what);
        } else {
            ++m_fail;
            Log::Error(L"[grid] FALLA  " + what + L"  -  dio " + got);
        }
    }

    int m_ok = 0;
    int m_fail = 0;
};

STRACE_REGISTER_SCRIPT(GridCheck)
