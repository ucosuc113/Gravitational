#pragma once

// LAS HERRAMIENTAS DE UN MENU: la paleta, la entrada (teclado y raton con la
// misma navegacion), pasar un punto del mundo a la pantalla, y el texto. El
// menu y la transicion entre niveles dibujan con esto; ninguna cosa de aqui
// sabe COMO es un menu.
//
// El texto es la fuente de pixeles del motor (5x7, solo ASCII, escala ENTERA):
// se escribe sin tildes y la escala sale de la altura de la pantalla, para que
// un 1280x720 y un 1920x1080 lean lo mismo.

#include "Engine/Input/InputService.h"
#include "Engine/Rendering/Camera.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/SceneService.h"
#include "Engine/UI/PixelFont.h"
#include "Engine/UI/UiDrawList.h"
#include "Engine/UI/UiService.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace gravitational::kit {

using namespace strace;

// --- La paleta del juego: el vacio negro, la cuadricula blanca y la luz ---
inline UiColor White(float a = 1.0f) { return UiColor::Rgba(1.0f, 1.0f, 1.0f, a); }
inline UiColor Dim(float a = 1.0f) { return UiColor::Rgba(0.55f, 0.58f, 0.62f, a); }
inline UiColor Faint(float a = 1.0f) { return UiColor::Rgba(0.30f, 0.32f, 0.36f, a); }
inline UiColor Beam(float a = 1.0f) { return UiColor::Rgba(0.55f, 0.85f, 1.0f, a); }
inline UiColor Red(float a = 1.0f) { return UiColor::Rgba(0.85f, 0.12f, 0.12f, a); }
inline UiColor Black(float a = 1.0f) { return UiColor::Rgba(0.0f, 0.0f, 0.0f, a); }
inline UiColor Mix(const UiColor& a, const UiColor& b, float t) {
    return UiColor::Rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

// Se juega con el dedo: siempre en Android, y en PC con GRAV_TOUCH=1 (el raton
// hace de dedo, para probarlo en el Editor). Ver Flow/Touch.h, que es donde se
// usa; vive aca porque el tamano del texto tambien depende de esto.
inline bool TouchMode() {
#if defined(__ANDROID__)
    return true;
#elif defined(_MSC_VER)
    static const bool on = [] {
        char buf[8] = {};
        size_t len = 0;
        return getenv_s(&len, buf, sizeof(buf), "GRAV_TOUCH") == 0 && len > 0 && buf[0] == '1';
    }();
    return on;
#else
    static const bool on = [] {
        const char* v = std::getenv("GRAV_TOUCH");
        return v && v[0] == '1';
    }();
    return on;
#endif
}

// La escala base del texto: 1 por cada 240 filas (720p = 3, 1080p = 4 con
// redondeo hacia abajo... 4,5 -> 4). Un `big` mas para titulos.
//
// Con el dedo, un escalon mas: la pantalla de un telefono mide la quinta parte
// que un monitor con las mismas filas, y a 1/38 del alto la letra queda en un
// milimetro y medio.
inline int TextScale(float extra = 0.0f) {
    const float h = UiService::Height();
    return std::max(1, static_cast<int>(std::floor(h / 240.0f + extra)) + (TouchMode() ? 1 : 0));
}
inline float TextW(std::string_view t, int s) { return static_cast<float>(MeasureTextWidth(t, s)); }
inline float TextH(int s) { return static_cast<float>(kGlyphHeight * s); }

// Texto centrado en (cx, cy).
inline UiRect TextCentered(UiDrawList& ui, float cx, float cy, std::string_view t, const UiColor& c, int s) {
    const float w = TextW(t, s), h = TextH(s);
    const UiRect r{std::floor(cx - w * 0.5f), std::floor(cy - h * 0.5f), w, h};
    ui.AddText(r.x, r.y, t, c, s);
    return r;
}

// Texto que se ESCRIBE: solo las primeras `chars` letras (con una fraccion
// para la letra que esta apareciendo, en el brillo de la luz).
inline void TextTyped(UiDrawList& ui, float x, float y, std::string_view t, float chars, const UiColor& c, int s) {
    const int n = std::clamp(static_cast<int>(chars), 0, static_cast<int>(t.size()));
    ui.AddText(x, y, t.substr(0, n), c, s);
    if (n < static_cast<int>(t.size())) {
        const float frac = chars - static_cast<float>(n);
        const float x1 = x + TextW(t.substr(0, n), s) + (n > 0 ? static_cast<float>(s) : 0.0f);
        ui.AddText(x1, y, t.substr(n, 1), Beam(frac), s);
    }
}

// --- La entrada del menu ---
//
// Teclado y raton navegan LO MISMO: flechas o WASD mueven, Enter o Espacio
// aceptan, Esc o Retroceso vuelven, y el raton elige al pasar por encima y
// acepta con un clic. El raton solo "toma" la seleccion cuando SE MUEVE: un
// cursor quieto encima de una fila no puede pisarle la flecha a quien usa
// teclado.
struct Nav {
    bool up = false, down = false, left = false, right = false, ok = false, back = false;
    bool click = false, mouseMoved = false, mouseInside = false;
    float mx = 0.0f, my = 0.0f;
};

inline void BindMenuInput() {
    InputActions& in = InputService::Actions();
    auto bind = [&](const char* name, Key a, Key b) {
        in.ClearAction(name);
        in.BindKey(name, a);
        in.BindKey(name, b);
    };
    bind("menu_up", Key::Up, Key::W);
    bind("menu_down", Key::Down, Key::S);
    bind("menu_left", Key::Left, Key::A);
    bind("menu_right", Key::Right, Key::D);
    bind("menu_ok", Key::Enter, Key::Space);
    bind("menu_back", Key::Escape, Key::Backspace);
    in.ClearAction("menu_click");
    in.BindMouseButton("menu_click", MouseButton::Left);
}

inline Nav ReadNav() {
    InputActions& in = InputService::Actions();
    Nav n;
    n.up = in.Pressed("menu_up");
    n.down = in.Pressed("menu_down");
    n.left = in.Pressed("menu_left");
    n.right = in.Pressed("menu_right");
    n.ok = in.Pressed("menu_ok");
    n.back = in.Pressed("menu_back");
    n.mouseInside = in.CursorInside();
    n.click = n.mouseInside && in.Pressed("menu_click");
    n.mx = in.CursorX();
    n.my = in.CursorY();
    n.mouseMoved = n.mouseInside && (std::fabs(in.CursorDeltaX()) + std::fabs(in.CursorDeltaY()) > 0.5f);
    return n;
}

inline bool Inside(const UiRect& r, float x, float y, float pad = 0.0f) {
    return x >= r.x - pad && x <= r.x + r.width + pad && y >= r.y - pad && y <= r.y + r.height + pad;
}

// --- Del mundo a la pantalla ---
//
// Con la MISMA matriz con que se dibuja (la camara activa y el aspecto de la
// UI), asi el texto cae exactamente sobre la superficie que nombra.
inline DirectX::XMMATRIX ViewProj(const GameObject& cam) {
    const float w = UiService::Width(), h = UiService::Height();
    return ComputeGameCameraView(cam) * ComputeGameCameraProjection(*cam.camera, SceneService::Type(), w / std::max(1.0f, h));
}

// false si el punto queda detras de la camara.
inline bool WorldToScreen(const DirectX::XMMATRIX& vp, float x, float y, float z, float& sx, float& sy) {
    using namespace DirectX;
    const XMVECTOR c = XMVector4Transform(XMVectorSet(x, y, z, 1.0f), vp);
    const float w = XMVectorGetW(c);
    if (w <= 1e-4f) return false;
    sx = (XMVectorGetX(c) / w * 0.5f + 0.5f) * UiService::Width();
    sy = (1.0f - (XMVectorGetY(c) / w * 0.5f + 0.5f)) * UiService::Height();
    return true;
}

// --- Para capturas sin nadie delante ---
//
// GRAV_MENU_AUTO=n (en el entorno): el menu elige SOLO la opcion n del menu
// principal al segundo de arrancar, por el mismo camino que una tecla. Es como
// se mira el menu USADO en una corrida automatica (--play --shot-game), que no
// tiene teclado. GRAV_MENU_AUTO2=m elige ademas la fila m de la pantalla que se
// abra. Sin la variable, nada.
inline int AutoPick(const char* var = "GRAV_MENU_AUTO") {
    char buf[16] = {};
    size_t len = 0;
#if defined(_MSC_VER)
    if (getenv_s(&len, buf, sizeof(buf), var) != 0 || len == 0) return -1;
#else
    const char* v = std::getenv(var);
    if (!v) return -1;
    std::snprintf(buf, sizeof(buf), "%s", v);
#endif
    return std::atoi(buf);
}

// Una variable de entorno entera, como texto (AutoPick solo lee numeros
// cortos). Para GRAV_PLACE y GRAV_CAM.
inline std::string EnvText(const char* var) {
#if defined(_MSC_VER)
    char* raw = nullptr;
    size_t len = 0;
    if (_dupenv_s(&raw, &len, var) != 0 || !raw) return {};
    std::string out(raw);
    std::free(raw);
    return out;
#else
    const char* v = std::getenv(var);
    return v ? v : "";
#endif
}

// --- Curvas ---
inline float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float SmoothStep(float t) {
    t = Saturate(t);
    return t * t * (3.0f - 2.0f * t);
}
inline float EaseOutCubic(float t) {
    t = 1.0f - Saturate(t);
    return 1.0f - t * t * t;
}
inline float EaseInOutCubic(float t) {
    t = Saturate(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
// Angulos en grados por el camino corto.
inline float LerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
    return a + d * t;
}
// Se acerca a `target` con una constante de tiempo (independiente del frame).
inline float Approach(float v, float target, float rate, float dt) { return target + (v - target) * std::exp(-rate * dt); }

// Un velo negro sobre todo (fundidos).
inline void Veil(UiDrawList& ui, float alpha) {
    if (alpha <= 0.001f) return;
    ui.AddRect(UiRect{0, 0, UiService::Width(), UiService::Height()}, Black(Saturate(alpha)));
}

// Una pista de teclas, abajo, tenue: el menu se explica sin manual.
inline void Hint(UiDrawList& ui, std::string_view text, float alpha = 1.0f) {
    const int s = std::max(1, TextScale() - 1);
    const float y = UiService::Height() - TextH(s) - 18.0f;
    TextCentered(ui, UiService::Width() * 0.5f, y + TextH(s) * 0.5f, text, Faint(alpha), s);
}

// Un aviso breve (p. ej. SALIR dentro del Editor).
inline void Toast(UiDrawList& ui, std::string_view text, float alpha) {
    if (alpha <= 0.01f || text.empty()) return;
    const int s = std::max(1, TextScale() - 1);
    const float w = TextW(text, s) + 32.0f, h = TextH(s) + 20.0f;
    const UiRect r{std::floor((UiService::Width() - w) * 0.5f), 28.0f, w, h};
    ui.AddRect(r, Black(0.85f * alpha));
    ui.AddRectOutline(r, Red(alpha), 1.0f);
    ui.AddTextCentered(r, text, White(alpha), s);
}

} // namespace gravitational::kit
