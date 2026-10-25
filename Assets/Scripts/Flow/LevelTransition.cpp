// EL PASO ENTRE NIVELES: el mismo lenguaje que el menu, al reves y al derecho.
//
//   Out   el nivel resuelto PIERDE sus dimensiones, una por una: se aplasta en
//         un plano (Y), el plano en una linea (Z), la linea en un punto (X).
//   Card  del punto sale una linea, y la luz la recorre escribiendo el nombre
//         del nivel que viene ("LEVEL 02").
//   In    la linea se recoge mientras el nivel nuevo CRECE desde ella: linea
//         (X), plano (Z), caja (Y). Recien ahi sale su luz.
//
// Entrar desde el menu es solo Card + In: la caja del menu se despliega, queda
// el punto, "LEVEL 01", y el nivel crece. Resolver el ultimo es solo Out, y
// el menu se pliega alrededor, en los creditos.
//
// En que orden: el primer eje que se pierde es el de la normal del plano de
// salida (dos paredes enfrentadas se juntan en UNA lamina: se ve el plano), el
// segundo el mas corto de los otros dos, y queda el mas largo (se ve la linea).
// Crecer es el mismo orden al reves. Un orden fijo (Y, Z, X) dejaba a las dos
// paredes del nivel 1 hechas dos puntos invisibles a mitad del paso.
//
// Como se aplasta: todo el mundo del nivel (los dos planos, las figuras y el
// fantasma) cuelga de UNA raiz, "Nivel", centrada en el pivote, y lo que se
// anima es su escala, eje por eje del MUNDO. Asi el director y el constructor
// de figuras no saben nada de esto: siguen poniendo cosas en sus celdas, y el
// padre las deforma. La luz y las marcas se miden en el mundo y quedarian
// corridas mientras tanto, asi que SurfaceLight las apaga durante el paso.
//
// Va en el vacio "Puzzle", despues de PuzzleDirector. Sin este script en la
// escena, los niveles se suceden de golpe, como al principio.

#include "GameFlow.h"
#include "MenuKit.h"
#include "../Puzzle/PuzzleSession.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Rendering/Material.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/Scene/SceneTransform.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Text.h"
#include "Engine/UI/UiService.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

using namespace strace;
using namespace gravitational;
namespace kit = gravitational::kit;

namespace {

const char* kSheetMat = "Assets/Materials/Sheet.strmat";
constexpr float kDeg = 3.14159265f / 180.0f;
constexpr float kCells = 9.0f;     // la linea del nombre, en celdas
constexpr float kCell = 0.34f;
constexpr float kAxisTime = 0.32f; // cada dimension que se pierde o se gana
constexpr float kFlat = 0.002f;    // "cero" de escala: una matriz singular no da normales

} // namespace

class LevelTransition : public IScript {
public:
    ObjectField level{this, "level"};
    ObjectField camera{this, "camera"};
    ObjectField pivot{this, "pivot"};
    FloatField holdSeconds{this, "holdSeconds", 0.6f};

    void OnStart() override {
        Flow().hasPassage = true;
        if (!level.Get()) Log::Warn(L"LevelTransition: falta el campo 'level' (la raiz que agrupa planos y figuras)");
        auto node = std::make_unique<GameObject>();
        node->name = "Paso_Lamina";
        node->primitive = PrimitiveType::Plane;
        node->materialPath = Utf8ToWide(kSheetMat);
        node->castShadows = false;
        node->faceCulling = false;
        node->active = false;
        node->parent = gameObject;
        m_sheet = node.get();
        gameObject->children.push_back(std::move(node));
    }

    void OnUpdate(float dt) override {
        GameFlow& flow = Flow();
        Passage& p = flow.passage;
        if (!flow.InGame() || !p.Active()) {
            // Fuera del paso (o de vuelta al menu en medio de uno): el nivel
            // entero, y la lamina guardada.
            SetScale(1.0f, 1.0f, 1.0f);
            m_sheet->active = false;
            m_stage = PassageStage::None;
            return;
        }
        if (p.stage != m_stage) {
            m_stage = p.stage;
            p.t = 0.0f;
            m_chars = 0.0f;
            // El orden sale del nivel que hay en ese momento: el que se va al
            // empezar Out, el que llega al empezar In.
            if (p.stage == PassageStage::Out || p.stage == PassageStage::In) PickOrder();
        }
        p.t += dt;

        float kx = 1, ky = 1, kz = 1, stretch = 0, dot = 0, textA = 0;
        switch (p.stage) {
            case PassageStage::Out: {
                // 3D -> 2D -> 1D -> 0D
                float k[3];
                for (int i = 0; i < 3; ++i) k[m_order[i]] = 1.0f - Step(p.t, i);
                kx = k[0];
                ky = k[1];
                kz = k[2];
                dot = kit::Saturate((p.t - 2.5f * kAxisTime) / (0.6f * kAxisTime));
                if (p.t >= 3.0f * kAxisTime) {
                    if (p.toCredits) {
                        flow.ReturnToMenu(MenuScreen::Credits, true);
                        SetScale(1, 1, 1);
                        m_sheet->active = false;
                        return;
                    }
                    flow.pendingLevel = p.next; // el director lo carga, ya en cero
                    p.stage = PassageStage::Card;
                }
                break;
            }
            case PassageStage::Card: {
                kx = ky = kz = 0.0f;
                dot = 1.0f - kit::Saturate(p.t / 0.5f);
                stretch = kit::EaseOutCubic(p.t / 0.55f);
                if (p.t > 0.5f) m_chars = std::min(m_chars + dt * 12.0f, static_cast<float>(Title(p.next).size()));
                textA = 1.0f;
                const float written = 0.5f + Title(p.next).size() / 12.0f;
                if (p.t >= written + holdSeconds.Get()) p.stage = PassageStage::In;
                break;
            }
            case PassageStage::In: {
                // 0D -> 1D -> 2D -> 3D: la linea del nombre se recoge mientras
                // la del nivel crece desde el mismo punto.
                float k[3];
                for (int i = 0; i < 3; ++i) k[m_order[2 - i]] = Step(p.t, i);
                kx = k[0];
                ky = k[1];
                kz = k[2];
                stretch = 1.0f - kit::EaseInOutCubic(p.t / kAxisTime);
                textA = 1.0f - kit::Saturate(p.t / (0.6f * kAxisTime));
                m_chars = 99.0f;
                if (p.t >= 3.0f * kAxisTime) {
                    p = Passage{};
                    m_stage = PassageStage::None;
                    SetScale(1, 1, 1);
                    m_sheet->active = false;
                    return;
                }
                break;
            }
            default: break;
        }
        SetScale(kx, ky, kz);
        PushSheet(stretch, dot, p.stage == PassageStage::Card);
        if (textA > 0.0f) DrawTitle(p.next, textA);
    }

private:
    void PickOrder() {
        const Board& b = Session().board;
        const int first = AxisOf(b.planes[0].normal);
        const int size[3] = {b.W, b.H, b.D};
        int a = (first + 1) % 3, c = (first + 2) % 3;
        if (size[a] > size[c]) std::swap(a, c);
        m_order[0] = first;
        m_order[1] = a; // el mas corto
        m_order[2] = c; // el mas largo: la linea que queda
    }

    // El eje `i` (0, 1, 2) de la escalera: va de 0 a 1 en su tercio del tiempo.
    static float Step(float t, int i) { return kit::EaseInOutCubic((t - i * kAxisTime) / kAxisTime); }

    static std::string Title(int levelIndex) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "LEVEL %02d", levelIndex + 1);
        return buf;
    }

    void SetScale(float x, float y, float z) {
        GameObject* root = level.Get();
        if (!root) return;
        root->transform.scale[0] = std::max(kFlat, x);
        root->transform.scale[1] = std::max(kFlat, y);
        root->transform.scale[2] = std::max(kFlat, z);
    }

    // Los ejes de la camara: la lamina la mira de frente, en el pivote.
    bool CameraAxes(float right[3], float up[3], float fwd[3]) const {
        const GameObject* cam = camera.Get();
        if (!cam) return false;
        const float p = cam->transform.rotation[0] * kDeg, y = cam->transform.rotation[1] * kDeg;
        fwd[0] = std::sin(y) * std::cos(p);
        fwd[1] = -std::sin(p);
        fwd[2] = std::cos(y) * std::cos(p);
        right[0] = std::cos(y);
        right[1] = 0.0f;
        right[2] = -std::sin(y);
        up[0] = fwd[1] * right[2] - fwd[2] * right[1];
        up[1] = fwd[2] * right[0] - fwd[0] * right[2];
        up[2] = fwd[0] * right[1] - fwd[1] * right[0];
        return true;
    }

    void Origin(float o[3]) const {
        o[0] = o[1] = o[2] = 0.0f;
        if (const GameObject* pv = pivot.Get()) {
            const DirectX::XMFLOAT3 c = ComputeWorldPosition(*pv);
            o[0] = c.x;
            o[1] = c.y;
            o[2] = c.z;
        }
    }

    void PushSheet(float stretch, float dot, bool writing) {
        float r[3], u[3], f[3], o[3];
        if (!CameraAxes(r, u, f)) return;
        Origin(o);
        m_sheet->active = stretch > 0.001f || dot > 0.001f;
        m_sheet->transform.position[0] = o[0];
        m_sheet->transform.position[1] = o[1];
        m_sheet->transform.position[2] = o[2];
        m_sheet->transform.rotation[0] = std::acos(std::clamp(f[1], -1.0f, 1.0f)) / kDeg;
        m_sheet->transform.rotation[1] = std::atan2(f[0], f[2]) / kDeg;
        m_sheet->transform.rotation[2] = 0.0f;
        const float half = (kCells * 0.5f + 1.5f) * kCell;
        m_sheet->transform.scale[0] = half;
        m_sheet->transform.scale[1] = 1.0f;
        m_sheet->transform.scale[2] = half;
        Material* m = SceneService::UniqueMaterial(*m_sheet);
        if (!m) return;
        m->userParams[0] = {o[0], o[1], o[2], kCell};
        m->userParams[1] = {r[0], r[1], r[2], stretch};
        m->userParams[2] = {u[0], u[1], u[2], 0.0f};
        m->userParams[3] = {kCells, 1.0f, 1.0f, dot};
        // La luz que escribe el nombre, al paso de las letras.
        const float total = static_cast<float>(Title(Flow().passage.next).size());
        const float k = writing ? std::min(1.0f, m_chars / total) : 0.0f;
        const float half2 = kCells * 0.5f * stretch;
        m->userParams[4] = {0.0f, 0.0f, 0.0f, k > 0.0f && k < 1.0f ? 1.0f : 0.0f};
        m->userParams[5] = {-half2, 0.0f, -half2 + 2.0f * half2 * k, 0.0f};
    }

    void DrawTitle(int levelIndex, float a) {
        const GameObject* cam = camera.Get();
        if (!cam || !cam->camera) return;
        float r[3], u[3], f[3], o[3];
        if (!CameraAxes(r, u, f)) return;
        Origin(o);
        const DirectX::XMMATRIX vp = kit::ViewProj(*cam);
        float x, y;
        if (!kit::WorldToScreen(vp, o[0] + u[0] * 0.55f, o[1] + u[1] * 0.55f, o[2] + u[2] * 0.55f, x, y)) return;
        UiDrawList& ui = UiService::Frame();
        const std::string title = Title(levelIndex);
        const int big = kit::TextScale(1.6f);
        const float w = kit::TextW(title, big);
        kit::TextTyped(ui, std::floor(x - w * 0.5f), std::floor(y - kit::TextH(big)), title, m_chars, kit::White(a), big);
        const GameFlow& fl = Flow();
        if (fl.levelCount > 0 && m_chars >= title.size()) {
            char sub[24];
            std::snprintf(sub, sizeof(sub), "OF %02d", fl.levelCount);
            float sx, sy;
            if (kit::WorldToScreen(vp, o[0] - u[0] * 0.45f, o[1] - u[1] * 0.45f, o[2] - u[2] * 0.45f, sx, sy))
                kit::TextCentered(ui, sx, sy, sub, kit::Faint(a), std::max(1, kit::TextScale() - 1));
        }
    }

    GameObject* m_sheet = nullptr;
    PassageStage m_stage = PassageStage::None;
    float m_chars = 0.0f;
    int m_order[3] = {1, 2, 0};
};

STRACE_REGISTER_SCRIPT(LevelTransition)
