// LAS PRUEBAS DE LAS REGLAS 3D (Board + LightTrace + BeamPath, ver
// docs/reglas.md): la luz por los planos y las figuras, el cubo y su eje, el
// inventario, las caras de la caja, los largos y el formato de nivel. Sin frame
// ni GPU; una linea por comprobacion en el log.
//aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
// Los tres niveles se leen de sus ARCHIVOS y se prueban con la solucion de su
// comentario: si alguien cambia uno y deja de tener solucion, esto lo dice.

#include "Puzzle/RuleChecks.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/IO/FileSystem.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Text.h"

#include <cmath>
#include <cwchar>
#include <sstream>
#include <string>

using namespace strace;
using namespace gravitational;

namespace {

const wchar_t* EndName(TraceEnd e) {
    switch (e) {
        case TraceEnd::Reached: return L"llego";
        case TraceEnd::Lost: return L"se perdio";
        case TraceEnd::Blocked: return L"se freno";
        case TraceEnd::Orbit: return L"orbita";
        case TraceEnd::Loop: return L"ciclo";
        case TraceEnd::Absorbed: return L"cayo en un agujero negro";
    }
    return L"?";
}

std::wstring Describe(const Trace& t) { return std::wstring(EndName(t.end)) + L", " + std::to_wstring(t.hops.size()) + L" tramos"; }

} // namespace

class TraceCheck : public IScript {
public:
    void OnStart() override {
        Board base;
        if (!Read(L"tutorial-1.txt", base)) return;
        Expect(base.W == 3 && base.inventory[0] == 3, L"el tutorial es de 3x3x3 con 3 esferas", std::to_wstring(base.W));

        // Sin figuras: la luz sube por la pared de salida y se pierde por el borde.
        {
            const Trace t = TraceLight(base);
            Expect(t.end == TraceEnd::Lost && t.hops.size() == 1 && t.hops[0].toEdge, L"sin figuras se pierde por el borde", Describe(t));
        }

        // La solucion: tres esferas en fila a la altura y = 2.
        {
            Board b = base;
            const bool placed = b.Place(Shape::Sphere, {0, 2, 1}) && b.Place(Shape::Sphere, {1, 2, 1}) && b.Place(Shape::Sphere, {2, 2, 1});
            const Trace t = TraceLight(b);
            Expect(placed && t.end == TraceEnd::Reached && t.hops.size() == 5, L"tres esferas en fila llegan (5 tramos)", Describe(t));
            if (t.hops.size() == 5) {
                const Hop& s = t.hops[2];
                Expect(s.kind == Hop::Kind::Figure && s.quarters == 2 && s.nIn == kNegX && s.t == kNegY,
                       L"la esfera del medio: entra por -X bajando y da media vuelta", L"otro tramo");
                Expect(t.hops[4].kind == Hop::Kind::Plane && t.hops[4].plane == 1, L"el ultimo tramo es el plano de llegada", L"otro");
            }
            const BeamPath path = BuildBeamPath(b, t);
            Expect(std::fabs(path.length - (1.0f + 3.0f * kPi * 0.5f)) < 1e-3f, L"el largo de la solucion es 1 + 3*pi/2",
                   std::to_wstring(path.length));
            Expect(!b.Place(Shape::Sphere, {1, 0, 0}), L"sin inventario no se coloca", L"se coloco");
            Expect(b.Remove({1, 2, 1}) && b.inventory[0] == 1, L"quitar devuelve al inventario", std::to_wstring(b.inventory[0]));
            // D3 (24/10/2026): la primera esta apoyada en la pared, asi que la
            // vuelta entera la devuelve a la pared y la luz sigue subiendo.
            // Hasta ese dia orbitaba.
            const Trace u = TraceLight(b);
            Expect(u.end != TraceEnd::Orbit && u.hops.size() == 3 && u.hops[2].kind == Hop::Kind::Plane && u.hops[2].plane == 0,
                   L"sin la del medio, la primera da la vuelta y devuelve la luz a la pared", Describe(u));
        }

        // La luz que vuelve a la fuente se frena ahi (un agujero blanco la
        // devuelve). Y sobre la fuente no se puede poner nada: antes se podia,
        // y una pieza ahi tapaba la marca y servia de espejo.
        {
            Board b = base;
            Figure white;
            white.shape = Shape::WhiteHole;
            white.cell = {0, 2, 1};
            white.fixed = true;
            b.figures.push_back(white);
            const Trace t = TraceLight(b);
            Expect(t.end == TraceEnd::Blocked && t.hops.back().kind == Hop::Kind::Plane && t.hops.back().plane == 0,
                   L"la luz que vuelve a la fuente se frena", Describe(t));
            Board c = base;
            Expect(!c.Place(Shape::Sphere, {0, 1, 1}), L"sobre la fuente no se pone nada", L"se puso");
        }

        // El cubo y su eje, apoyado en la pared por la que sube la luz. Con eje
        // Y (a lo largo de la subida) la cara de enfrente es una tapa y la luz
        // se FRENA en su borde (D2; hasta el 24/10/2026 entraba por debajo y la
        // franja la doblaba); con eje X tambien; con eje Z la luz SUBE por el
        // (ClimbsOnto en LightTrace.h): cara de abajo, de afuera y de arriba, y
        // en la arista de la pared, que no sigue mas arriba, se pierde.
        {
            Board b = base;
            b.inventory[1] = 1;
            b.Place(Shape::Cube, {0, 2, 1}); // nace con eje +Y
            const Trace t = TraceLight(b);
            Expect(t.end == TraceEnd::Blocked && t.hops.size() == 1 && t.hops[0].toEdge, L"un cubo de eje +Y frena en su borde la luz que sube (D2)",
                   Describe(t));
            Expect(b.Rotate({0, 2, 1}) && b.FigureAt({0, 2, 1})->axis == 2, L"rotar el cubo pasa su eje de Y a Z", L"otro eje");
            Expect(TraceLight(b).end == TraceEnd::Lost && TraceLight(b).hops.size() == 2 && TraceLight(b).hops[1].fromSeam,
                   L"con eje Z la luz sube por el (y solo, se pierde arriba)", Describe(TraceLight(b)));
            Expect(b.Rotate({0, 2, 1}) && TraceLight(b).end == TraceEnd::Blocked, L"con eje -X entra por la tapa y frena", Describe(TraceLight(b)));
            Expect(!b.Rotate({1, 0, 0}), L"rotar una celda vacia no es una jugada", L"roto");
        }

        // Llega a la pared de llegada pero el receptor esta en otra parte: se pierde.
        {
            Board b = base;
            b.receiverCell = {2, 2, 2};
            b.Place(Shape::Sphere, {0, 2, 1});
            b.Place(Shape::Sphere, {1, 2, 1});
            b.Place(Shape::Sphere, {2, 2, 1});
            Expect(TraceLight(b).end == TraceEnd::Lost, L"en el plano de llegada sin receptor en el camino, se pierde", Describe(TraceLight(b)));
        }

        // --- Nivel 2: del piso a la pared derecha ---
        {
            Board b;
            if (Read(L"nivel-2.txt", b)) {
                Expect(b.planes[0].normal == kNegY && b.planes[1].normal == kPosX, L"nivel 2: salida en el piso, llegada a la derecha", L"otras caras");
                Expect(!b.PlanesTouch(), L"nivel 2: los planos estan separados por aire", L"se tocan");
                Expect(TraceLight(b).end == TraceEnd::Lost, L"nivel 2 vacio: se pierde por el piso", Describe(TraceLight(b)));
                for (Int3 c : {Int3{2, 0, 1}, Int3{2, 1, 1}, Int3{2, 2, 1}, Int3{3, 3, 1}, Int3{4, 3, 1}}) b.Place(Shape::Sphere, c);
                b.Place(Shape::Cube, {3, 2, 1});
                // Con el cubo nuevo (que dobla la luz que va por su eje) ya no frena: la
                // manda a otra parte. Lo que importa es que sin rotarlo no llega.
                Expect(TraceLight(b).end != TraceEnd::Reached, L"nivel 2 con el cubo sin rotar: no llega", Describe(TraceLight(b)));
                b.Rotate({3, 2, 1});
                Expect(TraceLight(b).end == TraceEnd::Reached, L"nivel 2 con el cubo rotado: llega", Describe(TraceLight(b)));
            }
        }

        // --- Plano a plano por la arista (docs/reglas.md) ---
        // Un piso y una pared derecha enteros SE TOCAN: la luz que corre por el
        // piso hacia la derecha llega a la arista y sube por la pared, sin
        // ninguna figura. Por eso un nivel asi se rechaza (abajo); aqui se
        // arma a mano para probar la regla misma.
        {
            Board e;
            e.W = 5;
            e.H = 4;
            e.D = 3;
            e.planes[0].normal = kNegY;
            e.planes[1].normal = kPosX;
            e.FillWholeFaces();
            e.sourceCell = {0, 0, 1};
            e.sourceDir = kPosX;
            e.receiverCell = {4, 3, 1};
            Expect(e.PlanesTouch(), L"piso y pared enteros se tocan", L"no se tocan");
            const Trace t = TraceLight(e);
            Expect(t.end == TraceEnd::Reached && t.hops.size() == 2 && t.hops[0].toEdge && t.hops[1].fromEdge && t.hops[1].plane == 1,
                   L"la luz pasa del piso a la pared por la arista y llega sola", Describe(t));
            const BeamPath path = BuildBeamPath(e, t);
            Expect(std::fabs(path.length - 8.0f) < 1e-3f, L"ese recorrido mide 4,5 por el piso + 3,5 por la pared",
                   std::to_wstring(path.length));
        }

        // --- Nivel 3: del piso al techo, con un cubo ---
        {
            Board b;
            if (Read(L"tutorial-2.txt", b)) {
                b.Place(Shape::Sphere, {1, 0, 1});
                b.Place(Shape::Cube, {1, 1, 1});
                b.Place(Shape::Sphere, {1, 2, 1});
                Expect(TraceLight(b).end == TraceEnd::Blocked, L"nivel 3 con el cubo sin rotar: se frena", Describe(TraceLight(b)));
                b.Rotate({1, 1, 1});
                Expect(TraceLight(b).end == TraceEnd::Reached, L"nivel 3 con el cubo rotado: llega", Describe(TraceLight(b)));
                Expect(PlanePoint(b, 0, b.sourceCell).y < 0.0f && PlanePoint(b, 1, b.receiverCell).y > 0.0f,
                       L"piso y techo: la salida abajo y la llegada arriba", L"al reves");
            }
        }

        // --- La caja en el mundo y los largos ---
        {
            Board b;
            b.W = 5;
            b.H = 4;
            b.D = 3;
            bool roundTrip = true;
            for (int x = 0; x < b.W; ++x)
                for (int y = 0; y < b.H; ++y)
                    for (int z = 0; z < b.D; ++z) roundTrip = roundTrip && CellAt(b, CellCenter(b, {x, y, z})) == Int3{x, y, z};
            Expect(roundTrip, L"CellAt deshace CellCenter en toda la caja (5x4x3)", L"alguna celda no vuelve");
        }
        Expect(std::fabs(CubeArc(kPi * 0.5f) - 1.0f) < 1e-4f && std::fabs(CubeArc(kPi) - 2.0f) < 1e-4f,
               L"un cuarto de vuelta de cubo mide 1, media mide 2", std::to_wstring(CubeArc(kPi * 0.5f)));
        Expect(std::fabs(CubeArc(kPi * 0.25f) - 0.5f) < 1e-4f, L"hasta la arista (45 grados) son 0,5", std::to_wstring(CubeArc(kPi * 0.25f)));

        // --- Figuras fijas ---
        {
            Board b;
            std::string error;
            Expect(Board::Parse("size 2 2 2\nsource 0 0 0 up\nreceiver 1 0 0\nfixed cube 1 1 0 z\n", b, error), L"un nivel con un cubo fijo se lee",
                   Utf8ToWide(error));
            Expect(!b.Remove({1, 1, 0}) && !b.Rotate({1, 1, 0}), L"una figura fija no se quita ni se rota", L"se toco");
        }

        // --- Formato ---
        Board bad;
        std::string error;
        Expect(!Board::Parse("source 0 1 1 up\nreceiver 2 1 1\n", bad, error), L"sin size se rechaza", L"se acepto");
        Expect(!Board::Parse("size 3 3 3\nsource 0 1 1 sideways\nreceiver 2 1 1\n", bad, error), L"una direccion rara se rechaza", L"se acepto");
        Expect(!Board::Parse("size 3 3 3\nsource 1 1 1 up\nreceiver 2 1 1\n", bad, error), L"una fuente fuera de la capa de su plano se rechaza",
               L"se acepto");
        Expect(!Board::Parse("size 3 3 3\nsource 0 1 1 right\nreceiver 2 1 1\n", bad, error), L"una fuente que apunta fuera de su plano se rechaza",
               L"se acepto");
        Expect(!Board::Parse("size 3 3 3\nsource 0 1 1 up\nreceiver 2 1 1\nfixed sphere 9 0 0\n", bad, error),
               L"una figura fija fuera de la caja se rechaza", L"se acepto");
        Expect(!Board::Parse("start nowhere\nsize 3 3 3\nsource 0 1 1 up\nreceiver 2 1 1\n", bad, error), L"una cara desconocida se rechaza",
               L"se acepto");
        Expect(!Board::Parse("start left\nend left\nsize 3 3 3\nsource 0 1 1 up\nreceiver 0 1 1\n", bad, error),
               L"salida y llegada en la misma cara se rechaza", L"se acepto");
        Expect(!Board::Parse("size 3 3 3\nsource 0 1 1 up\nreceiver 2 1 1\nmagia 1\n", bad, error), L"una palabra desconocida se rechaza", L"se acepto");
        Expect(!Board::Parse("start floor\nend right\nsize 5 4 3\nsource 0 0 1 right\nreceiver 4 3 1\n", bad, error),
               L"piso y pared enteros (se tocan) se rechazan", L"se acepto");
        Expect(!Board::Parse("start floor 0 1 0 2 1 2\nend right\nsize 5 4 3\nsource 0 0 1 right\nreceiver 4 3 1\n", bad, error),
               L"un rectangulo fuera de su cara se rechaza", L"se acepto");
        Expect(!Board::Parse("start floor 0 0 0 2 0 2\nend right 4 2 0 4 3 2\nsize 5 4 3\nsource 3 0 1 right\nreceiver 4 3 1\n", bad, error),
               L"una fuente fuera del rectangulo de su plano se rechaza", L"se acepto");

        // Las piezas nuevas (Puzzle/RuleChecks.h): las mismas que corre
        // Tools/LevelLab sin abrir el Editor.
        RunRuleChecks([&](bool ok, const std::string& what, const std::string& got) { Expect(ok, Utf8ToWide(what), Utf8ToWide(got)); });

        // Cada nivel de la lista, con la solucion que trae anotada (";! place").
        // Si alguien cambia uno y deja de tener solucion, esto lo dice.
        for (int n = 4; n <= 23; ++n) {
            wchar_t name[16];
            std::swprintf(name, 16, L"n%02d.txt", n);
            std::string text, error;
            Board b;
            if (!FileSystem::ReadAll(SceneService::ProjectRoot() + L"/Assets/Levels/" + name, text) || !Board::Parse(text, b, error)) {
                Expect(false, std::wstring(name) + L" se lee", Utf8ToWide(error));
                continue;
            }
            bool placed = true;
            std::istringstream lines(text);
            std::string raw;
            while (std::getline(lines, raw)) {
                const size_t at = raw.find(";! place");
                if (at == std::string::npos) continue;
                std::istringstream in(raw.substr(at + 8));
                std::vector<Figure> got;
                placed = placed && Board::ParseFigure(in, got, error);
                for (const Figure& f : got) placed = placed && b.Place(f);
            }
            const Trace t = TraceLight(b);
            Expect(placed && t.end == TraceEnd::Reached, std::wstring(name) + L": su solucion llega", Describe(t));
        }

        Log::Info(L"[luz3d] " + std::to_wstring(m_ok) + L" OK, " + std::to_wstring(m_fail) + L" FALLA");
    }

private:
    bool Read(const std::wstring& file, Board& out) {
        std::string text, error;
        const bool ok = FileSystem::ReadAll(SceneService::ProjectRoot() + L"/Assets/Levels/" + file, text) && Board::Parse(text, out, error);
        Expect(ok, file + L" se lee", Utf8ToWide(error));
        return ok;
    }

    void Expect(bool ok, const std::wstring& what, const std::wstring& got) {
        if (ok) {
            ++m_ok;
            Log::Info(L"[luz3d] OK     " + what);
        } else {
            ++m_fail;
            Log::Error(L"[luz3d] FALLA  " + what + L"  -  dio " + got);
        }
    }

    int m_ok = 0;
    int m_fail = 0;
};

STRACE_REGISTER_SCRIPT(TraceCheck)
