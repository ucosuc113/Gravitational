// COLOCAR, ROTAR Y QUITAR PIEZAS con el clic izquierdo, segun la herramienta
// elegida en el HUD. Va en la camara (es lo que el jugador hace desde donde
// mira; OrbitCamera, en el mismo objeto, se queda con el boton derecho).
//
// Donde va una pieza nueva se decide como en un juego de bloques: clic sobre
// la cara de una pieza pone la nueva en la celda pegada a esa cara; clic sobre
// un plano, en la celda pegada al plano. Y con QUE POSTURA lo decide el
// tablero (Board::PlacementsFor): la primera en la que la pieza nueva TOCA lo
// que se clico. Una rampa puesta en el piso nace con la entrada apoyada; una
// esfera de 3, corrida hacia adentro para que su punto de apoyo caiga ahi.
// ROTAR recorre las demas posturas que siguen tocando.
//
// Un FANTASMA con la forma de la pieza la muestra ahi antes del clic (en rojo
// si no entra): sin el, la celda de destino en una cuadricula invisible es
// adivinanza, y con piezas de varias celdas tambien lo es la postura.

#include "../Audio/GameAudio.h"
#include "../Puzzle/PuzzleSession.h"
#include "../Flow/GameFlow.h"
#include "PieceNode.h"
#include "Picking.h"
#include "PlaneNodes.h"
#include "PlayTouch.h"

#include "Engine/Input/InputService.h"
#include "Engine/Rendering/Camera.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Scene/Raycast.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/UI/SceneUi.h"
#include "Engine/UI/UiService.h"

#include <cmath>
#include <vector>

using namespace strace;
using namespace gravitational;

class FigureBuilder : public IScript {
public:
    ObjectField ghost{this, "ghost"};
    ObjectField figures{this, "figures"};
    ObjectField startPlane{this, "startPlane"};
    ObjectField endPlane{this, "endPlane"};

    void OnStart() override { InputService::Actions().BindMouseButton("construir", MouseButton::Left); }

    void OnUpdate(float /*dt*/) override {
        PuzzleSession& s = Session();
        GameObject* g = ghost.Get();
        if (g) g->active = false;
        s.hoverFigure = -1;
        // En un telefono el dedo es el cursor y el clic es un TOQUE (Flow/
        // Touch.h): se actua al levantarlo sin haber arrastrado, porque
        // arrastrar es girar la camara. Mientras esta apoyado y quieto, el
        // fantasma muestra donde caeria la pieza: apoyar, mirar, soltar. Con el
        // dedo arriba no hay cursor que seguir, asi que no hay fantasma.
        const TouchGesture* finger = TouchMode() ? &PlayTouch() : nullptr;
        const bool aiming = !finger || (finger->down && !finger->dragging && !finger->startedOnUi);
        const bool act = finger ? finger->tap && !finger->startedOnUi : InputService::Actions().Pressed("construir");
        if (!aiming && !act) return;
        // Con el menu delante el clic es del menu; durante el paso entre niveles, nada.
        if (!s.loaded || !Flow().InGame() || Flow().passage.Active()) return;

        InputActions& in = InputService::Actions();
        if (!in.CursorInside() || SceneUi::PointerOverUi() || s.PointerOverHud(in.CursorX(), in.CursorY())) return;

        // Todos los planos, los de portal incluidos (los crea el director por
        // nivel). Sin director todavia, los dos de la escena.
        std::vector<const GameObject*> planes(PlaneNodes().begin(), PlaneNodes().end());
        if (planes.empty()) planes = {startPlane.Get(), endPlane.Get()};
        Pick pick;
        if (!PickAt(s.board, *gameObject, figures.Get(), planes, in.CursorX(), in.CursorY(), UiService::Width(), UiService::Height(), pick))
            return;
        s.hoverFigure = pick.figure;

        const bool placing = s.tool == Tool::Place;
        std::vector<Figure> stances;
        if (placing && pick.canPlace) stances = s.board.PlacementsFor(s.shape, pick.cell, pick.normal);

        // El fantasma: la primera postura, o la pieza "como seria" en rojo si
        // no entra (para que se vea POR QUE no).
        if (placing && pick.canPlace && g && aiming) {
            Figure shown;
            const bool ok = !stances.empty();
            if (ok) shown = stances.front();
            else {
                shown.shape = s.shape;
                shown.cell = pick.cell;
            }
            const PieceLook look = LookOf(shown);
            ApplyLook(*g, look);
            const Vec3f c = FigurePos(s.board, shown);
            g->transform.position[0] = c.x;
            g->transform.position[1] = c.y;
            g->transform.position[2] = c.z;
            g->active = true;
            if (Material* m = SceneService::UniqueMaterial(*g)) m->userParams[0].x = ok && s.board.inventory[static_cast<int>(s.shape)] > 0 ? 0.0f : 1.0f;
        }

        if (!act) return;
        if (s.solved) return;
        const Board before = s.board;
        bool changed = false;
        if (placing) {
            if (!pick.canPlace) return;
            if (s.board.inventory[static_cast<int>(s.shape)] <= 0) {
                s.Notice(std::string("NONE LEFT: ") + Info(s.shape).label);
                gameaudio::Play("Game/piece_place_invalid");
                return;
            }
            if (stances.empty()) {
                // El gusano no es una pieza que se apoya: se hunde en un plano
                // de portal. Decir donde SI va es mas util que "no entra".
                s.Notice(s.shape == Shape::Wormhole ? "THE WORMHOLE SINKS INTO A PORTAL PLANE (THE CYAN ONE)" : "IT DOES NOT FIT THERE");
                gameaudio::Play("Game/piece_place_invalid");
                return;
            }
            changed = s.board.Place(stances.front());
            if (changed && s.shape == Shape::Wormhole) {
                const Figure& placed = s.board.figures.back();
                s.Notice(s.board.Twin(placed) ? "WORMHOLE CONNECTED" : "NOW THE OTHER PIT");
                gameaudio::PlayLater("Game/hud_notice", 0.12f);
            }
            if (changed) gameaudio::PlayPlace();
        } else if (pick.figure >= 0) {
            const Figure& f = s.board.figures[static_cast<size_t>(pick.figure)];
            const Int3 anchor = f.cell;
            if (f.fixed) {
                s.Notice("THAT PIECE BELONGS TO THE LEVEL: HANDS OFF");
                gameaudio::Play("Ui/ui_denied");
                return;
            }
            if (s.tool == Tool::Rotate) {
                // f es una referencia al tablero: se lee la forma ANTES de rotar.
                const bool turnless = IsSphere(f.shape) || f.shape == Shape::Wormhole;
                changed = s.board.Rotate(anchor);
                if (!changed) s.Notice(turnless ? "THAT PIECE DOES NOT ROTATE" : "NO OTHER POSE FITS");
                gameaudio::Play(changed ? "Game/piece_rotate" : "Ui/ui_denied");
            } else {
                changed = s.board.Remove(anchor);
                if (changed) gameaudio::Play("Game/piece_remove");
            }
        }
        if (changed) {
            // El tablero ya cambio: lo que se guarda para deshacer es el de antes.
            Board after = s.board;
            s.board = before;
            s.Remember();
            s.board = std::move(after);
            s.Commit();
        }
    }

};

STRACE_REGISTER_SCRIPT(FigureBuilder)
