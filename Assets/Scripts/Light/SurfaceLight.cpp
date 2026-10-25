// LA LUZ SOBRE LAS SUPERFICIES: le reparte a cada superficie (los dos planos y
// cada figura) los tramos de luz que la cruzan, por su material, para que su
// shader los dibuje como parte de si misma. Va en el vacio "Puzzle", despues de
// PuzzleDirector (que es quien crea los objetos de las figuras).
//
// Por que asi y no un objeto de luz: en Gravitational una superficie 2D
// representa el espacio 3D, y lo que viaja por ese espacio viaja POR la
// superficie. Un tubo flotando sobre ella seria otra cosa: geometria en el
// espacio de afuera.
//
// Cada superficie recibe como mucho DOS tramos (8 float4 de material no dan
// para mas). Con las reglas de hoy la luz cruza una figura una vez casi
// siempre; si alguna vez pasa por tres, se avisa en el log en vez de dibujar
// mal en silencio.
//
// Parametros (ver Surface.hlsl, Figure.hlsl y Piece.hlsl):
//   plano   [2] = (progreso, segundos desde resuelto o -1, tramos, receptor alcanzado)
//           [3] = (punto de la marca xyz, 1 fuente / 2 receptor)
//           [4],[5] y [6],[7] = (A xyz, s inicio), (B xyz, s fin)
//   pieza   [0] = (progreso, segundos desde resuelto o -1, tramos, - del director)
//           [4],[5] y [6],[7] = (cara de entrada xyz, cuartos o largo), (direccion xyz, s inicio)
//           en un cubo, los cuartos son donde TERMINA el arco (x,5 si se va por
//           una costura) y +8 si empezo en una (ver Figure.hlsl)
//   el tramo que entrega la luz a un agujero negro lleva su s (fin en el
//   plano, inicio en la pieza) en NEGATIVO: ahi empieza a apagarse

#include "../Pieces/PlaneNodes.h"
#include "../Puzzle/PuzzleSession.h"
#include "../Flow/GameFlow.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"

#include <vector>

using namespace strace;
using namespace gravitational;

class SurfaceLight : public IScript {
public:
    ObjectField startPlane{this, "startPlane"};
    ObjectField endPlane{this, "endPlane"};
    ObjectField figures{this, "figures"};

    void OnUpdate(float /*dt*/) override {
        const PuzzleSession& s = Session();
        if (!s.loaded) return;
        // Durante el paso entre niveles el nivel se deforma (se aplasta o
        // crece) y la luz y las marcas, que se miden en el mundo, quedarian
        // corridas: se apagan, y la luz del nivel nuevo sale cuando ya crecio.
        const bool hidden = Flow().passage.Active();
        const float solved = s.solved ? s.solvedSeconds : -1.0f;
        // El tramo que entrega la luz a un agujero negro (el anteultimo, si
        // termino ahi): su superficie empieza a apagarla antes del contacto
        // (GravFall en los shaders), para que la caida no empiece con un corte
        // en el borde del pozo. Se marca con el s en NEGATIVO - no queda un
        // float4 libre, y un s es siempre >= 0 (y > 0 en todo tramo que
        // entrega, porque antes hubo al menos un tramo de plano).
        const DrawHop* feeder = nullptr;
        if (s.trace.end == TraceEnd::Absorbed && s.beam.hops.size() >= 2) feeder = &s.beam.hops[s.beam.hops.size() - 2];

        // Los planos: sus segmentos y su marca. Todos los del tablero, los de
        // portal incluidos (PlaneNodes, en el orden de board.planes); sin
        // director que la haya llenado, los dos de la escena.
        const int planeCount = static_cast<int>(s.board.planes.size());
        for (int which = 0; which < planeCount; ++which) {
            GameObject* plane = which < static_cast<int>(PlaneNodes().size()) ? PlaneNodes()[static_cast<size_t>(which)]
                                : which == 0                                  ? startPlane.Get()
                                : which == 1                                  ? endPlane.Get()
                                                                              : nullptr;
            if (!plane) continue;
            Material* m = SceneService::UniqueMaterial(*plane);
            if (!m) continue;
            int count = 0;
            for (const DrawHop& h : s.beam.hops) {
                if (!h.onPlane || h.plane != which) continue;
                if (count == kDrawSlots) {
                    WarnOnce(L"SurfaceLight: un plano tiene mas de dos tramos de luz; se dibujan los dos primeros");
                    break;
                }
                m->userParams[4 + count * 2] = {h.a.x, h.a.y, h.a.z, h.s0};
                m->userParams[5 + count * 2] = {h.b.x, h.b.y, h.b.z, &h == feeder ? -h.s1 : h.s1};
                ++count;
            }
            const bool reached = s.trace.end == TraceEnd::Reached && s.FullyRevealed();
            m->userParams[2] = {s.progress, solved, hidden ? 0.0f : static_cast<float>(count), reached ? 1.0f : 0.0f};
            // Los de portal no tienen marca (ni fuente ni receptor).
            const Vec3f mark = which == 0 ? PlanePoint(s.board, 0, s.board.sourceCell)
                               : which == 1 ? PlanePoint(s.board, 1, s.board.receiverCell)
                                            : Vec3f{0.0f, 0.0f, 0.0f};
            m->userParams[3] = {mark.x, mark.y, mark.z, hidden || which > 1 ? 0.0f : which == 0 ? 1.0f : 2.0f};
        }

        // Las piezas: el director las crea en el MISMO orden que
        // board.figures, asi que el hijo i es la pieza i. Antes se buscaban
        // por la celda del objeto, y eso dejo de alcanzar: una esfera de 3 o
        // una rampa reciben la luz por una celda que no es la de su centro.
        GameObject* container = figures.Get();
        if (!container || container->children.size() != s.board.figures.size()) return;
        for (size_t i = 0; i < container->children.size(); ++i) {
            Material* m = SceneService::UniqueMaterial(*container->children[i]);
            if (!m) continue;
            const Figure& fig = s.board.figures[i];
            // Figure.hlsl (esfera y cubo) lee cuartos de vuelta; Piece.hlsl,
            // el largo (negativo para la boca de salida y la vuelta al tubo).
            const bool round = IsSphere(fig.shape) || fig.shape == Shape::Cube;
            int count = 0;
            for (const DrawHop& h : s.beam.hops) {
                if (h.onPlane || s.board.IndexAt(h.cell) != static_cast<int>(i)) continue;
                if (count == kDrawSlots) {
                    WarnOnce(L"SurfaceLight: una pieza tiene mas de dos tramos de luz; se dibujan los dos primeros");
                    break;
                }
                // Los cuartos van en float: un cubo pegado a otro termina en la
                // costura (medio cuarto antes) y empieza en ella (+8, que ningun
                // arco alcanza: no queda otro hueco en los float4).
                const float quarters = static_cast<float>(h.quarters) - (h.toSeam ? 0.5f : 0.0f) + (h.fromSeam ? 8.0f : 0.0f);
                // El largo NEGATIVO marca lo especial de cada pieza: la boca de
                // salida de un gusano, la vuelta al tubo de un toroide, y la
                // cuna o la piramide a la que la luz subio desde el plano (ClimbsOnto).
                const bool special = h.outMouth || h.tube || (h.fromSeam && (fig.shape == Shape::Prism || fig.shape == Shape::Pyramid));
                const float w = round ? quarters : (special ? -h.len : h.len);
                m->userParams[4 + count * 2] = {h.nIn.x, h.nIn.y, h.nIn.z, w};
                // El arco se dibuja hacia donde la luz SALIO de la entrada (via):
                // el costado si la esfera la tiro hacia alli, la direccion
                // nueva si un cubo o una piramide la doblaron.
                m->userParams[5 + count * 2] = {h.via.x, h.via.y, h.via.z, &h == feeder ? -h.s0 : h.s0};
                ++count;
            }
            // .w es del director (el lado de la esfera, el tono del gusano).
            m->userParams[0].x = s.progress;
            m->userParams[0].y = solved;
            m->userParams[0].z = hidden ? 0.0f : static_cast<float>(count);
        }
    }

private:
    void WarnOnce(const wchar_t* text) {
        if (m_warned) return;
        m_warned = true;
        Log::Warn(text);
    }

    bool m_warned = false;
};

STRACE_REGISTER_SCRIPT(SurfaceLight)
