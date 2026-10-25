// EL DIRECTOR DEL PUZZLE 3D: carga el nivel, acomoda la escena a su tamano,
// crea los objetos de las piezas y revela la luz. Va en un vacio "Puzzle".
//
// Todo lo que toca de la escena le llega por campos del Inspector (planos y el
// contenedor de piezas): la escena se rearma sin tocar este archivo, y el log
// dice que campo falta.
//
// No lee el raton sobre el mundo (FigureBuilder), no dibuja la luz (la luz, la
// fuente y el receptor son dibujos SOBRE las superficies: SurfaceLight +
// Surface.hlsl / Figure.hlsl / Piece.hlsl) y no dibuja el HUD (GameHud). Hasta
// el 21/10/2026 el HUD era suyo: botones de la escena leidos por campos, con
// una fila por herramienta. Con doce piezas y un inventario distinto por
// nivel, el HUD paso a ser un script propio que lo arma segun el nivel.
//
// Con un menu en la escena (ver Flow/GameFlow.h), el director solo juega en
// modo Game: en modo Menu esconde su mundo y no avanza la luz. El menu le pide
// el nivel (pendingLevel), y el director le devuelve el control con Esc o al
// resolver el ultimo. Sin menu, nada de esto se nota.

#include "PieceLook.h"
#include "../Audio/GameAudio.h"
#include "../Pieces/PieceNode.h"
#include "../Pieces/PlaneNodes.h"
#include "PuzzleSession.h"
#include "../Flow/GameFlow.h"
#include "../Flow/GameHost.h"
#include "../Flow/MenuKit.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Input/InputService.h"
#include "Engine/IO/FileSystem.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Text.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace strace;
using namespace gravitational;

class PuzzleDirector : public IScript {
public:
    // Los niveles en orden, separados por ';'. Un campo de texto y no un
    // archivo de lista: para probar un nivel suelto alcanza con escribir solo
    // ese. Los tres primeros son los del principio; del 4 en adelante, los de
    // las piezas nuevas (docs/niveles.md).
    TextField levels{this, "levels",
                     "Assets/Levels/tutorial-1.txt;Assets/Levels/nivel-2.txt;Assets/Levels/tutorial-2.txt;"
                     "Assets/Levels/n04.txt;Assets/Levels/n05.txt;Assets/Levels/n06.txt;Assets/Levels/n07.txt;"
                     "Assets/Levels/n08.txt;Assets/Levels/n09.txt;Assets/Levels/n10.txt;Assets/Levels/n11.txt;"
                     "Assets/Levels/n12.txt;Assets/Levels/n13.txt;Assets/Levels/n14.txt;Assets/Levels/n15.txt;"
                     "Assets/Levels/n16.txt;Assets/Levels/n17.txt;Assets/Levels/n18.txt;Assets/Levels/n19.txt;"
                     "Assets/Levels/n20.txt;Assets/Levels/n21.txt;Assets/Levels/n22.txt;Assets/Levels/n23.txt"};
    FloatField advanceSeconds{this, "advanceSeconds", 3.0f};
    // Con LevelTransition: cuanto dura el festejo antes de que el nivel se
    // aplaste (el paso entero ya da tiempo a mirar, los 3 s de antes sobran).
    FloatField celebrateSeconds{this, "celebrateSeconds", 1.6f};
    FloatField metersPerSecond{this, "metersPerSecond", 3.0f};
    TextField figureMaterial{this, "figureMaterial", "Assets/Materials/Figure.strmat"};
    TextField pieceMaterial{this, "pieceMaterial", "Assets/Materials/Piece.strmat"};
    ObjectField startPlane{this, "startPlane"};
    ObjectField endPlane{this, "endPlane"};
    ObjectField figures{this, "figures"};

    void OnStart() override {
        const char* required[] = {"startPlane", "endPlane", "figures"};
        const ObjectField* fields[] = {&startPlane, &endPlane, &figures};
        for (int i = 0; i < 3; ++i)
            if (!fields[i]->Get()) Log::Warn(L"PuzzleDirector: falta el campo '" + Utf8ToWide(required[i]) + L"' en el Inspector");
        WarnIfMenuIsOff();
        m_levelIndex = 0;
        PublishLevels();
        InputActions& in = InputService::Actions();
        in.ClearAction("game_menu");
        in.BindKey("game_menu", Key::Escape);
        in.BindKey("game_menu", Key::Backspace);
        Load();
        m_audio.Start();
    }

    // El flujo es un estatico y el Scripts.dll puede seguir cargado entre dos
    // Play: sin esto, un Play que termino en el menu dejaria una escena sin
    // menu arrancando escondida.
    void OnDestroy() override {
        m_audio.Stop();
        Flow() = GameFlow{};
        // Lo mismo con los objetos de los planos: el proximo Play es otra copia
        // de la escena, y SurfaceLight no puede tocar un puntero de esta.
        PlaneNodes().clear();
    }

    // Un MainMenu en un objeto APAGADO (o bajo uno apagado) no se instancia:
    // nadie llama a EnterMenu, el flujo se queda en Game y el Play arranca en
    // el nivel 1 sin un solo error - se ve igual que una escena sin menu. Paso
    // el 21/10/2026: la raiz Menu quedo guardada apagada. El menu no puede
    // avisarlo (no corre), asi que lo dice el director.
    static bool FindOffMenu(const std::vector<std::unique_ptr<GameObject>>& nodes, bool parentOff, std::string& where) {
        for (const auto& node : nodes) {
            const bool off = parentOff || !node->active;
            if (off && std::find(node->scripts.begin(), node->scripts.end(), "MainMenu") != node->scripts.end()) {
                where = node->name;
                return true;
            }
            if (FindOffMenu(node->children, off, where)) return true;
        }
        return false;
    }
    static void WarnIfMenuIsOff() {
        std::string where;
        if (FindOffMenu(SceneService::Roots(), false, where))
            Log::Warn(L"PuzzleDirector: el MainMenu de '" + Utf8ToWide(where) +
                      L"' esta APAGADO (el objeto o un padre): el Play arranca en el nivel 1, sin menu. Encendelo en la Jerarquia.");
    }

    void OnUpdate(float dt) override {
        GameFlow& flow = Flow();
        PuzzleSession& s = Session();
        // Primero, antes de cualquier return: el sonido corre en el menu, en la
        // partida y durante el paso (mira lo que dejo el frame anterior).
        m_audio.Tick(dt);
        // GRAV_LEVEL=n (en el entorno): saltar el menu y empezar en el nivel n.
        // Es como se mira un nivel en una corrida automatica (--play
        // --shot-game), que no tiene teclado para llegar hasta el. Sin la
        // variable, nada. En el segundo frame: el menu ya entro en el primero.
        if (++m_frames == 2) {
            const int forced = kit::AutoPick("GRAV_LEVEL");
            if (forced >= 1) flow.StartLevel(std::min(forced, static_cast<int>(LevelList().size())) - 1);
        }
        if (flow.pendingLevel >= 0) {
            m_levelIndex = flow.pendingLevel;
            flow.pendingLevel = -1;
            Load();
        }
        flow.currentLevel = m_levelIndex;
        s.levelIndex = m_levelIndex;
        s.levelCount = static_cast<int>(LevelList().size());
        ShowWorld(flow.InGame());
        if (!flow.InGame()) return;

        if (flow.hasMenu && InputService::Actions().Pressed("game_menu")) {
            flow.ReturnToMenu(MenuScreen::Main);
            return;
        }
        if (s.resetRequested) {
            s.resetRequested = false;
            Load();
        }
        if (!s.loaded) return;

        // Durante el paso entre niveles el nivel se aplasta o crece: ni luz ni
        // jugadas. Las piezas si se arman (el nivel nuevo crece con las suyas).
        if (s.revision != m_builtRevision) BuildFigures(s);
        if (flow.passage.Active()) return;

        const float speed = std::max(0.1f, metersPerSecond.Get()) * (flow.hasMenu ? flow.LightSpeed() : 1.0f);
        if (!s.paused) s.progress = std::min(s.progress + dt * speed, s.beam.length);

        if (!s.solved && s.trace.end == TraceEnd::Reached && s.FullyRevealed()) {
            s.solved = true;
            Log::Info(L"Gravitational: nivel " + std::to_wstring(m_levelIndex + 1) + L" resuelto en " + std::to_wstring(s.moves) +
                      L" jugadas y " + std::to_wstring(static_cast<int>(s.elapsed)) + L" s");
            if (flow.hasMenu && m_levelIndex < 32) {
                flow.solvedMask |= 1u << m_levelIndex;
                host::SaveProgress(flow);
            }
        }
        if (s.solved) s.solvedSeconds += dt;
        else s.elapsed += dt;
        // Con GRAV_SOLVE el nivel resuelto se queda: es para mirarlo, y si
        // pasara al siguiente (que tambien se resuelve solo) la corrida
        // terminaria mostrando otro.
        if (s.solved && kit::AutoPick("GRAV_SOLVE") == 1) return;

        // Resuelto, con transicion: tras el festejo, el nivel se aplasta y el
        // siguiente crece (o, si era el ultimo, se vuelve a los creditos).
        if (flow.hasPassage) {
            if (s.solved && s.solvedSeconds >= celebrateSeconds.Get()) {
                const bool last = m_levelIndex + 1 >= static_cast<int>(LevelList().size());
                if (!last || flow.hasMenu) flow.BeginPassage(last ? m_levelIndex : m_levelIndex + 1, last);
            }
            return;
        }

        // Sin transicion: despues de la animacion de fin, el siguiente nivel.
        if (s.solved && s.solvedSeconds >= advanceSeconds.Get() && m_levelIndex + 1 < static_cast<int>(LevelList().size())) {
            ++m_levelIndex;
            Load();
            return;
        }
        // El ultimo: con menu, de vuelta a el (a los creditos).
        if (s.solved && flow.hasMenu && m_levelIndex + 1 >= static_cast<int>(LevelList().size()) &&
            s.solvedSeconds >= advanceSeconds.Get() + 1.0f)
            flow.ReturnToMenu(MenuScreen::Credits, true);
    }

private:
    void PublishLevels() {
        GameFlow& flow = Flow();
        const std::vector<std::string> list = LevelList();
        flow.levelCount = static_cast<int>(list.size());
        flow.levelNames = list;
    }

    // El mundo del puzzle (planos y piezas) solo en modo Game. Se toca solo al
    // cambiar, para no pelear con quien mas lo active.
    void ShowWorld(bool visible) {
        if (visible == m_worldVisible) return;
        m_worldVisible = visible;
        for (const ObjectField* f : {&startPlane, &endPlane, &figures})
            if (GameObject* node = f->Get()) node->active = visible;
        if (m_portals) m_portals->active = visible;
    }

    void Load() {
        PuzzleSession& s = Session();
        s = PuzzleSession{};
        m_builtRevision = -1;
        const std::vector<std::string> list = LevelList();
        if (list.empty()) {
            Log::Error(L"PuzzleDirector: el campo 'levels' esta vacio");
            return;
        }
        m_levelIndex = std::min(m_levelIndex, static_cast<int>(list.size()) - 1);
        s.levelIndex = m_levelIndex;
        s.levelCount = static_cast<int>(list.size());
        const std::string path = list[static_cast<size_t>(m_levelIndex)];
        std::string text, error;
        if (!FileSystem::ReadAll(SceneService::ProjectRoot() + L"/" + Utf8ToWide(path), text)) {
            Log::Error(L"PuzzleDirector: no se pudo leer el nivel '" + Utf8ToWide(path) + L"'");
            s.Notice("COULD NOT READ THE LEVEL");
            return;
        }
        if (!Board::Parse(text, s.board, error)) {
            Log::Error(L"PuzzleDirector: el nivel '" + Utf8ToWide(path) + L"' esta mal escrito: " + Utf8ToWide(error));
            s.Notice("MALFORMED LEVEL (SEE THE CONSOLE)");
            return;
        }
        // D5: se juega igual (puede que siga funcionando), pero queda dicho.
        if (s.board.rules != kRulesVersion)
            Log::Warn(L"PuzzleDirector: '" + Utf8ToWide(path) + L"' se hizo con las reglas " + std::to_wstring(s.board.rules) +
                      L" y las de hoy son las " + std::to_wstring(kRulesVersion) + L": pasarle levellab check");
        s.loaded = true;
        s.SelectFirstAvailable();
        // GRAV_SOLVE=1: poner la solucion anotada (";! place ...") al cargar.
        // Para mirar un nivel RESUELTO en una corrida automatica: las piezas,
        // la luz entera y el festejo. Sin la variable, nada.
        if (kit::AutoPick("GRAV_SOLVE") == 1) {
            std::istringstream lines(text);
            std::string raw;
            while (std::getline(lines, raw)) {
                const size_t at = raw.find(";! place");
                if (at == std::string::npos) continue;
                std::istringstream in(raw.substr(at + 8));
                std::vector<Figure> got;
                std::string ignored;
                if (Board::ParseFigure(in, got, ignored))
                    for (const Figure& f : got) s.board.Place(f);
            }
        }
        // GRAV_PLACE="sphere 3 3 2|cube 0 1 1 z": poner esas piezas al cargar,
        // con la misma sintaxis que ";! place". Es para MIRAR un estado que no
        // es la solucion - la luz cayendo en un agujero negro, perdiendose -
        // en una corrida automatica, que no tiene raton para armarlo.
        if (const std::string extra = kit::EnvText("GRAV_PLACE"); !extra.empty()) {
            std::istringstream specs(extra);
            std::string spec;
            while (std::getline(specs, spec, '|')) {
                std::istringstream in(spec);
                std::vector<Figure> got;
                std::string error;
                if (Board::ParseFigure(in, got, error))
                    for (const Figure& f : got) s.board.Place(f);
                else
                    Log::Warn(L"GRAV_PLACE: '" + Utf8ToWide(spec) + L"': " + Utf8ToWide(error));
            }
        }
        s.Recompute();
        PlaceScene(s.board);
        // La camara se aleja con el tamano de la caja: la diagonal de un
        // tablero de 3x3x3 es la que da la distancia del Inspector.
        const float diag = std::sqrt(static_cast<float>(s.board.W * s.board.W + s.board.H * s.board.H + s.board.D * s.board.D));
        Flow().cameraScale = std::max(1.0f, diag / std::sqrt(27.0f));
        Log::Info(L"PuzzleDirector: nivel '" + Utf8ToWide(path) + L"', " + std::to_wstring(s.board.W) + L"x" +
                  std::to_wstring(s.board.H) + L"x" + std::to_wstring(s.board.D));
    }

    // Donde caen los bordes de las casillas, para la cuadricula del MUNDO de
    // los shaders (WorldGrid): el tablero esta centrado en el origen
    // (CellCenter), asi que con un lado impar los bordes caen en los medios
    // metros y con uno par en los enteros. Un bit por eje (x, y, z).
    static float GridOrigin(const Board& b) {
        return static_cast<float>((b.W % 2 ? 1 : 0) | (b.H % 2 ? 2 : 0) | (b.D % 2 ? 4 : 0));
    }

    // Cada plano es una CARA de la caja de la cuadricula (ver Board.h): se
    // acomoda a la cara y al tamano del NIVEL, asi otro nivel no pide rearmar
    // la escena.
    // Acomoda el objeto `p` (la primitiva Plane) sobre el plano `f`, que puede
    // estar en cualquier lugar de la caja (planos flotantes, ver Board.h), y le
    // dice al shader que es (Surface.hlsl, UserParams[0].z): 1 solo lineas, 2 de
    // portal.
    void FitQuad(GameObject* p, const Board& b, const Face& f, float mode) {
        if (!p) return;
        // La primitiva Plane es de 2x2 en el XZ de su objeto, mirando a +Y. Por
        // cara: el giro que la lleva ahi y que ejes del mundo quedan en su X y
        // su Z locales. El sentido exacto de la normal no importa: los planos
        // se dibujan por las dos caras.
        const Int3 n = f.normal;
        const Vec3f c = FaceCenter(b, f);
        const int cx = f.hi.x - f.lo.x + 1, cy = f.hi.y - f.lo.y + 1, cz = f.hi.z - f.lo.z + 1;
        float rot[3] = {0.0f, 0.0f, 0.0f};
        int cellsU = cx, cellsV = cz; // piso y techo: X y Z
        if (n.y > 0) rot[0] = 180.0f;
        if (n.x != 0) {
            rot[2] = n.x < 0 ? -90.0f : 90.0f; // paredes izq./der.: su X local pasa a ser la Y del mundo
            cellsU = cy;
            cellsV = cz;
        }
        if (n.z != 0) {
            rot[0] = n.z < 0 ? 90.0f : -90.0f; // frente/fondo: su Z local pasa a ser la Y del mundo
            cellsU = cx;
            cellsV = cy;
        }
        p->transform.position[0] = c.x;
        p->transform.position[1] = c.y;
        p->transform.position[2] = c.z;
        for (int i = 0; i < 3; ++i) p->transform.rotation[i] = rot[i];
        p->transform.scale[0] = cellsU * 0.5f;
        p->transform.scale[1] = 1.0f;
        p->transform.scale[2] = cellsV * 0.5f;
        if (Material* m = SceneService::UniqueMaterial(*p)) {
            m->userParams[0] = {static_cast<float>(cellsU), m->userParams[0].y, mode, static_cast<float>(cellsV)};
            m->userParams[1].w = GridOrigin(b);
        }
    }

    // Cada plano se acomoda al lugar y al tamano que le da el NIVEL, asi otro
    // nivel no pide rearmar la escena.
    void PlaceScene(const Board& b) {
        std::vector<GameObject*>& nodes = PlaneNodes();
        nodes = {startPlane.Get(), endPlane.Get()};
        BuildPortals(b);
        for (size_t i = 0; i < nodes.size(); ++i) FitQuad(nodes[i], b, b.planes[i], b.planes[i].portal ? 2.0f : 1.0f);
    }

    // LOS PLANOS DE MAS: los de portal y los paneles (planos flotantes). Los
    // trae cada nivel, asi que no son de la escena: se crean al cargar, como
    // hijos de "Portales" (que cuelga de la
    // misma raiz que los planos de la escena: el paso entre niveles los aplasta
    // junto con todo lo demas), con la misma primitiva y el mismo material que
    // el plano de salida. Quedan en PlaneNodes en el orden de board.planes.
    void BuildPortals(const Board& b) {
        GameObject* model = startPlane.Get();
        if (!model || !model->parent) return;
        if (!m_portals) {
            for (const auto& child : model->parent->children)
                if (child->name == "Portales") m_portals = child.get();
            if (!m_portals) {
                auto holder = std::make_unique<GameObject>();
                holder->name = "Portales";
                holder->primitive = PrimitiveType::Empty;
                holder->parent = model->parent;
                m_portals = holder.get();
                model->parent->children.push_back(std::move(holder));
            }
        }
        m_portals->children.clear();
        m_portals->active = m_worldVisible;
        for (size_t i = 2; i < b.planes.size(); ++i) {
            auto node = std::make_unique<GameObject>();
            node->name = (b.planes[i].portal ? "Portal " : "Panel ") + std::to_string(i - 1);
            node->primitive = model->primitive;
            node->materialPath = model->materialPath;
            node->faceCulling = false;
            node->castShadows = false;
            node->parent = m_portals;
            PlaneNodes().push_back(node.get());
            m_portals->children.push_back(std::move(node));
        }
    }

    // Los objetos de las piezas: se rehacen enteros cuando el tablero cambia,
    // en el MISMO orden que board.figures (SurfaceLight cuenta con eso). Son
    // pocos y rehacerlos es mas simple y mas dificil de dejar desincronizado
    // que moverlos uno por uno.
    void BuildFigures(PuzzleSession& s) {
        m_builtRevision = s.revision;
        GameObject* container = figures.Get();
        if (!container) return;
        container->children.clear();
        const std::wstring figMat = Utf8ToWide(figureMaterial.Get());
        const std::wstring pieceMat = Utf8ToWide(pieceMaterial.Get());
        const float origin = GridOrigin(s.board);
        for (const Figure& f : s.board.figures) {
            const PieceLook look = LookOf(f);
            auto node = std::make_unique<GameObject>();
            node->name = NodeName(f);
            ApplyLook(*node, look);
            const Vec3f c = FigurePos(s.board, f);
            node->transform.position[0] = c.x;
            node->transform.position[1] = c.y;
            node->transform.position[2] = c.z;
            node->materialPath = look.pieceShader ? pieceMat : figMat;
            node->parent = container;
            GameObject* raw = node.get();
            container->children.push_back(std::move(node));
            Material* m = SceneService::UniqueMaterial(*raw);
            if (!m) continue;
            if (!look.pieceShader) {
                // Figure.hlsl: el eje de la franja del cubo, el centro, si es
                // fija, y el lado (una esfera de 3 mide sus arcos por 3).
                m->userParams[0].w = look.scale;
                // El eje CON SENTIDO: el shader dibuja hacia donde gira la franja.
                const Int3 axis = CubeAxis(f.axis);
                m->userParams[2] = {static_cast<float>(axis.x), static_cast<float>(axis.y), static_cast<float>(axis.z),
                                    f.shape == Shape::Cube ? 1.0f : 0.0f};
                m->userParams[3] = {c.x, c.y, c.z, f.fixed ? 1.0f : 0.0f};
                m->userParams[1].w = origin;
            } else {
                // Piece.hlsl: el marco, la forma, el centro de la celda ancla.
                const Rot& r = look.frame;
                m->userParams[0].w = f.shape == Shape::Wormhole ? static_cast<float>((f.pair % 3 + 3) % 3) : 0.0f;
                m->userParams[1] = {static_cast<float>(r.u.x), static_cast<float>(r.u.y), static_cast<float>(r.u.z), origin};
                m->userParams[2] = {static_cast<float>(r.f.x), static_cast<float>(r.f.y), static_cast<float>(r.f.z),
                                    static_cast<float>(static_cast<int>(f.shape))};
                m->userParams[3] = {c.x, c.y, c.z, f.fixed ? 1.0f : 0.0f};
            }
        }
    }

private:
    std::vector<std::string> LevelList() const {
        std::vector<std::string> out;
        const std::string all = levels.Get();
        size_t start = 0;
        while (start <= all.size()) {
            size_t semi = all.find(';', start);
            std::string item = all.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
            while (!item.empty() && item.front() == ' ') item.erase(item.begin());
            while (!item.empty() && item.back() == ' ') item.pop_back();
            if (!item.empty()) out.push_back(item);
            if (semi == std::string::npos) break;
            start = semi + 1;
        }
        return out;
    }

    GameAudio m_audio;
    GameObject* m_portals = nullptr; // "Portales": los planos de mas del nivel (ver BuildPortals)
    int m_builtRevision = -1;
    int m_levelIndex = 0;
    int m_frames = 0;
    bool m_worldVisible = true;
};

STRACE_REGISTER_SCRIPT(PuzzleDirector)
