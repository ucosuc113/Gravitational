// LA PRUEBA DEL CLIC: del pixel a la celda, con la camara y el raycast de
// verdad. Lo que el raton hace en FigureBuilder (Pieces/Picking.h), sin raton:
// proyecta el centro de una celda a la pantalla con la misma matriz con la que
// se dibuja, y pregunta que hay ahi. Despues pone una esfera por el mismo
// camino que un clic (PlacementsFor + Place + Commit), espera a que el
// director la cree, apunta a ELLA y la quita con DESHACER.
//
// Por que existe: la entrada programada (`strace play click`) no llega al
// raton de una corrida automatica, asi que colocar, el fantasma y la eleccion
// de pieza no se podian probar sin una persona delante. Solo corre con
// GRAV_PICKCHECK=1 en el entorno: toca el tablero de la partida.

#include "Puzzle/PuzzleSession.h"
#include "Flow/GameFlow.h"
#include "Flow/MenuKit.h"
#include "Pieces/Picking.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Scene/SceneQuery.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/UI/UiService.h"

#include <string>

using namespace strace;
using namespace gravitational;

class PickCheck : public IScript {
public:
    void OnStart() override { m_on = kit::AutoPick("GRAV_PICKCHECK") == 1; }

    void OnUpdate(float /*dt*/) override {
        if (!m_on || m_stage >= 3) return;
        PuzzleSession& s = Session();
        if (!s.loaded || !Flow().InGame() || Flow().passage.Active()) {
            m_wait = 0;
            return;
        }
        if (++m_wait < 20) return; // que el nivel este quieto y dibujado
        m_wait = 0;
        const auto& roots = SceneService::Roots();
        const GameObject* cam = FindActiveCamera(roots);
        const GameObject* container = FindByPath(roots, "Nivel/Figuras");
        const GameObject* p0 = FindByPath(roots, "Nivel/PlanoSalida");
        const GameObject* p1 = FindByPath(roots, "Nivel/PlanoLlegada");
        if (!cam || !container) {
            Expect(false, L"la escena tiene camara y Nivel/Figuras", L"falta");
            m_stage = 3;
            return;
        }
        const float w = UiService::Width(), h = UiService::Height();
        const DirectX::XMMATRIX vp = kit::ViewProj(*cam);

        if (m_stage == 0) {
            // 1. Un plano: la celda siguiente a la fuente, donde iria la primera pieza.
            m_target = s.board.sourceCell + s.board.sourceDir;
            const Vec3f world = PlanePoint(s.board, 0, m_target);
            float sx = 0, sy = 0;
            Pick pick;
            const bool seen = kit::WorldToScreen(vp, world.x, world.y, world.z, sx, sy) && PickAt(s.board, *cam, container, {p0, p1}, sx, sy, w, h, pick);
            Expect(seen && pick.figure < 0 && pick.cell == m_target && pick.normal == -s.board.planes[0].normal,
                   L"clic sobre el plano de salida: cae en la celda de al lado de la fuente", seen ? Cell(pick.cell) : L"nada bajo el pixel");
            // 1b. El BORDE: cerca de la esquina de afuera de las celdas extremas
            // del plano. El raycast del motor descartaba la mitad de afuera de
            // todo Plane (su caja era de 1x1 y el Plane es de 2x2), y el
            // fantasma desaparecia al pasar el centro de esas celdas. La prueba
            // de arriba apunta al CENTRO de una celda, asi que no lo veia.
            const Face& f0 = s.board.planes[0];
            for (int corner = 0; corner < 2; ++corner) {
                const Int3 edge = corner == 0 ? f0.lo : f0.hi;
                const float out = corner == 0 ? -0.4f : 0.4f;
                Vec3f q = PlanePoint(s.board, 0, edge);
                if (f0.normal.x == 0) q.x += out;
                if (f0.normal.y == 0) q.y += out;
                if (f0.normal.z == 0) q.z += out;
                float ex = 0, ey = 0;
                Pick edgePick;
                const bool edgeSeen = kit::WorldToScreen(vp, q.x, q.y, q.z, ex, ey) && PickAt(s.board, *cam, container, {p0, p1}, ex, ey, w, h, edgePick);
                // Una pieza ya puesta en esa celda se lleva el clic, y esta bien.
                Expect(edgeSeen && (edgePick.figure >= 0 || edgePick.cell == edge),
                       corner == 0 ? L"clic en el borde de afuera de la primera celda del plano" : L"clic en el borde de afuera de la ultima celda del plano",
                       edgeSeen ? Cell(edgePick.cell) : L"nada bajo el pixel");
            }
            // 2. Poner una esfera ahi, como el clic.
            const std::vector<Figure> stances = s.board.PlacementsFor(Shape::Sphere, m_target, -s.board.planes[0].normal);
            ++s.board.inventory[static_cast<int>(Shape::Sphere)];
            const int before = static_cast<int>(s.board.figures.size());
            s.Remember();
            const bool placed = !stances.empty() && s.board.Place(stances.front());
            s.Commit();
            Expect(placed && static_cast<int>(s.board.figures.size()) == before + 1, L"la esfera se pone donde apunto el clic", L"no se puso");
            m_stage = 1;
            return;
        }
        if (m_stage == 1) {
            // 3. Apuntar a la esfera nueva (ya la creo el director).
            const Vec3f c = CellCenter(s.board, m_target);
            float sx = 0, sy = 0;
            Pick pick;
            const bool seen = kit::WorldToScreen(vp, c.x, c.y, c.z, sx, sy) && PickAt(s.board, *cam, container, {p0, p1}, sx, sy, w, h, pick);
            const int expected = static_cast<int>(s.board.figures.size()) - 1;
            Expect(seen && pick.figure == expected, L"clic sobre la esfera: la elige a ella", seen ? std::to_wstring(pick.figure) : L"nada");
            Expect(TraceLight(s.board).hops.size() > 1, L"la luz entra a la esfera puesta", L"no la toca");
            // 4. Deshacer la deja como estaba.
            const bool undone = s.Undo();
            --s.board.inventory[static_cast<int>(Shape::Sphere)];
            Expect(undone && static_cast<int>(s.board.figures.size()) == expected, L"DESHACER la saca", L"sigue ahi");
            m_stage = 2;
            return;
        }
        Log::Info(L"[pick] " + std::to_wstring(m_ok) + L" OK, " + std::to_wstring(m_fail) + L" FALLA");
        m_stage = 3;
    }

private:
    static std::wstring Cell(const Int3& c) { return std::to_wstring(c.x) + L" " + std::to_wstring(c.y) + L" " + std::to_wstring(c.z); }

    void Expect(bool ok, const std::wstring& what, const std::wstring& got) {
        if (ok) {
            ++m_ok;
            Log::Info(L"[pick] OK     " + what);
        } else {
            ++m_fail;
            Log::Error(L"[pick] FALLA  " + what + L"  -  dio " + got);
        }
    }

    bool m_on = false;
    int m_stage = 0, m_wait = 0, m_ok = 0, m_fail = 0;
    Int3 m_target;
};

STRACE_REGISTER_SCRIPT(PickCheck)
