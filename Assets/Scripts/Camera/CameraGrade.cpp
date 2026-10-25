// EL AJUSTE DE COLOR QUE RESPONDE A LA LUZ. Va en la camara, junto a
// OrbitCamera y FigureBuilder.
//
// El LOOK de base esta en la escena (Camara > Camara > Ajuste de color, del
// motor desde el 22/10/2026): negros apenas azules, contraste alrededor de un
// pivote bajo para que el vacio quede negro, luces un poco bajadas para que el
// centro de la luz no se queme contra el bloom, y algo de intensidad. Se edita
// ahi, en el Inspector, no aqui: este script lo LEE al empezar y solo lo empuja.
//
// Lo que agrega es lo que el jugador tiene que SENTIR sin leer el HUD:
//   resuelto        un destello calido que se apaga (sube exposicion e intensidad)
//   agujero negro   la imagen se apaga y pierde color, con los negros hacia el
//                   rojo del logo: la luz cayo y no vuelve
//   se perdio       un poco menos de color: no es un error, es un "todavia no"
// Todo con una constante de tiempo, nunca de golpe: el ajuste de color que salta
// en un frame se lee como un parpadeo del monitor, no como una reaccion.
//
// Por que en la camara y no en el post-proceso de la escena: el ajuste va
// ANTES de la UI (el HUD no se tine, ver ColorGrading.h) y el material de post
// de esta escena esta vacio. Y por que no en el shader de cada superficie: es la
// imagen entera la que reacciona, no cada pieza.

#include "../Puzzle/PuzzleSession.h"
#include "../Flow/GameFlow.h"
#include "../Flow/MenuKit.h"

#include "Engine/Scene/GameObject.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"

#include <cmath>

using namespace strace;
using namespace gravitational;

class CameraGrade : public IScript {
public:
    // Cuanto pesa cada reaccion (0 = apagada), por si el look de base cambia y
    // hay que dosificarlas sin tocar el codigo.
    FloatField solvedFlash{this, "solvedFlash", 1.0f};
    FloatField fallDarken{this, "fallDarken", 1.0f};
    FloatField lostFade{this, "lostFade", 1.0f};

    void OnStart() override {
        if (gameObject->camera) m_base = gameObject->camera->grade;
    }

    // Detener vuelve a la escena guardada, pero el Scripts.dll puede seguir: que
    // la camara no se quede con el ultimo empujon.
    void OnDestroy() override {
        if (gameObject->camera) gameObject->camera->grade = m_base;
    }

    void OnUpdate(float dt) override {
        if (!gameObject->camera) return;
        const PuzzleSession& s = Session();
        const GameFlow& flow = Flow();
        const bool playing = flow.InGame() && !flow.passage.Active() && s.loaded;
        const bool settled = playing && !s.solved && s.FullyRevealed();
        const bool fell = settled && s.trace.end == TraceEnd::Absorbed;
        const bool lost = settled && !fell && s.trace.end != TraceEnd::Reached;

        m_fall = kit::Approach(m_fall, fell ? 1.0f : 0.0f, fell ? 2.5f : 4.0f, dt);
        m_lost = kit::Approach(m_lost, lost ? 1.0f : 0.0f, 2.0f, dt);
        const float flash = playing && s.solved ? std::exp(-s.solvedSeconds * 1.4f) : 0.0f;

        ColorGrading g = m_base;
        const float f = m_fall * fallDarken.Get(), l = m_lost * lostFade.Get(), k = flash * solvedFlash.Get();
        g.exposure += 0.35f * k - 0.4f * f - 0.08f * l;
        g.temperature += 0.18f * k;
        g.vibrance += 0.25f * k;
        g.saturation *= (1.0f - 0.65f * f) * (1.0f - 0.2f * l);
        g.lift.x += 0.10f * f; // el tono 0 de la rueda es el rojo
        g.lift.master -= 0.03f * f;
        gameObject->camera->grade = g;
    }

private:
    ColorGrading m_base;
    float m_fall = 0.0f, m_lost = 0.0f;
};

STRACE_REGISTER_SCRIPT(CameraGrade)
