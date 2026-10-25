// EL MENU PRINCIPAL - "THE FOLDED BOX": una escalera de dimensiones que
// termina en una caja. (Salio de comparar tres prototipos: la escalera de uno y
// la organizacion de otro; los tres quedaron en Archive/menus-descartados.)
//
//   0D  un punto de luz que late. Nada mas.
//   1D  se estira en una linea, y la luz la recorre escribiendo el titulo.
//   2D  la linea se abre en una lamina: la portada.
//   3D  de los cuatro bordes de la portada salen cuatro laminas mas - el
//       desplegable de un cubo - y se PLIEGAN alrededor de la camara: la caja.
//
// Adentro, cada cara es una seccion (piso JUGAR, izquierda NIVELES, derecha
// OPCIONES, techo CREDITOS) y se va de una a otra por las aristas, la camara
// siguiendo a la luz. Cada cara es una lamina de celdas con la cuadricula
// tenue, y la luz va de celda en celda - de la fuente al borde, por la arista, por un riel junto al borde, y
// hasta la celda de la fila elegida, que se llena.
//
// Para atras se pierde una dimension, en el orden inverso: Esc en una seccion
// vuelve a la portada; en la portada la caja se DESPLIEGA y se cierra en la
// lamina, la lamina en la linea, la linea en el punto; Esc en el punto pregunta
// si salir. Jugar despliega la caja (el cubo abierto se aplana y se apaga)
// mientras la camara baja a la orbita del nivel - y ahi sigue la transicion de
// nivel (Flow/LevelTransition.cpp): el punto, "LEVEL 01", y el nivel que crece.
// Volver de la partida la pliega otra vez alrededor.
//
// Las cinco caras son la misma lamina (Shaders/Sheet.hlsl) con otros ejes: el
// pliegue es solo el angulo de cada eje alrededor de su arista.

#include "../Audio/GameAudio.h"
#include "../Flow/GameFlow.h"
#include "../Flow/GameHost.h"
#include "../Flow/MenuKit.h"
#include "../Flow/MenuModel.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Text.h"
#include "Engine/UI/UiService.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace strace;
using namespace gravitational;
namespace kit = gravitational::kit;

namespace {

const char* kSheetMat = "Assets/Materials/Sheet.strmat";

constexpr float kHalf = 2.5f;              // la caja: 5 m de lado
constexpr float kCells = 9.0f;             // 9 x 9 celdas por cara: la del centro es UNA celda
constexpr float kCell = 2.0f * kHalf / kCells;
constexpr float kEdge = kCells * 0.5f;     // el borde, en celdas desde el centro (4,5)
constexpr float kRail = kEdge - 0.5f;      // la primera celda adentro del borde (4)
constexpr float kCamBack = 1.9f;           // adentro: la camara, atras del centro
constexpr float kOuterDist = 8.6f;         // afuera: de la portada a la camara
constexpr float kBeamCells = 13.0f;        // celdas por segundo de la luz al viajar
constexpr float kDeg = 3.14159265f / 180.0f;

struct V3 {
    float x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline bool Same(V3 a, V3 b) { return std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) < 1e-4f; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

struct P2 {
    float u = 0, v = 0;
};

// Las caras. Back es la portada; las otras cuatro cuelgan de sus bordes.
enum Face { Back = 0, Floor, Left, Right, Ceiling, kFaceCount };

// En la caja ARMADA: la normal hacia afuera, y los ejes de pantalla de quien la
// mira desde adentro (los del menu C). Con la caja desplegada, cada eje que
// apunta a la portada (+Z) se reemplaza por el que apunta a la arista.
struct FaceDef {
    V3 normal, right, up;
    float yaw, pitch;
};
const V3 kBackN{0, 0, 1};
const FaceDef kFaces[kFaceCount] = {
    /* Back    */ {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, 0.0f, 0.0f},
    /* Floor   */ {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}, 0.0f, 90.0f},
    /* Left    */ {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}, -90.0f, 0.0f},
    /* Right   */ {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}, 90.0f, 0.0f},
    /* Ceiling */ {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}, 0.0f, -90.0f},
};

// Lo que muestra una lamina: cuanto se estiro (punto -> linea), cuanto se abrio
// (linea -> plano) y cuanto brilla su cuadricula.
struct SheetLook {
    float stretch = 0, open = 0, grid = 1, dot = 0;
};

struct Pose {
    float pos[3] = {}, rot[3] = {};
};

enum class Phase { Point, Line, Rise, Assemble, Idle, Travel, Disassemble, Fall, ToGame, Game, FromGame };

} // namespace

class MainMenu : public IScript {
public:
    ObjectField camera{this, "camera"};

    void OnStart() override {
        GameFlow& flow = Flow();
        flow.EnterMenu();
        host::LoadProgress(flow);
        host::ApplySettings(flow.settings);
        kit::BindMenuInput();
        BuildSheets();
        m_look[Back].dot = 1.0f;
        Enter(Phase::Point);
        gameaudio::Play("Ui/menu_wake"); // el punto aparece de la nada
        Log::Info(L"MainMenu: listo");
    }

    // Al detener el Play: el flujo vuelve a "sin menu" (ver PuzzleDirector).
    void OnDestroy() override { Flow() = GameFlow{}; }

    void OnUpdate(float dt) override {
        GameFlow& flow = Flow();
        m_time += dt;
        m_phaseTime += dt;
        m_dt = dt;
        UiDrawList& ui = UiService::Frame();

        bool skipInput = false;
        if (flow.returnRequested) {
            flow.returnRequested = false;
            skipInput = true;
            BeginFromGame(flow.returnScreen);
        }
        gameObject->active = !flow.InGame();
        if (flow.InGame()) return;

        kit::Nav nav = kit::ReadNav();
        const bool interactive = m_phase == Phase::Point || m_phase == Phase::Line || m_phase == Phase::Idle;
        if (skipInput || !interactive) nav = kit::Nav{};
        AutoDemo(nav);

        switch (m_phase) {
            case Phase::Point: UpdatePoint(nav); break;
            case Phase::Line: UpdateLine(nav); break;
            case Phase::Rise: UpdateRise(); break;
            case Phase::Assemble: UpdateFold(true); break;
            case Phase::Disassemble: UpdateFold(false); break;
            case Phase::Fall: UpdateFall(); break;
            case Phase::Idle: UpdateIdle(nav); break;
            case Phase::Travel: UpdateTravel(dt); break;
            case Phase::ToGame: UpdateToGame(); break;
            case Phase::FromGame: UpdateFromGame(); break;
            case Phase::Game: break;
        }
        AnimateLooks(dt);
        PushSheets();
        DrawText(ui);

        m_toast = std::max(0.0f, m_toast - dt);
        kit::Toast(ui, m_toastText, std::min(1.0f, m_toast));
    }

private:
    // ------------------------------------------------------------------
    // Las laminas y su pliegue
    // ------------------------------------------------------------------
    void BuildSheets() {
        gameObject->children.clear();
        gameObject->transform = Transform{};
        static const char* names[kFaceCount] = {"D_Portada", "D_Piso", "D_Izquierda", "D_Derecha", "D_Techo"};
        for (int f = 0; f < kFaceCount; ++f) {
            auto node = std::make_unique<GameObject>();
            node->name = names[f];
            node->primitive = PrimitiveType::Plane;
            node->materialPath = Utf8ToWide(kSheetMat);
            node->castShadows = false;
            node->faceCulling = false;
            node->parent = gameObject;
            m_sheets[f] = node.get();
            gameObject->children.push_back(std::move(node));
        }
    }

    // Los ejes de la cara f con el pliegue actual. Una cara lateral gira
    // alrededor de la arista que comparte con la portada: desplegada (0) queda
    // en el plano de la portada, a su lado; plegada (1) es su pared de la caja.
    // `d` es la direccion, sobre la lamina, del centro hacia esa arista.
    void Frame(int f, V3& origin, V3& right, V3& up) const {
        if (f == Back) {
            origin = kBackN * kHalf;
            right = kFaces[Back].right;
            up = kFaces[Back].up;
            return;
        }
        const float th = m_fold * 90.0f * kDeg;
        const V3 n = kFaces[f].normal;
        const V3 d = (n * -1.0f) * std::cos(th) + kBackN * std::sin(th);
        const V3 hinge = n * kHalf + kBackN * kHalf;
        origin = hinge - d * kHalf;
        auto map = [&](V3 a) { return Same(a, kBackN) ? d : Same(a, kBackN * -1.0f) ? d * -1.0f : a; };
        right = map(kFaces[f].right);
        up = map(kFaces[f].up);
    }

    V3 SheetToWorld(int f, P2 p) const {
        V3 o, r, u;
        Frame(f, o, r, u);
        return o + r * (p.u * kCell) + u * (p.v * kCell);
    }

    // Por donde entra la luz a una cara lateral (en sus celdas): el medio de la
    // arista con la portada. En la portada, por donde SALE hacia la cara f.
    static P2 EntrySide(int f) { return {Dot(kBackN, kFaces[f].right), Dot(kBackN, kFaces[f].up)}; }
    static P2 ExitOnBack(int f) { return {Dot(kFaces[f].normal, kFaces[Back].right), Dot(kFaces[f].normal, kFaces[Back].up)}; }

    void PushSheets() {
        for (int f = 0; f < kFaceCount; ++f) {
            GameObject* s = m_sheets[f];
            V3 o, r, u;
            Frame(f, o, r, u);
            const V3 n = Cross(r, u);
            s->transform.position[0] = o.x;
            s->transform.position[1] = o.y;
            s->transform.position[2] = o.z;
            // La primitiva Plane mira a +Y; la lamina es cuadrada, asi que basta
            // con llevar su normal a la de la cara (el giro dentro del plano no
            // cambia nada: el shader mide con los ejes que le llegan).
            s->transform.rotation[0] = std::acos(std::clamp(n.y, -1.0f, 1.0f)) / kDeg;
            s->transform.rotation[1] = std::atan2(n.x, n.z) / kDeg;
            s->transform.rotation[2] = 0.0f;
            // Del tamano de la lamina y nada mas: desplegadas, las laterales son
            // coplanares con la portada, y un margen (aunque ahi no dibuje nada)
            // le tapaba los costados. Los halos no llegan al borde de todos modos.
            const float half = (kEdge + 0.05f) * kCell;
            s->transform.scale[0] = half;
            s->transform.scale[1] = 1.0f;
            s->transform.scale[2] = half;

            Material* m = SceneService::UniqueMaterial(*s);
            if (!m) continue;
            const SheetLook& lk = m_look[f];
            m->userParams[0] = {o.x, o.y, o.z, kCell};
            m->userParams[1] = {r.x, r.y, r.z, lk.stretch};
            m->userParams[2] = {u.x, u.y, u.z, lk.open};
            m->userParams[3] = {kCells, kCells, lk.grid * m_fade, lk.dot};
            const std::vector<Seg>& segs = m_segs[f];
            m->userParams[4] = {m_sel[f].u, m_sel[f].v, m_selA[f] * m_fade, static_cast<float>(std::min<size_t>(segs.size(), 3))};
            for (size_t i = 0; i < 3; ++i) {
                if (i < segs.size()) m->userParams[5 + i] = {segs[i].a.u, segs[i].a.v, segs[i].b.u, segs[i].b.v};
                else m->userParams[5 + i] = {0, 0, 0, 0};
            }
        }
    }

    // ------------------------------------------------------------------
    // La luz: un recorrido de celdas que cruza de la portada a una cara.
    // ------------------------------------------------------------------
    struct Seg {
        P2 a, b;
    };
    struct RouteSeg {
        int face;
        P2 a, b;
        float Len() const { return std::fabs(b.u - a.u) + std::fabs(b.v - a.v); }
    };

    // Las filas de una cara: cuantas, y donde esta la celda de la fila i.
    int RowCount(int f) const {
        const GameFlow& fl = Flow();
        switch (f) {
            case Floor: return fl.HasProgress() ? 2 : 1;
            case Left: return std::max(1, fl.levelCount);
            case Right: return menu::kOptionCount;
            default: return 0;
        }
    }
    // NIVELES va por PAGINAS de seis filas: la cara tiene lugar para seis
    // debajo del titulo, y con 23 niveles la lista se salia de la caja por
    // abajo. La pagina es la de la fila elegida, asi que bajar con la flecha
    // pasa de pagina solo; izquierda y derecha saltan de a una.
    static constexpr int kRowsPerPage = 6;
    int PageStart(int f) const { return f == Left ? (m_row / kRowsPerPage) * kRowsPerPage : 0; }

    // El riel corre junto a la arista de entrada; las filas cuelgan de el.
    P2 Marker(int f, int row) const {
        const P2 e = EntrySide(f);
        const int slot = f == Left ? row % kRowsPerPage : row;
        if (f == Ceiling) return {0.0f, e.v * kRail};                 // los creditos: solo llegar
        if (std::fabs(e.u) > 0.5f) return {e.u * kRail, 2.0f - slot};  // izq./der.: riel vertical
        return {-kRail, 2.0f - slot};                                   // piso: riel a la izquierda
    }

    std::vector<RouteSeg> Route(int f, int row) const {
        std::vector<RouteSeg> r;
        const P2 exit = ExitOnBack(f);
        r.push_back({Back, {0, 0}, {exit.u * kEdge, exit.v * kEdge}});
        const P2 e = EntrySide(f);
        const P2 edge{e.u * kEdge, e.v * kEdge};
        const P2 first{e.u * kRail, e.v * kRail};
        const P2 mk = Marker(f, row);
        r.push_back({f, edge, first});
        if (std::fabs(e.u) > 0.5f) {
            r.push_back({f, first, {first.u, mk.v}});
        } else if (f != Ceiling) {
            r.push_back({f, first, {mk.u, first.v}});
            r.push_back({f, {mk.u, first.v}, mk});
        }
        return r;
    }

    static float RouteLen(const std::vector<RouteSeg>& r) {
        float s = 0;
        for (const RouteSeg& x : r) s += x.Len();
        return s;
    }

    // Reparte lo revelado (`reveal` celdas desde la fuente) entre las caras.
    void LayRoute(const std::vector<RouteSeg>& route, float reveal) {
        float left = reveal;
        for (const RouteSeg& s : route) {
            const float len = s.Len();
            if (left <= 0.0f || len < 1e-4f) break;
            const float k = std::min(1.0f, left / len);
            if (m_segs[s.face].size() < 3) m_segs[s.face].push_back({s.a, {s.a.u + (s.b.u - s.a.u) * k, s.a.v + (s.b.v - s.a.v) * k}});
            left -= len;
        }
    }

    void ClearLight() {
        for (int f = 0; f < kFaceCount; ++f) {
            m_segs[f].clear();
            m_selA[f] = 0.0f;
        }
    }

    // ------------------------------------------------------------------
    // La camara
    // ------------------------------------------------------------------
    static Pose OuterPose() {
        Pose p;
        p.pos[2] = kHalf - kOuterDist;
        return p;
    }
    static Pose FacePose(int f) {
        Pose p;
        const V3 c = kFaces[f].normal * (-kCamBack);
        p.pos[0] = c.x;
        p.pos[1] = c.y;
        p.pos[2] = c.z;
        p.rot[0] = kFaces[f].pitch;
        p.rot[1] = kFaces[f].yaw;
        return p;
    }
    static Pose HomePose() {
        Pose p;
        const GameFlow& f = Flow();
        for (int i = 0; i < 3; ++i) {
            p.pos[i] = f.homePos[i];
            p.rot[i] = f.homeRot[i];
        }
        return p;
    }
    static Pose Blend(const Pose& a, const Pose& b, float t) {
        Pose p;
        for (int i = 0; i < 3; ++i) {
            p.pos[i] = kit::Lerp(a.pos[i], b.pos[i], t);
            p.rot[i] = kit::LerpAngle(a.rot[i], b.rot[i], t);
        }
        return p;
    }
    Pose CurrentCamera() const {
        Pose p;
        if (const GameObject* cam = camera.Get())
            for (int i = 0; i < 3; ++i) {
                p.pos[i] = cam->transform.position[i];
                p.rot[i] = cam->transform.rotation[i];
            }
        return p;
    }
    void SetCamera(const Pose& p) {
        GameObject* cam = camera.Get();
        if (!cam) return;
        for (int i = 0; i < 3; ++i) {
            cam->transform.position[i] = p.pos[i];
            cam->transform.rotation[i] = p.rot[i];
        }
    }
    // Quieto en una cara, respira apenas (un menu inmovil parece una captura).
    Pose Breathing(int f) const {
        Pose p = FacePose(f);
        p.rot[1] += 0.8f * std::sin(m_time * 0.31f);
        p.rot[0] += 0.5f * std::sin(m_time * 0.23f);
        return p;
    }

    // ------------------------------------------------------------------
    // Fases
    // ------------------------------------------------------------------
    void Enter(Phase p) {
        Sound(m_phase, p);
        m_phase = p;
        m_phaseTime = 0.0f;
    }

    // Cada fase suena al EMPEZAR, y lo que suena depende de donde viene: la
    // linea que nace del punto no es la linea a la que se vuelve desde la caja.
    // En un solo sitio para que una fase nueva no quede muda sin que nadie lo
    // note.
    static void Sound(Phase from, Phase to) {
        if (from == Phase::Point && to == Phase::Line) gameaudio::Play("Ui/menu_line");
        if (from == Phase::Line && to == Phase::Point) gameaudio::Play("Ui/menu_close");
        if (to == Phase::Rise) gameaudio::Play("Ui/menu_open");
        if (to == Phase::Assemble)
            // Las cuatro laminas aterrizan al final del plegado (1,35 s); el
            // sonido de una tarda 0,38 s del barrido al golpe. Corridas apenas,
            // para que se oigan cuatro y no una fuerte.
            for (int i = 0; i < 4; ++i) gameaudio::PlayLater("Ui/menu_fold", 0.85f + 0.07f * i, -2.0f * i);
        if (to == Phase::Disassemble) gameaudio::Play("Ui/menu_close");
        if (to == Phase::ToGame) {
            gameaudio::Play("Ui/ui_accept");
            gameaudio::Play("Ui/ui_move");
        }
        if (to == Phase::FromGame) gameaudio::Play("Ui/menu_open");
    }

    static bool AnyKey(const kit::Nav& n) { return n.ok || n.click || n.up || n.down || n.left || n.right; }

    // Targets de cada lamina por fase; AnimateLooks los persigue.
    void Targets(float backStretch, float backOpen, float backDot, float sideStretch, float sideOpen) {
        m_target[Back] = {backStretch, backOpen, 1.0f, backDot};
        for (int f = 1; f < kFaceCount; ++f) m_target[f] = {sideStretch, sideOpen, m_target[f].grid, 0.0f};
    }

    void UpdatePoint(const kit::Nav& nav) {
        Targets(0, 0, 1, 0, 0);
        m_fold = 0.0f;
        m_titleChars = 0.0f;
        SetCamera(OuterPose());
        ClearLight();
        if (m_quitAsk) {
            if (nav.ok) {
                m_quitAsk = false;
                gameaudio::Play("Ui/ui_accept");
                if (!host::RequestQuit()) {
                    m_toastText = "IN THE EDITOR, QUIT DOES NOT CLOSE: STOP PLAY";
                    m_toast = 3.0f;
                }
            } else if (nav.back || nav.click || nav.left || nav.right || nav.up || nav.down) {
                m_quitAsk = false;
                gameaudio::Play("Ui/ui_back");
            }
            return;
        }
        if (nav.back && m_phaseTime > 0.2f) {
            m_quitAsk = true;
            gameaudio::Play("Ui/quit_prompt");
        }
        else if (AnyKey(nav) && m_phaseTime > 0.3f) {
            m_autoRise = true;
            Enter(Phase::Line);
        }
    }

    void UpdateLine(const kit::Nav& nav) {
        Targets(1, 0, 0, 0, 0);
        m_fold = 0.0f;
        SetCamera(OuterPose());
        ClearLight();
        if (m_look[Back].stretch > 0.97f) m_titleChars = std::min(m_titleChars + m_dt * 10.0f, 13.0f);
        // La luz que escribe: recorre la linea al paso de las letras.
        if (m_titleChars > 0.0f && m_titleChars < 13.0f) {
            const float half = kEdge * m_look[Back].stretch;
            m_segs[Back].push_back({{-half, 0}, {-half + 2.0f * half * (m_titleChars / 13.0f), 0}});
        }
        const bool written = m_titleChars >= 13.0f;
        if (written && m_autoRise && m_phaseTime > 2.4f) {
            m_autoRise = false;
            Enter(Phase::Rise);
        } else if (written && !m_autoRise && (nav.ok || nav.click || nav.up || nav.down)) {
            Enter(Phase::Rise);
        } else if (nav.back) {
            Enter(Phase::Point);
        }
    }

    // La portada se abre; de sus bordes salen las otras cuatro (primero como
    // lineas, despues como laminas), todavia desplegadas.
    void UpdateRise() {
        SetCamera(OuterPose());
        ClearLight();
        const float sideStretch = m_look[Back].open > 0.95f ? 1.0f : 0.0f;
        const float sideOpen = m_look[Floor].stretch > 0.95f ? 1.0f : 0.0f;
        Targets(1, 1, 0, sideStretch, sideOpen);
        if (m_look[Floor].open > 0.97f) {
            m_camFrom = OuterPose();
            Enter(Phase::Assemble);
        }
    }

    // El cubo se pliega (armar) o se despliega (desarmar) alrededor de la
    // camara, que entra o sale por donde estaria la cara de enfrente.
    void UpdateFold(bool assemble) {
        Targets(1, 1, 0, 1, 1);
        ClearLight();
        const float dur = assemble ? 1.35f : 1.1f;
        const float t = kit::EaseInOutCubic(m_phaseTime / dur);
        m_fold = assemble ? t : 1.0f - t;
        SetCamera(assemble ? Blend(OuterPose(), FacePose(Back), t) : Blend(m_camFrom, OuterPose(), t));
        if (m_phaseTime >= dur) {
            if (assemble) {
                m_face = Back;
                m_arrive = 0.0f;
                m_previewFrom = -1;
                Enter(Phase::Idle);
            } else {
                Enter(Phase::Fall);
            }
        }
    }

    // Desplegada, la caja se cierra: las laterales en lineas que se recogen, y
    // la portada en la linea del titulo.
    void UpdateFall() {
        SetCamera(OuterPose());
        ClearLight();
        const float backOpen = m_look[Floor].stretch < 0.05f ? 0.0f : 1.0f;
        Targets(1, backOpen, 0, 0, 0);
        if (m_look[Back].open < 0.05f) {
            m_autoRise = false;
            Enter(Phase::Line);
        }
    }

    // --- Adentro ---
    void UpdateIdle(const kit::Nav& nav) {
        Targets(1, 1, 0, 1, 1);
        m_fold = 1.0f;
        m_arrive += m_dt;
        SetCamera(Breathing(m_face));
        for (int f = 0; f < kFaceCount; ++f) m_target[f].grid = f == m_face ? 1.0f : 0.4f;
        const int h = Hovered(nav);
        if (m_face == Back) IdleHub(nav, h);
        else IdleFace(nav, h);
        LightIdle();
    }

    static constexpr int kExits[4] = {Floor, Left, Right, Ceiling};

    void IdleHub(const kit::Nav& nav, int hovered) {
        if (hovered >= 0 && nav.mouseMoved && hovered != m_hubSel) {
            m_hubSel = hovered;
            gameaudio::Play("Ui/ui_tick");
        }
        if (nav.down) Go(Floor);
        else if (nav.left) Go(Left);
        else if (nav.right) Go(Right);
        else if (nav.up) Go(Ceiling);
        else if (nav.click && hovered >= 0) Go(kExits[hovered]);
        else if (nav.ok) Go(kExits[m_hubSel]);
        else if (nav.back) {
            // Una dimension menos: la caja se despliega.
            m_camFrom = CurrentCamera();
            Enter(Phase::Disassemble);
        }
    }

    void IdleFace(const kit::Nav& nav, int hovered) {
        GameFlow& fl = Flow();
        const int n = RowCount(m_face);
        if (hovered >= 0) hovered += PageStart(m_face); // los rectangulos son los de la pagina
        int pick = -1;
        if (n > 0) {
            int row = m_row;
            if (nav.up) row = (row + n - 1) % n;
            if (nav.down) row = (row + 1) % n;
            if (m_face == Left && nav.right) row = std::min(n - 1, row + kRowsPerPage);
            if (m_face == Left && nav.left) row = std::max(0, row - kRowsPerPage);
            if (hovered >= 0 && nav.mouseMoved) row = hovered;
            if (nav.click && hovered >= 0) row = hovered;
            if (row != m_row) {
                gameaudio::Play("Ui/ui_tick");
                m_row = row;
                m_rowReveal = 0.0f; // la luz vuelve a bajar por el riel hasta la nueva
                m_options.Disarm();
            }
            if (nav.ok || (nav.click && hovered >= 0)) pick = m_row;
        }
        switch (m_face) {
            case Floor:
                if (pick == 0) BeginToGame(fl.ContinueLevel());
                if (pick == 1) BeginToGame(0);
                break;
            case Left:
                if (pick >= 0 && fl.Unlocked(pick)) BeginToGame(pick);
                else if (pick >= 0) gameaudio::Play("Ui/ui_denied");
                break;
            case Right:
                if (nav.left) menu::ChangeOption(m_row, -1, fl, m_options);
                if (nav.right) menu::ChangeOption(m_row, +1, fl, m_options);
                if (pick >= 0) menu::ChangeOption(pick, 0, fl, m_options);
                if (nav.left || nav.right || pick >= 0) gameaudio::Play("Ui/ui_toggle");
                break;
            case Ceiling:
                if (nav.ok || nav.click) {
                    fl.finishedAll = false;
                    Go(Back);
                    return;
                }
                break;
            default: break;
        }
        if (nav.back) Go(Back);
    }

    // La luz quieta: en la portada, una vista previa hacia la salida elegida;
    // en una cara, el recorrido entero hasta la fila elegida.
    void LightIdle() {
        ClearLight();
        if (m_face == Back) {
            if (m_previewFrom != m_hubSel) {
                m_previewFrom = m_hubSel;
                m_preview = 0.0f;
            }
            m_preview = std::min(m_preview + m_dt * 26.0f, kEdge);
            const P2 e = ExitOnBack(kExits[m_hubSel]);
            m_segs[Back].push_back({{0, 0}, {e.u * m_preview, e.v * m_preview}});
            m_sel[Back] = {0, 0};
            m_selA[Back] = 0.55f;
            return;
        }
        // La parte hasta la arista y el primer tramo quedan; lo que cambia con
        // la fila se vuelve a revelar.
        const std::vector<RouteSeg> route = Route(m_face, m_row);
        const float fixed = kEdge + (kEdge - kRail);
        m_rowReveal = std::min(m_rowReveal + m_dt * 30.0f, 100.0f);
        const float reveal = fixed + m_rowReveal;
        LayRoute(route, reveal);
        m_sel[m_face] = Marker(m_face, m_row);
        m_selA[m_face] = reveal >= RouteLen(route) - 1e-3f ? 1.0f : 0.3f;
    }

    // --- Ir por una arista ---
    void Go(int target) {
        if (target == m_face) return;
        m_from = m_face;
        m_dest = target;
        m_toHub = target == Back;
        m_camFrom = CurrentCamera();
        // Volviendo, la luz se recoge desde donde esta hasta la fuente.
        m_travelRoute = Route(m_toHub ? m_from : m_dest, m_toHub ? m_row : 0);
        m_travelLen = RouteLen(m_travelRoute);
        m_beam = m_toHub ? m_travelLen : 0.0f;
        if (!m_toHub) m_row = 0;
        m_rowReveal = 100.0f;
        m_options.Disarm();
        gameaudio::Play(m_toHub ? "Ui/ui_back" : "Ui/ui_accept");
        gameaudio::Play("Ui/ui_move", -2.0f);
        Enter(Phase::Travel);
    }

    void UpdateTravel(float dt) {
        Targets(1, 1, 0, 1, 1);
        if (m_toHub) m_beam = std::max(0.0f, m_beam - dt * kBeamCells * 1.5f);
        else m_beam = std::min(m_travelLen, m_beam + dt * kBeamCells);
        // La camara gira mientras la luz cruza la arista: de la mitad de la
        // portada al final del riel.
        const float edgeAt = kEdge;
        const float k = (m_beam - edgeAt * 0.5f) / std::max(0.01f, (std::min(m_travelLen, edgeAt + 1.5f) - edgeAt * 0.5f));
        const float t = kit::EaseInOutCubic(m_toHub ? 1.0f - kit::Saturate(k) : kit::Saturate(k));
        const int sectionFace = m_toHub ? m_from : m_dest;
        SetCamera(m_toHub ? Blend(FacePose(sectionFace), FacePose(Back), t) : Blend(m_camFrom, FacePose(m_dest), t));
        for (int f = 0; f < kFaceCount; ++f) m_target[f].grid = f == Back || f == sectionFace ? 0.8f : 0.4f;
        ClearLight();
        LayRoute(m_travelRoute, m_beam);
        const bool done = m_toHub ? m_beam <= 0.0f : m_beam >= m_travelLen;
        if (done) {
            m_face = m_dest;
            m_arrive = 0.0f;
            m_previewFrom = -1;
            Enter(Phase::Idle);
        }
    }

    // --- A la partida: el cubo se despliega y se apaga, la camara baja a la
    // orbita. Y de vuelta, al reves. ---
    void BeginToGame(int level) {
        m_level = level;
        m_camFrom = CurrentCamera();
        Enter(Phase::ToGame);
    }

    void UpdateToGame() {
        const float dur = 1.5f;
        const float t = kit::Saturate(m_phaseTime / dur);
        m_fold = 1.0f - kit::EaseInOutCubic(t);
        m_fade = 1.0f - kit::SmoothStep(t / 0.85f);
        SetCamera(Blend(m_camFrom, HomePose(), kit::EaseInOutCubic(t)));
        if (t > 0.35f) ClearLight();
        if (t >= 1.0f) {
            Enter(Phase::Game);
            Flow().StartLevel(m_level);
        }
    }

    void BeginFromGame(MenuScreen screen) {
        m_camFrom = CurrentCamera();
        m_face = screen == MenuScreen::Credits ? Ceiling : screen == MenuScreen::Levels ? Left : Back;
        m_row = 0;
        m_rowReveal = 100.0f;
        m_quitAsk = false;
        m_titleChars = 13.0f;
        for (int f = 0; f < kFaceCount; ++f) m_look[f] = {1, 1, f == m_face ? 1.0f : 0.4f, 0};
        m_fold = 0.0f;
        m_fade = 0.0f;
        ClearLight();
        Enter(Phase::FromGame);
    }

    void UpdateFromGame() {
        const float dur = 1.3f;
        const float t = kit::Saturate(m_phaseTime / dur);
        m_fold = kit::EaseInOutCubic(t);
        m_fade = kit::SmoothStep(t);
        SetCamera(Blend(m_camFrom, FacePose(m_face), kit::EaseInOutCubic(t)));
        if (t >= 1.0f) {
            m_arrive = 0.0f;
            m_previewFrom = -1;
            Enter(Phase::Idle);
        }
    }

    // Persigue los targets: estirar es lento (se ve viajar), abrir es rapido.
    void AnimateLooks(float dt) {
        for (int f = 0; f < kFaceCount; ++f) {
            SheetLook& l = m_look[f];
            const SheetLook& t = m_target[f];
            const float stretchRate = f == Back ? (t.stretch > l.stretch ? 3.2f : 7.0f) : 6.0f;
            l.stretch = kit::Approach(l.stretch, t.stretch, stretchRate, dt);
            l.open = kit::Approach(l.open, t.open, 9.0f, dt);
            l.grid = kit::Approach(l.grid, t.grid, 6.0f, dt);
            l.dot = kit::Approach(l.dot, t.dot, 5.0f, dt);
        }
    }

    // ------------------------------------------------------------------
    // El texto, sobre la cara que se mira
    // ------------------------------------------------------------------
    bool At(const DirectX::XMMATRIX& vp, int f, float u, float v, float& x, float& y) const {
        const V3 p = SheetToWorld(f, {u, v});
        return kit::WorldToScreen(vp, p.x, p.y, p.z, x, y);
    }

    int Hovered(const kit::Nav& nav) const {
        if (!nav.mouseInside) return -1;
        for (size_t i = 0; i < m_rects.size(); ++i)
            if (kit::Inside(m_rects[i], nav.mx, nav.my, 6.0f)) return static_cast<int>(i);
        return -1;
    }

    void DrawText(UiDrawList& ui) {
        m_rects.clear();
        GameObject* cam = camera.Get();
        if (!cam || !cam->camera) return;
        const DirectX::XMMATRIX vp = kit::ViewProj(*cam);
        const float W = UiService::Width(), H = UiService::Height();
        const int s = kit::TextScale();
        const int ls = std::max(2, s - 1);
        const int big = kit::TextScale(1.6f);
        const GameFlow& fl = Flow();

        // --- 0D ---
        if (m_phase == Phase::Point) {
            if (m_quitAsk) {
                kit::TextCentered(ui, W * 0.5f, H * 0.70f, "QUIT THE GAME?", kit::White(), s);
                kit::Hint(ui, "ENTER  QUIT     ESC  STAY");
            } else {
                const float a = kit::Saturate((m_phaseTime - 1.4f) / 1.0f) * (0.55f + 0.45f * std::sin(m_time * 2.0f));
                kit::TextCentered(ui, W * 0.5f, H * 0.80f, "PRESS ANY KEY", kit::Faint(a), std::max(1, s - 1));
            }
            return;
        }

        // --- El titulo: sobre la linea, y la portada al abrirse lo lleva a
        // su lugar adentro. ---
        const bool titleVisible = m_phase == Phase::Line || m_phase == Phase::Rise || m_phase == Phase::Assemble ||
                                  m_phase == Phase::Disassemble || m_phase == Phase::Fall ||
                                  (m_phase == Phase::Idle && m_face == Back);
        if (titleVisible) {
            const float open = m_look[Back].open;
            const float v = kit::Lerp(1.4f, 2.4f, open);
            float tx, ty;
            if (At(vp, Back, 0.0f, v, tx, ty)) {
                const std::string title = "GRAVITATIONAL";
                const float w = kit::TextW(title, big);
                kit::TextTyped(ui, std::floor(tx - w * 0.5f), std::floor(ty - kit::TextH(big) * 0.5f), title, m_titleChars, kit::White(),
                               big);
                if (m_phase == Phase::Line && m_titleChars >= 13.0f) {
                    const float a = kit::Saturate((m_phaseTime - 1.5f) / 0.6f);
                    kit::TextCentered(ui, tx, ty + kit::TextH(big) + 3.0f * s, "LIGHT ONLY TRAVELS ALONG SURFACES", kit::Faint(a),
                                      std::max(1, s - 1));
                }
            }
        }
        if (m_phase == Phase::Line) {
            if (!m_autoRise && m_titleChars >= 13.0f) kit::Hint(ui, "ENTER  OPEN     ESC  ONE DIMENSION DOWN");
            return;
        }
        if (m_phase != Phase::Idle) return;

        // El resto, solo con la camara quieta: girando, un texto pegado a la
        // pantalla y no a la lamina se nota falso.
        const float a = kit::Saturate(m_arrive / 0.25f);
        switch (m_face) {
            case Back: {
                const std::string play = menu::MainLabel(menu::MainItem::Play, fl);
                const std::string labels[4] = {play, "LEVELS", "OPTIONS", "CREDITS"};
                // Las etiquetas junto a cada arista, al costado de la luz (no
                // encima): (u, v) del texto y si se alinea a izquierda/derecha.
                const float pos[4][2] = {{0.6f, -3.8f}, {-4.2f, 0.75f}, {4.2f, 0.75f}, {0.6f, 3.8f}};
                const int align[4] = {-1, -1, 1, -1};
                const char* arrows[4] = {"v ", "< ", " >", "^ "};
                for (int i = 0; i < 4; ++i) {
                    float x, y;
                    At(vp, Back, pos[i][0], pos[i][1], x, y);
                    const std::string text = i == 2 ? labels[i] + arrows[i] : arrows[i] + labels[i];
                    const float w = kit::TextW(text, ls);
                    const float x0 = align[i] < 0 ? x : x - w;
                    const bool sel = i == m_hubSel;
                    const UiRect r{std::floor(x0), std::floor(y - kit::TextH(ls) * 0.5f), w, kit::TextH(ls)};
                    ui.AddText(r.x, r.y, text, sel ? kit::White(a) : kit::Dim(a), ls);
                    if (sel) ui.AddRect(UiRect{r.x, r.y + r.height + ls * 2.0f, r.width, static_cast<float>(ls)}, kit::Beam(a));
                    m_rects.push_back(r);
                }
                kit::Hint(ui, "ARROWS  CROSS AN EDGE     ENTER  ACCEPT     ESC  ONE DIMENSION DOWN", a);
                break;
            }
            case Floor: {
                Header(ui, vp, Floor, play_label(fl), a);
                std::vector<Row> rows;
                rows.push_back({fl.HasProgress() ? "CONTINUE" : "START", "LEVEL " + menu::LevelName(fl, fl.ContinueLevel()), true});
                if (fl.HasProgress()) rows.push_back({"FROM THE BEGINNING", "LEVEL 01", true});
                Rows(ui, vp, Floor, rows, a);
                kit::Hint(ui, "ENTER  ENTER THE LEVEL     ESC  BACK ACROSS THE EDGE", a);
                break;
            }
            case Left: {
                const int first = PageStart(Left), last = std::min(fl.levelCount, first + kRowsPerPage);
                char head[48];
                std::snprintf(head, sizeof(head), "LEVELS  %02d-%02d / %02d", first + 1, last, fl.levelCount);
                Header(ui, vp, Left, fl.levelCount > kRowsPerPage ? head : "LEVELS", a);
                std::vector<Row> rows;
                for (int i = 0; i < fl.levelCount; ++i)
                    rows.push_back({"LEVEL " + menu::LevelName(fl, i), fl.Solved(i) ? "SOLVED" : fl.Unlocked(i) ? "NEW" : "LOCKED",
                                    fl.Unlocked(i)});
                Rows(ui, vp, Left, rows, a);
                kit::Hint(ui, fl.levelCount > kRowsPerPage ? "UP/DOWN  CHOOSE     <- ->  PAGE     ENTER  PLAY     ESC  BACK ACROSS THE EDGE"
                                                           : "UP/DOWN  CHOOSE     ENTER  PLAY     ESC  BACK ACROSS THE EDGE",
                          a);
                break;
            }
            case Right: {
                Header(ui, vp, Right, "OPTIONS", a);
                std::vector<Row> rows;
                for (int i = 0; i < menu::kOptionCount; ++i) rows.push_back({menu::OptionLabel(i), menu::OptionValue(i, fl, m_options), true});
                Rows(ui, vp, Right, rows, a);
                kit::Hint(ui, "UP/DOWN  CHOOSE     <- ->  CHANGE     ESC  BACK ACROSS THE EDGE", a);
                break;
            }
            case Ceiling: {
                std::vector<std::string> lines;
                if (fl.finishedAll) lines.push_back("YOU SOLVED EVERY LEVEL");
                for (const std::string& l : menu::CreditLines()) lines.push_back(l);
                const float top = 3.6f, bottom = -3.0f;
                const float step = (top - bottom) / std::max<size_t>(1, lines.size() - 1);
                for (size_t i = 0; i < lines.size(); ++i) {
                    float x, y;
                    At(vp, Ceiling, 0.0f, top - step * i, x, y);
                    const bool head = i == 0 || lines[i - 1].empty();
                    kit::TextCentered(ui, x, y, lines[i], head ? kit::White(a) : kit::Dim(a), ls);
                }
                kit::Hint(ui, "ESC / ENTER  BACK ACROSS THE EDGE", a);
                break;
            }
            default: break;
        }
    }

    static std::string play_label(const GameFlow& fl) { return menu::MainLabel(menu::MainItem::Play, fl); }

    void Header(UiDrawList& ui, const DirectX::XMMATRIX& vp, int f, const std::string& text, float a) {
        float x, y;
        const P2 e = EntrySide(f);
        // Arriba a la izquierda, del lado contrario al riel si el riel esta a la izquierda.
        const float u = e.u < -0.5f || f == Floor ? -3.3f : -4.2f;
        At(vp, f, u, 3.3f, x, y);
        ui.AddText(std::floor(x), std::floor(y - kit::TextH(kit::TextScale()) * 0.5f), text, kit::Beam(a), kit::TextScale());
    }

    struct Row {
        std::string label, value;
        bool enabled = true;
    };

    // Una fila por celda del riel: la etiqueta al lado de su celda, el valor
    // del otro lado de la lamina.
    void Rows(UiDrawList& ui, const DirectX::XMMATRIX& vp, int f, const std::vector<Row>& rows, float a) {
        const int ls = std::max(2, kit::TextScale() - 1);
        const P2 e = EntrySide(f);
        const bool railRight = e.u > 0.5f; // izquierda: el riel queda a la derecha
        const size_t first = static_cast<size_t>(PageStart(f));
        const size_t last = std::min(rows.size(), first + (f == Left ? kRowsPerPage : rows.size()));
        for (size_t i = first; i < last; ++i) {
            const P2 mk = Marker(f, static_cast<int>(i));
            const bool sel = static_cast<int>(i) == m_row;
            const UiColor lc = !rows[i].enabled ? kit::Faint(a) : sel ? kit::White(a) : kit::Dim(a);
            const UiColor vc = sel ? kit::Beam(a) : kit::Dim(a);
            float lx, ly, vx, vy, mx, my;
            At(vp, f, mk.u, mk.v, mx, my);
            const float th = kit::TextH(ls);
            if (railRight) {
                // La etiqueta pegada al borde de la izquierda, el estado junto al riel.
                At(vp, f, -kEdge + 0.3f, mk.v, lx, ly);
                At(vp, f, mk.u - 0.7f, mk.v, vx, vy);
                ui.AddText(std::floor(lx), std::floor(ly - th * 0.5f), rows[i].label, lc, ls);
                ui.AddText(std::floor(vx - kit::TextW(rows[i].value, ls)), std::floor(vy - th * 0.5f), rows[i].value, vc, ls);
                m_rects.push_back(UiRect{std::floor(lx), std::floor(my - th), std::floor(mx - lx) + th, th * 2.0f});
            } else {
                At(vp, f, mk.u + 0.7f, mk.v, lx, ly);
                At(vp, f, kEdge - 0.3f, mk.v, vx, vy);
                ui.AddText(std::floor(lx), std::floor(ly - th * 0.5f), rows[i].label, lc, ls);
                ui.AddText(std::floor(vx - kit::TextW(rows[i].value, ls)), std::floor(vy - th * 0.5f), rows[i].value, vc, ls);
                m_rects.push_back(UiRect{std::floor(mx - th), std::floor(my - th), std::floor(vx - mx) + th, th * 2.0f});
            }
        }
    }

    // GRAV_MENU_AUTO (ver MenuKit.h): despierta, arma la caja, va a la cara
    // (0 piso, 1 izq., 2 der., 3 techo, 4 desarmar) y despues la fila AUTO2.
    void AutoDemo(kit::Nav& nav) {
        const int pick = kit::AutoPick();
        if (pick < 0) return;
        if (m_autoStage == 0 && m_phase == Phase::Point && m_time > 0.6f) {
            m_autoStage = 1;
            nav.ok = true;
        } else if (m_autoStage == 1 && m_phase == Phase::Idle && m_face == Back && m_arrive > 0.8f) {
            m_autoStage = 2;
            if (pick == 0) nav.down = true;
            if (pick == 1) nav.left = true;
            if (pick == 2) nav.right = true;
            if (pick == 3) nav.up = true;
            if (pick == 4) nav.back = true;
        } else if (m_autoStage == 2 && m_phase == Phase::Idle && m_face != Back && m_arrive > 1.0f) {
            m_autoStage = 3;
            const int row = kit::AutoPick("GRAV_MENU_AUTO2");
            if (row >= 0) {
                m_row = row;
                nav.ok = true;
            }
        }
    }

    GameObject* m_sheets[kFaceCount] = {};
    SheetLook m_look[kFaceCount], m_target[kFaceCount];
    std::vector<Seg> m_segs[kFaceCount];
    P2 m_sel[kFaceCount];
    float m_selA[kFaceCount] = {};

    Phase m_phase = Phase::Point;
    float m_fold = 0.0f, m_fade = 1.0f, m_titleChars = 0.0f;
    bool m_autoRise = false, m_quitAsk = false;

    int m_face = Back, m_from = Back, m_dest = Back, m_hubSel = 0, m_row = 0, m_level = 0, m_previewFrom = -1;
    bool m_toHub = false;
    float m_beam = 0.0f, m_travelLen = 0.0f, m_rowReveal = 100.0f, m_preview = 0.0f, m_arrive = 0.0f;
    std::vector<RouteSeg> m_travelRoute;
    Pose m_camFrom;

    std::vector<UiRect> m_rects;
    menu::OptionsState m_options;
    float m_time = 0.0f, m_phaseTime = 0.0f, m_dt = 0.0f, m_toast = 0.0f;
    std::string m_toastText;
    int m_autoStage = 0;
};

STRACE_REGISTER_SCRIPT(MainMenu)
