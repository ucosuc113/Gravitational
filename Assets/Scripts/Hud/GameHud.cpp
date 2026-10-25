// EL HUD DE LA PARTIDA: el nivel, el estado de la luz, el inventario, las
// herramientas y los controles de la luz. Va en el vacio "Puzzle".
//
// La FORMA del HUD es de la escena: el arbol "HUD" de la Jerarquia (UiCanvas
// de 720, etiquetas, paneles y botones; ver docs/hud-jerarquia.md). Mover un
// boton, cambiar un color o el tamano de un texto se hace en el Inspector, sin
// recompilar, y el panel Juego lo muestra tambien fuera de Play. Este script es
// el PUENTE: escribe los textos, prende y apaga lo que depende del estado, lee
// los clics de los botones y pinta el boton elegido.
//
// Lo unico que sigue dibujado desde aca son las CASILLAS DE PIEZAS: una por
// pieza que el nivel da, con su dibujo, su cantidad y su tecla, y cambian de
// numero con cada nivel. Como nodos habria que crearlas y destruirlas al cargar
// cada nivel; con la UI inmediata son un recuadro, un dibujo de lineas y dos
// textos. Van en el hueco "HUD/Barra/Casillas", al que este script le pone el
// ancho segun cuantas haya: asi la barra sigue centrada con 1 o con 5 tipos. La
// UI inmediata se dibuja ENCIMA de la de la escena, asi que las casillas quedan
// sobre el velo de la barra.
//
// Antes (hasta el 25/10/2026) todo el HUD era UI inmediata, y antes de eso una
// botonera fija de la escena (ESFERA, CUBO, ROTAR, QUITAR) que con doce piezas
// no alcanzaba. El reparto de ahora toma lo bueno de cada uno.
//
// El fundido de entrada y salida: los componentes no tienen alfa de grupo, asi
// que al empezar se guarda el color de cada componente del arbol tal como lo
// dejo el Inspector, y cada frame se escribe ese color por el alfa del HUD (mas
// lo que cambie el estado: el boton elegido, el color del estado de la luz).
//
// Todo tiene tecla, y la tecla esta escrita en el boton:
//   1..9 las piezas, R rotar, X quitar, Z deshacer, ESPACIO pausa, F paso,
//   F5 reiniciar, ESC menu.

#include "../Audio/GameAudio.h"
#include "../Puzzle/PuzzleSession.h"
#include "../Flow/GameFlow.h"
#include "../Flow/MenuKit.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Input/InputService.h"
#include "Engine/Scene/ComponentQuery.h"
#include "Engine/Scene/SceneQuery.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Text.h"
#include "Engine/UI/SceneUi.h"
#include "Engine/UI/UiService.h"

#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using namespace strace;
using namespace gravitational;

namespace {

// Lo que hace cada pieza, en una linea (la fuente es ASCII: sin tildes).
const char* RuleOf(Shape s) {
    switch (s) {
        case Shape::Sphere: return "LIGHT GOES TO THE FIRST THING IT TOUCHES: AHEAD, SIDES, ACROSS OR BEHIND";
        case Shape::Cube: return "CONDUCTS ALONG ITS BAND, AND TURNS LIGHT TRAVELLING ALONG ITS AXIS THE WAY IT SPINS";
        case Shape::Sphere3:
        case Shape::Sphere5: return "LIKE THE SPHERE, BUT IT ONLY TOUCHES AT THE CENTER OF EACH FACE";
        case Shape::Torus: return "CONDUCTS AROUND ITS RING. ALONG ITS AXIS, LIGHT GOES STRAIGHT THROUGH";
        case Shape::Prism: return "JOINS ITS TWO FLAT FACES: SHORT AROUND THE CORNER, LONG ALONG THE SLOPE";
        case Shape::Pyramid: return "JOINS THE BASE TO THE TIP, AND AT THE TIP TURNS THE LIGHT 90 DEGREES";
        case Shape::Ramp: return "FROM FLOOR TO WALL: LIGHT CLIMBS. IT ALSO WORKS BACKWARDS";
        case Shape::Twist: return "THE RIBBON TWISTS: LIGHT KEEPS GOING STRAIGHT AND ENDS ON THE WALL";
        case Shape::Bend: return "FROM FLOOR TO WALL, TURNING LEFT";
        case Shape::Wormhole: return "PITS ON THE CYAN PLANE: LIGHT FALLS INTO ONE AND COMES OUT OF THE OTHER";
        case Shape::BlackHole: return "LIGHT THAT TOUCHES IT NEVER COMES OUT";
        case Shape::WhiteHole: return "SENDS LIGHT BACK THE WAY IT CAME";
        default: return "";
    }
}

// --- Los dibujos de las piezas: lineas dentro de un cuadrado de lado `r*2` ---

void Poly(UiDrawList& ui, const std::vector<float>& xy, float cx, float cy, float r, const UiColor& c, float th, bool closed = false) {
    const size_t n = xy.size() / 2;
    for (size_t i = 0; i + (closed ? 0 : 1) < n; ++i) {
        const size_t j = (i + 1) % n;
        ui.AddLine(cx + xy[i * 2] * r, cy + xy[i * 2 + 1] * r, cx + xy[j * 2] * r, cy + xy[j * 2 + 1] * r, c, th);
    }
}
std::vector<float> Ellipse(float rx, float ry, float ox = 0, float oy = 0, float a0 = 0, float a1 = 6.2831853f, int n = 20) {
    std::vector<float> out;
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + (a1 - a0) * i / n;
        out.push_back(ox + std::cos(a) * rx);
        out.push_back(oy + std::sin(a) * ry);
    }
    return out;
}

void Icon(UiDrawList& ui, Shape s, float cx, float cy, float r, const UiColor& c, float th) {
    switch (s) {
        case Shape::Sphere:
        case Shape::Sphere3:
        case Shape::Sphere5: {
            const float k = s == Shape::Sphere ? 0.62f : s == Shape::Sphere3 ? 0.8f : 0.95f;
            Poly(ui, Ellipse(k, k), cx, cy, r, c, th);
            Poly(ui, Ellipse(k, k * 0.32f), cx, cy, r, kit::Mix(c, kit::Black(), 0.45f), th);
            break;
        }
        case Shape::Cube:
            Poly(ui, {-0.6f, -0.6f, 0.6f, -0.6f, 0.6f, 0.6f, -0.6f, 0.6f}, cx, cy, r, c, th, true);
            ui.AddRect(UiRect{cx - 0.6f * r, cy - 0.1f * r, 1.2f * r, 0.2f * r}, kit::Mix(c, kit::Black(), 0.55f));
            break;
        case Shape::Torus:
            Poly(ui, Ellipse(0.85f, 0.45f), cx, cy, r, c, th);
            Poly(ui, Ellipse(0.32f, 0.14f), cx, cy, r, c, th);
            break;
        case Shape::Prism: Poly(ui, {-0.6f, -0.6f, -0.6f, 0.6f, 0.6f, 0.6f}, cx, cy, r, c, th, true); break;
        case Shape::Pyramid:
            Poly(ui, {0.0f, -0.7f, 0.7f, 0.55f, -0.7f, 0.55f}, cx, cy, r, c, th, true);
            ui.AddLine(cx, cy - 0.7f * r, cx + 0.15f * r, cy + 0.55f * r, kit::Mix(c, kit::Black(), 0.45f), th);
            break;
        case Shape::Ramp:
            Poly(ui, Ellipse(1.2f, 1.2f, -0.6f, -0.6f, 1.5707963f, 0.0f, 12), cx, cy, r, c, th);
            ui.AddLine(cx - 0.8f * r, cy + 0.6f * r, cx - 0.6f * r, cy + 0.6f * r, c, th);
            break;
        case Shape::Twist:
            Poly(ui, {-0.8f, 0.5f, -0.2f, 0.5f, 0.3f, 0.1f, 0.5f, -0.6f}, cx, cy, r, c, th);
            Poly(ui, {-0.8f, 0.2f, -0.2f, 0.2f, 0.1f, -0.1f, 0.2f, -0.6f}, cx, cy, r, kit::Mix(c, kit::Black(), 0.45f), th);
            break;
        case Shape::Bend:
            Poly(ui, Ellipse(1.1f, 1.1f, -0.5f, 0.6f, -1.5707963f, 0.0f, 12), cx, cy, r, c, th);
            ui.AddLine(cx + 0.6f * r, cy + 0.6f * r, cx + 0.4f * r, cy + 0.35f * r, c, th);
            ui.AddLine(cx + 0.6f * r, cy + 0.6f * r, cx + 0.8f * r, cy + 0.35f * r, c, th);
            break;
        case Shape::Wormhole:
            Poly(ui, Ellipse(0.32f, 0.12f, -0.45f, -0.45f), cx, cy, r, c, th);
            Poly(ui, Ellipse(0.32f, 0.12f, 0.45f, 0.45f), cx, cy, r, c, th);
            Poly(ui, {-0.45f, -0.33f, -0.2f, 0.2f, 0.2f, -0.2f, 0.45f, 0.33f}, cx, cy, r, kit::Mix(c, kit::Black(), 0.4f), th);
            break;
        case Shape::BlackHole:
            Poly(ui, Ellipse(0.7f, 0.7f), cx, cy, r, c, th);
            Poly(ui, Ellipse(0.22f, 0.22f), cx, cy, r, kit::Red(c.a), th);
            break;
        case Shape::WhiteHole:
            Poly(ui, Ellipse(0.7f, 0.7f), cx, cy, r, c, th);
            ui.AddRect(UiRect{cx - 0.12f * r, cy - 0.12f * r, 0.24f * r, 0.24f * r}, c);
            break;
        default: break;
    }
}

std::string Clock(float seconds) {
    const int s = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

UiColor WithAlpha(UiColor c, float a) {
    c.a *= a;
    return c;
}

// Las medidas de la barra, en unidades del canvas (pixeles de 720p). Las de los
// botones son las de la escena; estas son las que el puente necesita para
// colocar lo que depende de cuantas casillas hay.
constexpr float kSlot = 60.0f;   // una casilla de pieza (y la altura de los botones)
constexpr float kGap = 8.0f;     // entre casillas y entre botones
constexpr float kSep = 22.0f;    // entre grupos (con un separador en el medio)
constexpr float kPadX = 14.0f;   // margen de la barra a los costados
constexpr float kRefHeight = 720.0f;

} // namespace

class GameHud : public IScript {
public:
    void OnStart() override {
        InputActions& in = InputService::Actions();
        auto bind = [&](const char* name, std::initializer_list<Key> keys) {
            in.ClearAction(name);
            for (Key k : keys) in.BindKey(name, k);
        };
        const Key digits[9] = {Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5, Key::Digit6, Key::Digit7, Key::Digit8, Key::Digit9};
        for (int i = 0; i < 9; ++i) bind(SlotAction(i), {digits[i]});
        bind("hud_rotate", {Key::R});
        bind("hud_remove", {Key::X, Key::Delete});
        bind("hud_undo", {Key::Z});
        bind("hud_pause", {Key::Space});
        bind("hud_step", {Key::F});
        bind("hud_reset", {Key::F5});
        in.ClearAction("hud_click");
        in.BindMouseButton("hud_click", MouseButton::Left);
        Resolve();
    }

    void OnUpdate(float dt) override {
        PuzzleSession& s = Session();
        GameFlow& flow = Flow();
        s.hudRects.clear();
        s.noticeTime += dt;
        if (!m.root && !Resolve()) return;

        // GRAV_NOHUD=1: sin HUD, para capturar el tablero en una corrida
        // automatica (a 320x180 los carteles tapan media imagen).
        static const bool noHud = kit::AutoPick("GRAV_NOHUD") == 1;
        const bool visible = flow.InGame() && !flow.passage.Active() && s.loaded && !noHud;
        m_alpha = kit::Approach(m_alpha, visible ? 1.0f : 0.0f, visible ? 6.0f : 14.0f, dt);
        m.root->active = m_alpha >= 0.01f;
        if (!m.root->active) return;
        const float a = m_alpha;
        ApplyAlpha(a);

        InputActions& in = InputService::Actions();
        const float mx = in.CursorX(), my = in.CursorY();
        const bool click = visible && in.CursorInside() && in.Pressed("hud_click");
        const float W = UiService::Width(), H = UiService::Height();
        const float u = H / kRefHeight; // la escala del canvas (SceneUi: alto / referencia)

        // ---------- Arriba a la izquierda: el nivel ----------
        char head[32];
        std::snprintf(head, sizeof(head), "LEVEL %02d / %02d", s.levelIndex + 1, s.levelCount);
        Text(m.header, head);
        Text(m.title, s.board.title.empty() ? std::string("UNTITLED") : s.board.title);
        Text(m.stats, "MOVES " + std::to_string(s.moves) + "     " + Clock(s.elapsed));
        Text(m.hint, s.board.hint);
        Show(m.hint, !s.board.hint.empty());
        Show(m.menu, flow.hasMenu);

        // ---------- Arriba al centro: que hizo la luz ----------
        UpdateStatus(s, a);

        // ---------- Abajo: inventario | herramientas | luz ----------
        std::vector<Shape> shapes;
        int placedByPlayer[kShapeCount] = {};
        for (const Figure& f : s.board.figures)
            if (!f.fixed) ++placedByPlayer[static_cast<int>(f.shape)];
        for (int i = 0; i < kShapeCount; ++i)
            if (s.board.inventory[i] + placedByPlayer[i] > 0) shapes.push_back(static_cast<Shape>(i));
        Layout(shapes.size());

        std::string describe;
        const UiRect bar = RectOf(m.bar, W, H);
        const UiRect cells = RectOf(m.cells, W, H);
        s.hudRects.push_back({bar.x, bar.y, bar.width, bar.height});
        DrawSlots(s, shapes, cells, u, mx, my, click, a, describe);

        // Las herramientas.
        if (Pressed(m.rotate, "hud_rotate")) {
            if (s.tool != Tool::Rotate) gameaudio::Play("Game/piece_pick");
            s.tool = Tool::Rotate;
        }
        if (Pressed(m.remove, "hud_remove")) {
            if (s.tool != Tool::Remove) gameaudio::Play("Game/piece_pick");
            s.tool = Tool::Remove;
        }

        // La luz y la partida.
        if (Pressed(m.undo, "hud_undo")) {
            if (s.Undo()) gameaudio::Play("Game/undo");
            else {
                s.Notice(s.solved ? "ALREADY SOLVED" : "NOTHING TO UNDO");
                gameaudio::Play("Ui/ui_denied");
            }
        }
        if (Pressed(m.pause, "hud_pause")) s.paused = !s.paused;
        if (Pressed(m.step, "hud_step")) {
            // Un tramo entero: hasta el final de la proxima superficie.
            gameaudio::SuppressPauseSound() = true;
            gameaudio::Play("Game/step");
            s.paused = true;
            for (float end : s.beam.hopEnd)
                if (end > s.progress + 1e-3f) {
                    s.progress = end;
                    break;
                }
        }
        if (Pressed(m.reset, "hud_reset")) {
            s.resetRequested = true;
            gameaudio::Play("Game/restart");
        }
        Text(m.pauseText, s.paused ? "RESUME" : "PAUSE");

        // El estado de cada boton: elegido (borde de la luz) o con el cursor
        // encima (borde y texto mas claros). Despues de los clics, para que el
        // boton recien apretado ya salga elegido este frame.
        PaintButton(m.rotate, m.rotateText, s.tool == Tool::Rotate, a);
        PaintButton(m.remove, m.removeText, s.tool == Tool::Remove, a);
        PaintButton(m.undo, m.undoText, false, a);
        PaintButton(m.pause, m.pauseText, s.paused, a);
        PaintButton(m.step, m.stepText, false, a);
        PaintButton(m.reset, m.resetText, false, a);

        if (Hovered(m.rotate)) describe = "ROTATE  -  CLICK ONE OF YOUR PIECES: IT MOVES TO ITS NEXT POSE";
        if (Hovered(m.remove)) describe = "REMOVE  -  CLICK ONE OF YOUR PIECES: IT GOES BACK TO THE INVENTORY";
        if (Hovered(m.step)) describe = "STEP  -  PAUSE AND ADVANCE THE LIGHT ONE SURFACE";

        // La linea de arriba de la barra: lo que se esta por hacer, o lo que
        // hay bajo el cursor. Es como se aprende una pieza nueva sin manual.
        if (describe.empty() && s.hoverFigure >= 0 && s.hoverFigure < static_cast<int>(s.board.figures.size())) {
            const Figure& f = s.board.figures[static_cast<size_t>(s.hoverFigure)];
            describe = std::string(Info(f.shape).label) + (f.fixed ? " (FIXED)" : "") + "  -  " + RuleOf(f.shape);
        }
        if (describe.empty()) {
            if (s.tool == Tool::Place) describe = std::string(Info(s.shape).label) + "  -  " + RuleOf(s.shape);
            else if (s.tool == Tool::Rotate) describe = "ROTATE  -  CLICK ONE OF YOUR PIECES: IT MOVES TO ITS NEXT POSE";
            else describe = "REMOVE  -  CLICK ONE OF YOUR PIECES: IT GOES BACK TO THE INVENTORY";
        }
        Text(m.describe, describe);

        // El aviso: arriba de la descripcion, en rojo, y se va solo. El ancho
        // es el del texto, asi que lo pone el puente.
        const bool notice = !s.notice.empty() && s.noticeTime < 2.2f;
        Show(m.notice, notice);
        if (notice) {
            const float na = a * kit::Saturate((2.2f - s.noticeTime) / 0.4f) * kit::Saturate(s.noticeTime / 0.08f);
            Text(m.noticeText, s.notice);
            if (UiTransform* t = TransformOf(m.notice))
                t->width = static_cast<float>(MeasureTextWidth(s.notice, 2)) + 32.0f;
            if (UiPanel* p = GetComponent<UiPanel>(*m.notice)) {
                p->color = WithAlpha(Base(&p->color), na);
                p->borderColor = WithAlpha(Base(&p->borderColor), na);
            }
            if (UiLabel* l = LabelOf(m.noticeText)) l->color = WithAlpha(Base(&l->color), na);
        }
    }

private:
    // Los nodos del arbol HUD, por ruta (docs/hud-jerarquia.md). Por ruta y no
    // por ObjectField: son veinte, y la ruta escrita una vez aca es mas facil de
    // mantener que veinte ranuras del Inspector que hay que volver a arrastrar
    // si alguien rehace el arbol.
    struct Nodes {
        GameObject* root = nullptr;
        GameObject *header = nullptr, *title = nullptr, *stats = nullptr, *hint = nullptr;
        GameObject *menu = nullptr;
        GameObject *status = nullptr, *statusDetail = nullptr;
        GameObject *describe = nullptr, *notice = nullptr, *noticeText = nullptr;
        GameObject *bar = nullptr, *cells = nullptr, *sep1 = nullptr, *sep2 = nullptr, *tools = nullptr, *controls = nullptr;
        GameObject *rotate = nullptr, *remove = nullptr, *undo = nullptr, *pause = nullptr, *step = nullptr, *reset = nullptr;
        GameObject *rotateText = nullptr, *removeText = nullptr, *undoText = nullptr, *pauseText = nullptr, *stepText = nullptr,
                   *resetText = nullptr;
    } m;

    bool Resolve() {
        auto& roots = SceneService::MutableRoots();
        auto find = [&](const char* path) {
            GameObject* n = FindByPath(roots, path);
            if (!n && !m_warned) Log::Warn(L"GameHud: falta '" + Utf8ToWide(path) + L"' en la escena (ver docs/hud-jerarquia.md)");
            return n;
        };
        m = Nodes{};
        m.root = find("HUD");
        if (!m.root) {
            m_warned = true;
            return false;
        }
        m.header = find("HUD/Nivel/Cabecera");
        m.title = find("HUD/Nivel/Titulo");
        m.stats = find("HUD/Nivel/Jugadas");
        m.hint = find("HUD/Nivel/Pista");
        m.menu = find("HUD/Esquina/Menu");
        m.status = find("HUD/Estado/Texto");
        m.statusDetail = find("HUD/Estado/Detalle");
        m.describe = find("HUD/Descripcion");
        m.notice = find("HUD/Aviso");
        m.noticeText = find("HUD/Aviso/Texto");
        m.bar = find("HUD/Barra");
        m.cells = find("HUD/Barra/Casillas");
        m.sep1 = find("HUD/Barra/Separador1");
        m.sep2 = find("HUD/Barra/Separador2");
        m.tools = find("HUD/Barra/Herramientas");
        m.controls = find("HUD/Barra/Controles");
        m.rotate = find("HUD/Barra/Herramientas/Rotar");
        m.remove = find("HUD/Barra/Herramientas/Quitar");
        m.undo = find("HUD/Barra/Controles/Deshacer");
        m.pause = find("HUD/Barra/Controles/Pausa");
        m.step = find("HUD/Barra/Controles/Paso");
        m.reset = find("HUD/Barra/Controles/Reiniciar");
        m.rotateText = find("HUD/Barra/Herramientas/Rotar/Texto");
        m.removeText = find("HUD/Barra/Herramientas/Quitar/Texto");
        m.undoText = find("HUD/Barra/Controles/Deshacer/Texto");
        m.pauseText = find("HUD/Barra/Controles/Pausa/Texto");
        m.stepText = find("HUD/Barra/Controles/Paso/Texto");
        m.resetText = find("HUD/Barra/Controles/Reiniciar/Texto");
        m_warned = true;
        m_base.clear();
        Snapshot(*m.root);
        return true;
    }

    // --- El alfa del HUD ---

    // El color de cada componente tal como lo dejo el Inspector (la escena de
    // Play es una copia: estos punteros viven lo que vive el Play).
    void Snapshot(GameObject& n) {
        if (UiPanel* p = GetComponent<UiPanel>(n)) {
            m_base[&p->color] = p->color;
            m_base[&p->borderColor] = p->borderColor;
        }
        if (UiLabel* l = GetComponent<UiLabel>(n)) m_base[&l->color] = l->color;
        if (UiButton* b = GetComponent<UiButton>(n)) {
            for (UiColor* c : {&b->color, &b->hoverColor, &b->pressedColor, &b->disabledColor, &b->borderColor}) m_base[c] = *c;
        }
        for (auto& child : n.children) Snapshot(*child);
    }
    UiColor Base(UiColor* c) const {
        const auto it = m_base.find(c);
        return it != m_base.end() ? it->second : *c;
    }
    void ApplyAlpha(float a) {
        for (auto& [ptr, base] : m_base) *ptr = WithAlpha(base, a);
    }

    // --- Comodidades: todas aceptan un nodo que falta (ya se aviso al buscarlo) ---

    static UiLabel* LabelOf(GameObject* n) { return n ? GetComponent<UiLabel>(*n) : nullptr; }
    static UiTransform* TransformOf(GameObject* n) { return n ? GetComponent<UiTransform>(*n) : nullptr; }
    static void Text(GameObject* n, const std::string& text) {
        if (n) SceneUi::SetText(*n, text);
    }
    static void Show(GameObject* n, bool on) {
        if (n) n->active = on;
    }
    static bool Hovered(GameObject* n) {
        const UiButton* b = n ? GetComponent<UiButton>(*n) : nullptr;
        return b && b->hovered;
    }
    // El boton se apreto (clic completo, ver UiButton) o su tecla.
    static bool Pressed(GameObject* n, const char* action) {
        return (n && SceneUi::Clicked(*n)) || InputService::Actions().Pressed(action);
    }

    // Elegido: fondo con un toque de la luz y borde de la luz, texto blanco.
    // Con el cursor encima: borde y texto mas claros. Si no, lo de la escena.
    void PaintButton(GameObject* n, GameObject* text, bool on, float a) {
        UiButton* b = n ? GetComponent<UiButton>(*n) : nullptr;
        if (!b) return;
        UiLabel* l = LabelOf(text);
        if (on) {
            const UiColor fill = kit::Mix(kit::Black(a), kit::Beam(a), 0.12f);
            b->color = b->hoverColor = fill;
            b->borderColor = kit::Beam(a);
            b->borderThickness = 2.0f;
            if (l) l->color = kit::White(a);
            return;
        }
        b->borderThickness = 1.0f;
        if (b->hovered) {
            b->borderColor = kit::Dim(a);
            if (l) l->color = kit::White(a);
        }
    }

    // El rectangulo en pantalla de un nodo del HUD: la misma cuenta que SceneUi
    // (LayoutUiRect de la raiz para abajo, con la escala del canvas), asi las
    // casillas caen exactamente en el hueco que la barra les deja.
    UiRect RectOf(GameObject* n, float W, float H) const {
        std::vector<GameObject*> chain;
        for (GameObject* p = n; p; p = p->parent) chain.push_back(p);
        UiRect rect{0.0f, 0.0f, W, H};
        float scale = 1.0f;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (const UiCanvas* c = GetComponent<UiCanvas>(**it)) scale = c->referenceHeight > 0.0f ? H / c->referenceHeight : 1.0f;
            if (const UiTransform* t = GetComponent<UiTransform>(**it)) rect = LayoutUiRect(rect, *t, scale);
        }
        return rect;
    }

    // Los anchos que dependen de cuantas piezas hay: el hueco de las casillas,
    // la barra entera y donde arrancan los grupos de botones. En unidades del
    // canvas; los anchos de los grupos los toma de la escena.
    void Layout(size_t count) {
        UiTransform* cells = TransformOf(m.cells);
        UiTransform* tools = TransformOf(m.tools);
        UiTransform* controls = TransformOf(m.controls);
        UiTransform* bar = TransformOf(m.bar);
        if (!cells || !tools || !controls || !bar) return;
        const float cellsW = count ? count * (kSlot + kGap) - kGap : 0.0f;
        cells->x = kPadX;
        cells->width = cellsW;
        tools->x = kPadX + cellsW + kSep;
        controls->x = tools->x + tools->width + kSep;
        if (UiTransform* s1 = TransformOf(m.sep1)) s1->x = std::floor(tools->x - kSep * 0.5f);
        if (UiTransform* s2 = TransformOf(m.sep2)) s2->x = std::floor(controls->x - kSep * 0.5f);
        bar->width = controls->x + controls->width + kPadX;
    }

    void UpdateStatus(const PuzzleSession& s, float a) {
        std::string text;
        UiColor color = kit::Dim(a);
        if (s.solved) {
            text = "SOLVED";
            color = kit::Beam(a);
        } else if (s.FullyRevealed()) {
            switch (s.trace.end) {
                case TraceEnd::Lost: text = "THE LIGHT IS LOST IN THE VOID"; break;
                case TraceEnd::Blocked: text = "THE LIGHT STOPS"; break;
                case TraceEnd::Orbit: text = "THE LIGHT ORBITS WITH NO WAY OUT"; break;
                case TraceEnd::Loop: text = "THE LIGHT GOES IN CIRCLES"; break;
                case TraceEnd::Absorbed:
                    text = "THE LIGHT FALLS INTO A BLACK HOLE";
                    color = kit::Red(a);
                    break;
                default: break;
            }
        } else if (s.paused) {
            text = "PAUSED";
        }
        Show(m.status, !text.empty());
        Text(m.status, text);
        if (UiLabel* l = LabelOf(m.status)) {
            l->color = color;
            // Resuelto, un escalon mas grande: es el final de la partida.
            l->scale = s.solved ? 4 : 2;
        }
        Show(m.statusDetail, s.solved);
        if (s.solved)
            Text(m.statusDetail, std::to_string(s.moves) + (s.moves == 1 ? " MOVE" : " MOVES") + "  -  " + Clock(s.elapsed));
    }

    // Las casillas de piezas, dibujadas en el hueco de la barra. `u` es la
    // escala del canvas: las medidas son las de 720p, como el resto del HUD.
    void DrawSlots(PuzzleSession& s, const std::vector<Shape>& shapes, const UiRect& cells, float u, float mx, float my, bool click, float a,
                   std::string& describe) {
        UiDrawList& ui = UiService::Frame();
        InputActions& in = InputService::Actions();
        // Los textos con la misma regla que las etiquetas del canvas (SceneUi):
        // escala de diseno por la del canvas, redondeada.
        const int ss = std::max(1, static_cast<int>(std::lround(2.0f * u)));
        const int xs = std::max(1, static_cast<int>(std::lround(1.0f * u)));
        const float slot = std::floor(kSlot * u), step = (kSlot + kGap) * u;
        for (size_t i = 0; i < shapes.size(); ++i) {
            const Shape sh = shapes[i];
            const int left = s.board.inventory[static_cast<int>(sh)];
            const bool sel = s.tool == Tool::Place && s.shape == sh;
            const UiRect r{std::floor(cells.x + i * step), std::floor(cells.y), slot, slot};
            const bool hover = kit::Inside(r, mx, my);
            if ((hover && click) || (i < 9 && in.Pressed(SlotAction(static_cast<int>(i))))) {
                if (!sel) gameaudio::Play("Game/piece_pick");
                s.tool = Tool::Place;
                s.shape = sh;
            }
            const float k = left > 0 ? 1.0f : 0.35f;
            ui.AddRect(r, sel ? kit::Mix(kit::Black(a), kit::Beam(a), 0.12f) : kit::Black(0.0f));
            ui.AddRectOutline(r, sel ? kit::Beam(a) : hover ? kit::Dim(a) : kit::Faint(a), sel ? 2.0f : 1.0f);
            Icon(ui, sh, r.x + slot * 0.5f, r.y + slot * 0.42f, slot * 0.30f, sel ? kit::White(a * k) : kit::Dim(a * k), std::max(1.0f, std::floor(1.5f * u)));
            if (i < 9) ui.AddText(r.x + 4.0f * u, r.y + 4.0f * u, std::to_string(i + 1), kit::Faint(a), xs);
            const std::string count = "x" + std::to_string(left);
            ui.AddText(r.x + slot - kit::TextW(count, ss) - 4.0f * u, r.y + slot - kit::TextH(ss) - 4.0f * u, count,
                       left > 0 ? (sel ? kit::Beam(a) : kit::Dim(a)) : kit::Faint(a), ss);
            if (hover) describe = std::string(Info(sh).label) + "  -  " + RuleOf(sh);
            if (sel && sh == Shape::Wormhole && left % 2 == 1) describe = "WORMHOLE  -  PLACE THE OTHER PIT: ON A CYAN PLANE, WHERE THE LIGHT SHOULD EXIT";
        }
    }

    static const char* SlotAction(int i) {
        static const char* names[9] = {"hud_slot1", "hud_slot2", "hud_slot3", "hud_slot4", "hud_slot5",
                                       "hud_slot6", "hud_slot7", "hud_slot8", "hud_slot9"};
        return names[i];
    }

    std::unordered_map<UiColor*, UiColor> m_base;
    float m_alpha = 0.0f;
    bool m_warned = false;
};

STRACE_REGISTER_SCRIPT(GameHud)
