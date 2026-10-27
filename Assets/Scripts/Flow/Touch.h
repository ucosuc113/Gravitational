#pragma once

// EL DEDO: lo que el juego necesita para jugarse en un telefono.
//
// En Android el primer dedo es el cursor y el boton izquierdo del raton
// (Player/src/android_main.cpp), y los demas llegan aparte, como posiciones
// (InputActions::TouchCount). No hay boton derecho para orbitar, ni rueda para
// el zoom, ni teclado para el menu. Asi que el mismo dedo tiene que significar
// varias cosas, y lo que las separa es el GESTO:
//
//   toque     bajo y subio casi en el mismo sitio: es un clic. Se informa al
//             SOLTAR, no al apoyar - si no, empezar a deslizar sobre una fila
//             del menu la elegiria antes de saber que era un deslizamiento.
//   arrastre  se movio mas que el umbral: gira la camara (en la partida) y ya
//             no es un toque aunque vuelva a donde empezo.
//   barrido   un arrastre corto y rapido, sobre todo horizontal o vertical: en
//             el menu pasa de pagina o cambia un valor.
//   pellizco  dos dedos que se acercan o se alejan: el zoom. En cuanto apoya
//             el segundo, lo de un dedo se CANCELA hasta que se levanten
//             todos: ni un toque al soltar, ni un giro con el dedo que queda
//             (el cursor salta a el, y eso no es un arrastre de nadie).
//
// En PC nada de esto se usa: el clic sigue siendo al apretar y la orbita sigue
// siendo el boton derecho. TouchMode() decide; en el Editor se prueba con
// GRAV_TOUCH=1 (el raton hace de dedo: el boton izquierdo arrastra la camara).
//
// El umbral se mide en fraccion del ALTO de la pantalla y no en pixeles: un
// telefono de 1080 filas y uno de 720 tienen que sentir lo mismo con el mismo
// dedo, y el dedo no sabe de pixeles.

#include "MenuKit.h"

#include "Engine/Gameplay/GameTime.h"
#include "Engine/Input/InputService.h"
#include "Engine/UI/UiService.h"

#include <cmath>
#include <cstdint>

namespace gravitational {

// true en Android; en PC solo con GRAV_TOUCH=1 (MenuKit.h).
using kit::TouchMode;

// "TAP" o "CLICK", para los textos que le dicen a la persona que hacer.
inline const char* TapWord() { return TouchMode() ? "TAP" : "CLICK"; }

enum class Swipe { None, Left, Right, Up, Down };

class TouchGesture {
public:
    // Una vez por frame. `overUi` es si el dedo esta sobre la interfaz AHORA;
    // solo importa en el frame en que apoya (un arrastre que empezo en un
    // boton no gira la camara aunque salga de el).
    void Update(bool overUi) {
        strace::InputActions& in = strace::InputService::Actions();
        if (!m_bound) {
            in.BindMouseButton("touch_finger", strace::MouseButton::Left);
            m_bound = true;
        }
        const bool held = in.Held("touch_finger");
        const float x = in.CursorX(), y = in.CursorY();
        const float h = std::max(1.0f, strace::UiService::Height());

        began = held && !m_wasHeld;
        ended = !held && m_wasHeld;
        tap = false;
        swipe = Swipe::None;
        dx = dy = 0.0f;

        if (began) {
            startX = lastX = x;
            startY = lastY = y;
            time = 0.0f;
            dragging = false;
            startedOnUi = overUi;
            cancelled = false;
        }
        if (held || ended) {
            time += strace::GameTime::RawDelta();
            // El ultimo punto conocido: al soltar, Android deja el cursor donde
            // subio el dedo, que es justo el que hace falta para el toque.
            const float tx = x - startX, ty = y - startY;
            if (!dragging && std::sqrt(tx * tx + ty * ty) > kDragFraction * h) dragging = true;
            if (dragging && !began) {
                dx = x - lastX;
                dy = y - lastY;
            }
            lastX = x;
            lastY = y;
        }
        if (ended) {
            const float tx = x - startX, ty = y - startY;
            if (!dragging) tap = true;
            // Barrido: rapido, largo y claramente en un eje (el doble que en
            // el otro). Lento es un arrastre que se arrepintio, no un barrido.
            else if (!cancelled && time < kSwipeSeconds) {
                const float ax = std::fabs(tx), ay = std::fabs(ty);
                if (ax > kSwipeFraction * h && ax > 2.0f * ay) swipe = tx < 0.0f ? Swipe::Left : Swipe::Right;
                else if (ay > kSwipeFraction * h && ay > 2.0f * ax) swipe = ty < 0.0f ? Swipe::Up : Swipe::Down;
            }
        }
        m_wasHeld = held;
        down = held;

        // El pellizco: cuanto cambio la distancia entre los dos primeros dedos
        // desde el frame anterior. Solo entre dos frames con la MISMA cuenta de
        // dedos: si uno entra o sale, los indices se corren y la distancia
        // saltaria de un par de dedos a otro.
        const int fingers = in.TouchCount();
        pinch = 1.0f;
        pinching = fingers >= 2;
        if (pinching) {
            const float px = in.TouchX(1) - in.TouchX(0), py = in.TouchY(1) - in.TouchY(0);
            const float d = std::sqrt(px * px + py * py);
            if (fingers == m_prevFingers && m_pinchDist > 1.0f && d > 1.0f) pinch = d / m_pinchDist;
            m_pinchDist = d;
            Cancel();
        } else {
            m_pinchDist = 0.0f;
        }
        m_prevFingers = fingers;
    }

    // Lo mismo, pero una sola vez por frame aunque lo llamen varios scripts
    // (la camara y el constructor comparten el dedo de la partida).
    void UpdateOnce(bool overUi) {
        const uint64_t f = strace::GameTime::FrameCount();
        if (f == m_frame && m_frameKnown) return;
        m_frame = f;
        m_frameKnown = true;
        Update(overUi);
    }

    // Olvida el dedo en curso: el que este apoyado tiene que levantarse y
    // volver a apoyar para contar (al salir de un menu, por ejemplo, el mismo
    // toque que eligio "jugar" no tiene que poner una pieza).
    void Cancel() {
        dragging = true;
        startedOnUi = true;
        cancelled = true;
    }

    bool down = false;        // apoyado ahora
    bool began = false;       // apoyo este frame
    bool ended = false;       // se levanto este frame
    bool tap = false;         // se levanto sin haber arrastrado: un clic
    bool dragging = false;    // ya paso el umbral
    bool startedOnUi = false; // apoyo sobre la interfaz
    Swipe swipe = Swipe::None;
    float startX = 0.0f, startY = 0.0f;
    float dx = 0.0f, dy = 0.0f; // lo que se movio este frame, solo arrastrando
    float time = 0.0f;          // segundos desde que apoyo
    bool cancelled = false;     // Cancel() desde que apoyo: ni toque ni barrido
    bool pinching = false;      // hay dos dedos o mas
    float pinch = 1.0f;         // distancia entre los dos dedos / la del frame anterior (>1 = se separan)

private:
    // 3% del alto: ~30 px en un telefono de 1080 filas, menos que el temblor
    // de un dedo que apoya con prisa pero mucho menos que cualquier intencion.
    static constexpr float kDragFraction = 0.03f;
    static constexpr float kSwipeFraction = 0.10f;
    static constexpr float kSwipeSeconds = 0.6f;

    float lastX = 0.0f, lastY = 0.0f;
    float m_pinchDist = 0.0f;
    int m_prevFingers = 0;
    bool m_wasHeld = false;
    bool m_bound = false;
    uint64_t m_frame = 0;
    bool m_frameKnown = false;
};

// El dedo de la PARTIDA, compartido por OrbitCamera (arrastre) y FigureBuilder
// (toque). Un solo seguimiento: si cada uno llevara el suyo podrian discrepar
// sobre si un gesto fue arrastre, y entonces el mismo dedo giraria la camara Y
// pondria una pieza.
inline TouchGesture& GameTouch() {
    static TouchGesture g;
    return g;
}

} // namespace gravitational
