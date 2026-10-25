// EL HUD DE LA PARTIDA: el nivel, el estado de la luz, el inventario, las
// herramientas y los controles de la luz. Va en el vacio "Puzzle".
//
// Era UI de la escena (Canvas, botones) leida por campos del director: una
// fila de cuatro botones fijos (ESFERA, CUBO, ROTAR, QUITAR) y la seleccion
// escrita como "> SPHERE <". Con doce piezas y un inventario distinto por
// nivel, una botonera fija no alcanza: este script la ARMA segun el nivel -
// una casilla por pieza que el nivel da, con su dibujo, su cantidad y su
// tecla - y la dibuja con la misma paleta y la misma fuente que el menu
// (Flow/MenuKit.h), asi el juego y el menu se leen como una sola cosa.
//
// Se dibuja con la UI inmediata (UiService::Frame) y no con nodos de la
// escena: los nodos tendrian que crearse y destruirse con cada nivel, y una
// casilla es un recuadro, un dibujo de lineas y dos textos - justo lo que la
// lista inmediata hace en una llamada. Los rectangulos que ocupa se publican
// en la sesion (hudRects): FigureBuilder no toma como jugada un clic sobre el HUD.
//
// Todo tiene tecla, y la tecla esta escrita en el boton:
//   1..9 las piezas, R rotar, X quitar, Z deshacer, ESPACIO pausa, F paso,
//   F5 reiniciar, ESC menu.

#include "../Audio/GameAudio.h"
#include "../Puzzle/PuzzleSession.h"
#include "../Flow/GameFlow.h"
#include "../Flow/MenuKit.h"

#include "Engine/Input/InputService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/UI/UiService.h"

#include <cmath>
#include <initializer_list>
#include <cstdio>
#include <string>
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
    }

    void OnUpdate(float dt) override {
        PuzzleSession& s = Session();
        GameFlow& flow = Flow();
        s.hudRects.clear();
        s.noticeTime += dt;
        // GRAV_NOHUD=1: sin HUD, para capturar el tablero en una corrida
        // automatica (a 320x180 los carteles tapan media imagen).
        static const bool noHud = kit::AutoPick("GRAV_NOHUD") == 1;
        const bool visible = flow.InGame() && !flow.passage.Active() && s.loaded && !noHud;
        m_alpha = kit::Approach(m_alpha, visible ? 1.0f : 0.0f, visible ? 6.0f : 14.0f, dt);
        if (m_alpha < 0.01f) return;
        const float a = m_alpha;

        InputActions& in = InputService::Actions();
        const float mx = in.CursorX(), my = in.CursorY();
        const bool click = visible && in.CursorInside() && in.Pressed("hud_click");

        UiDrawList& ui = UiService::Frame();
        const float W = UiService::Width(), H = UiService::Height();
        const float u = std::max(0.75f, H / 720.0f); // un "pixel de diseno" de 720p
        const int ts = kit::TextScale();             // 3 a 720p
        const int ss = std::max(1, ts - 1);          // 2: lo que se lee
        const int xs = std::max(1, ts - 2);          // 1: solo las teclas (5x7 px a 720p)

        // ---------- Arriba a la izquierda: el nivel ----------
        {
            float x = std::floor(28.0f * u), y = std::floor(24.0f * u);
            char head[32];
            std::snprintf(head, sizeof(head), "LEVEL %02d / %02d", s.levelIndex + 1, s.levelCount);
            ui.AddText(x, y, head, kit::Dim(a), ss);
            y += kit::TextH(ss) + 12.0f * u;
            const std::string title = s.board.title.empty() ? std::string("UNTITLED") : s.board.title;
            ui.AddText(x, y, title, kit::White(a), ts);
            y += kit::TextH(ts) + 12.0f * u;
            const std::string stats = "MOVES " + std::to_string(s.moves) + "     " + Clock(s.elapsed);
            ui.AddText(x, y, stats, kit::Faint(a), ss);
            if (!s.board.hint.empty()) {
                y += kit::TextH(ss) + 14.0f * u;
                ui.AddText(x, y, s.board.hint, kit::Faint(a), ss);
            }
        }
        if (flow.hasMenu) {
            const char* text = "ESC  MENU";
            ui.AddText(W - kit::TextW(text, ss) - 28.0f * u, 24.0f * u, text, kit::Faint(a), ss);
        }
        {
            // La camara, que no tiene boton: si no se dice, no se descubre.
            const char* cam = "RIGHT BUTTON  ORBIT     WHEEL OR Q E  ZOOM";
            ui.AddText(W - kit::TextW(cam, xs) - 28.0f * u, 24.0f * u + kit::TextH(ss) + 10.0f * u, cam, kit::Faint(a), xs);
        }

        // ---------- Arriba al centro: que hizo la luz ----------
        DrawStatus(ui, s, W, u, ts, ss, a);

        // ---------- Abajo: inventario | herramientas | luz ----------
        const float slot = std::floor(60.0f * u), gap = std::floor(8.0f * u), sep = std::floor(22.0f * u);
        // Los botones, del ancho de su texto mas largo: a 720p "RESTART" no
        // entraba en 104 px y se salia del recuadro.
        const float btnH = slot, btnW = std::floor(kit::TextW("RESTART", ss) + 22.0f * u);
        std::vector<Shape> shapes;
        int placedByPlayer[kShapeCount] = {};
        for (const Figure& f : s.board.figures)
            if (!f.fixed) ++placedByPlayer[static_cast<int>(f.shape)];
        for (int i = 0; i < kShapeCount; ++i)
            if (s.board.inventory[i] + placedByPlayer[i] > 0) shapes.push_back(static_cast<Shape>(i));

        const int tools = 2, controls = 4;
        const float total = shapes.size() * (slot + gap) - (shapes.empty() ? 0 : gap) + sep + tools * (btnW + gap) - gap + sep +
                            controls * (btnW + gap) - gap;
        float x = std::floor((W - total) * 0.5f);
        const float y = std::floor(H - slot - 26.0f * u);

        // El fondo de la barra: un velo apenas, para que las lineas del mundo
        // no crucen los dibujos de las casillas.
        const UiRect bar{x - 14.0f * u, y - 12.0f * u, total + 28.0f * u, slot + 24.0f * u};
        ui.AddRect(bar, kit::Black(0.72f * a));
        ui.AddLine(bar.x, bar.y, bar.x + bar.width, bar.y, kit::Faint(0.6f * a), 1.0f);
        s.hudRects.push_back({bar.x, bar.y, bar.width, bar.height});

        std::string describe;
        for (size_t i = 0; i < shapes.size(); ++i) {
            const Shape sh = shapes[i];
            const int left = s.board.inventory[static_cast<int>(sh)];
            const bool sel = s.tool == Tool::Place && s.shape == sh;
            const UiRect r{x, y, slot, slot};
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
            x += slot + gap;
        }
        x += sep - gap;
        Separator(ui, x - sep * 0.5f, y, slot, a);

        // Las herramientas.
        if (Button(ui, x, y, btnW, btnH, "ROTATE", "R", s.tool == Tool::Rotate, mx, my, click, a) || in.Pressed("hud_rotate")) {
            if (s.tool != Tool::Rotate) gameaudio::Play("Game/piece_pick");
            s.tool = Tool::Rotate;
        }
        if (kit::Inside(UiRect{x, y, btnW, btnH}, mx, my)) describe = "ROTATE  -  CLICK ONE OF YOUR PIECES: IT MOVES TO ITS NEXT POSE";
        x += btnW + gap;
        if (Button(ui, x, y, btnW, btnH, "REMOVE", "X", s.tool == Tool::Remove, mx, my, click, a) || in.Pressed("hud_remove")) {
            if (s.tool != Tool::Remove) gameaudio::Play("Game/piece_pick");
            s.tool = Tool::Remove;
        }
        if (kit::Inside(UiRect{x, y, btnW, btnH}, mx, my)) describe = "REMOVE  -  CLICK ONE OF YOUR PIECES: IT GOES BACK TO THE INVENTORY";
        x += btnW + gap + sep - gap;
        Separator(ui, x - sep * 0.5f, y, slot, a);

        // La luz y la partida.
        if (Button(ui, x, y, btnW, btnH, "UNDO", "Z", false, mx, my, click, a) || in.Pressed("hud_undo"))
        {
            if (s.Undo()) gameaudio::Play("Game/undo");
            else {
                s.Notice(s.solved ? "ALREADY SOLVED" : "NOTHING TO UNDO");
                gameaudio::Play("Ui/ui_denied");
            }
        }
        x += btnW + gap;
        if (Button(ui, x, y, btnW, btnH, s.paused ? "RESUME" : "PAUSE", "SPC", s.paused, mx, my, click, a) || in.Pressed("hud_pause"))
            s.paused = !s.paused;
        x += btnW + gap;
        if (Button(ui, x, y, btnW, btnH, "STEP", "F", false, mx, my, click, a) || in.Pressed("hud_step")) {
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
        if (kit::Inside(UiRect{x, y, btnW, btnH}, mx, my)) describe = "STEP  -  PAUSE AND ADVANCE THE LIGHT ONE SURFACE";
        x += btnW + gap;
        if (Button(ui, x, y, btnW, btnH, "RESTART", "F5", false, mx, my, click, a) || in.Pressed("hud_reset")) {
            s.resetRequested = true;
            gameaudio::Play("Game/restart");
        }

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
        kit::TextCentered(ui, W * 0.5f, bar.y - 16.0f * u - kit::TextH(ss) * 0.5f, describe, kit::Dim(a), ss);

        // El aviso: arriba de la descripcion, en rojo, y se va solo.
        if (!s.notice.empty() && s.noticeTime < 2.2f) {
            const float na = a * kit::Saturate((2.2f - s.noticeTime) / 0.4f) * kit::Saturate(s.noticeTime / 0.08f);
            const float w = kit::TextW(s.notice, ss) + 32.0f * u, h = kit::TextH(ss) + 18.0f * u;
            const UiRect r{std::floor((W - w) * 0.5f), std::floor(bar.y - 48.0f * u - h), w, h};
            ui.AddRect(r, kit::Black(0.85f * na));
            ui.AddRectOutline(r, kit::Red(na), 1.0f);
            ui.AddTextCentered(r, s.notice, kit::White(na), ss);
        }
    }

private:
    static const char* SlotAction(int i) {
        static const char* names[9] = {"hud_slot1", "hud_slot2", "hud_slot3", "hud_slot4", "hud_slot5",
                                       "hud_slot6", "hud_slot7", "hud_slot8", "hud_slot9"};
        return names[i];
    }

    static void Separator(UiDrawList& ui, float x, float y, float h, float a) {
        ui.AddLine(std::floor(x), y + h * 0.15f, std::floor(x), y + h * 0.85f, kit::Faint(0.7f * a), 1.0f);
    }

    // Un boton: el nombre al centro y la tecla, chica, arriba a la izquierda.
    // Devuelve si se hizo clic.
    static bool Button(UiDrawList& ui, float x, float y, float w, float h, const char* label, const char* key, bool on, float mx, float my, bool click,
                       float a) {
        const UiRect r{x, y, w, h};
        const bool hover = kit::Inside(r, mx, my);
        const int ts = std::max(1, kit::TextScale() - 1), ks = std::max(1, kit::TextScale() - 2);
        if (on) ui.AddRect(r, kit::Mix(kit::Black(a), kit::Beam(a), 0.12f));
        ui.AddRectOutline(r, on ? kit::Beam(a) : hover ? kit::Dim(a) : kit::Faint(a), on ? 2.0f : 1.0f);
        ui.AddTextCentered(UiRect{r.x, r.y + h * 0.12f, r.width, r.height}, label, on ? kit::White(a) : hover ? kit::White(a) : kit::Dim(a), ts);
        ui.AddText(r.x + 4.0f, r.y + 4.0f, key, kit::Faint(a), ks);
        return hover && click;
    }

    void DrawStatus(UiDrawList& ui, const PuzzleSession& s, float W, float u, int ts, int ss, float a) {
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
        if (text.empty()) return;
        const int scale = s.solved ? ts + 1 : ss;
        const float y = 28.0f * u + kit::TextH(scale) * 0.5f;
        kit::TextCentered(ui, W * 0.5f, y, text, color, scale);
        if (s.solved) {
            const std::string sub = std::to_string(s.moves) + (s.moves == 1 ? " MOVE" : " MOVES") + "  -  " + Clock(s.elapsed);
            kit::TextCentered(ui, W * 0.5f, y + kit::TextH(scale) * 0.5f + 10.0f * u + kit::TextH(ss) * 0.5f, sub, kit::Dim(a), ss);
        }
    }

    float m_alpha = 0.0f;
};

STRACE_REGISTER_SCRIPT(GameHud)
