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
// mal en silencio. Un PLANO si recibe muchos (esquivar obstaculos sobre una
// pared), y los que pasan de dos van a sus capas de luz: hijos aditivos con la
// misma malla, de a dos tramos (BeamLayer, abajo; kPlaneBeamLayers).
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
#include "Engine/Text.h"

#include <memory>
#include <string>
#include <vector>

using namespace strace;
using namespace gravitational;

class SurfaceLight : public IScript {
public:
    ObjectField startPlane{this, "startPlane"};
    ObjectField endPlane{this, "endPlane"};
    ObjectField figures{this, "figures"};
    TextField beamMaterial{this, "beamMaterial", "Assets/Materials/SurfaceBeam.strmat"};

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
            // Los dos primeros tramos van al plano; los demas, de a dos, a sus
            // capas de luz (ver kPlaneBeamLayers en BeamPath.h).
            Material* layers[kPlaneBeamLayers] = {};
            int layerCount[kPlaneBeamLayers] = {};
            int count = 0, total = 0;
            for (const DrawHop& h : s.beam.hops) {
                if (!h.onPlane || h.plane != which) continue;
                if (total == kPlaneDrawSlots) {
                    WarnOnce(L"SurfaceLight: un plano tiene mas tramos de luz de los que puede dibujar; se dibujan los primeros");
                    break;
                }
                Material* target = m;
                int slot = total;
                if (total >= kDrawSlots) {
                    const int layer = total / kDrawSlots - 1;
                    if (!layers[layer]) layers[layer] = BeamLayer(*plane, layer);
                    target = layers[layer];
                    slot = total % kDrawSlots;
                    ++layerCount[layer];
                } else {
                    ++count;
                }
                ++total;
                if (!target) continue;
                target->userParams[4 + slot * 2] = {h.a.x, h.a.y, h.a.z, h.s0};
                target->userParams[5 + slot * 2] = {h.b.x, h.b.y, h.b.z, &h == feeder ? -h.s1 : h.s1};
            }
            // Las capas que no usa este recorrido se quedan sin tramos (no se
            // borran: el proximo cambio del tablero puede volver a pedirlas).
            for (int layer = 0; layer < kPlaneBeamLayers; ++layer) {
                Material* lm = layers[layer] ? layers[layer] : ExistingBeamLayer(*plane, layer);
                if (!lm) continue;
                lm->userParams[2] = {s.progress, solved, hidden ? 0.0f : static_cast<float>(layerCount[layer]), 0.0f};
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
    // LAS CAPAS DE LUZ de un plano: hijos "Luz N" con la misma primitiva y
    // transform identidad (asi cubren exactamente el mismo cuadrado, y en
    // profundidad empatan con el), y SurfaceBeam.strmat: aditivo, dibuja solo
    // la luz encima de lo que el plano ya dibujo. Se crean cuando un recorrido
    // las pide por primera vez: un nivel comun no las necesita nunca. El clic
    // las atraviesa (Picking.h solo se queda con piezas y planos), y como son
    // hijos, los planos de portal que el director rehace en cada nivel se
    // llevan las suyas.
    static std::string LayerName(int layer) { return "Luz " + std::to_string(layer + 1); }

    static GameObject* FindLayer(GameObject& plane, int layer) {
        const std::string name = LayerName(layer);
        for (const auto& child : plane.children)
            if (child && child->name == name) return child.get();
        return nullptr;
    }

    static Material* ExistingBeamLayer(GameObject& plane, int layer) {
        GameObject* node = FindLayer(plane, layer);
        return node ? SceneService::UniqueMaterial(*node) : nullptr;
    }

    Material* BeamLayer(GameObject& plane, int layer) {
        GameObject* node = FindLayer(plane, layer);
        if (!node) {
            auto child = std::make_unique<GameObject>();
            child->name = LayerName(layer);
            child->primitive = plane.primitive;
            child->meshPath = plane.meshPath;
            child->materialPath = Utf8ToWide(beamMaterial.Get());
            child->faceCulling = false;
            child->castShadows = false;
            child->parent = &plane;
            node = child.get();
            plane.children.push_back(std::move(child));
        }
        return SceneService::UniqueMaterial(*node);
    }

    void WarnOnce(const wchar_t* text) {
        if (m_warned) return;
        m_warned = true;
        Log::Warn(text);
    }

    bool m_warned = false;
};

STRACE_REGISTER_SCRIPT(SurfaceLight)
