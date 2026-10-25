#include "../Flow/GameFlow.h"
#include "../Flow/MenuKit.h"

#include "Engine/Diagnostics/Log.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scripting/IScript.h"
#include "Engine/Scripting/ScriptFields.h"
#include "Engine/Scripting/ScriptRegistry.h"
#include "Engine/Text.h"
#include "Engine/Input/InputService.h"
#include "Engine/Scene/SceneTransform.h"
#include "Engine/UI/SceneUi.h"
#include <algorithm>
#include <cmath>
#include <sstream>

#include <string>

using namespace strace;
using gravitational::Flow;

class OrbitCamera : public IScript {
public:
    ObjectField pivot{this, "pivot"};
    FloatField distance{this, "distance", 10.0f};
    FloatField minDistance{this, "minDistance", 2.5f};
    FloatField maxDistance{this, "maxDistance", 40.0f};
    FloatField startYaw{this, "startYaw", 0.0f};
    FloatField startPitch{this, "startPitch", 30.0f};
    FloatField sensitivity{this, "sensitivity", 0.25f};
    // El zoom es PROPORCIONAL: cada paso acerca o aleja la misma fraccion de la
    // distancia, asi se siente igual de cerca que de lejos (sumar metros fijos
    // era nada estando lejos y un salto estando cerca). zoomSpeed es la
    // fraccion por muesca de rueda (las teclas son ocho muescas por segundo).
    FloatField zoomSpeed{this, "zoomSpeed", 0.12f};

    void OnStart() override {
        GameObject* p = pivot.Get();
        if (!p) {
            Log::Warn(L"OrbitCamera: no hay pivote (arrastra un objeto al campo 'pivot' en el Inspector)");
        } else {
            Log::Info(L"OrbitCamera: pivote '" + Utf8ToWide(p->name) + L"', distancia " + std::to_wstring(distance.Get()));
        }
        m_yaw = startYaw.Get();
        m_pitch = startPitch.Get();
        m_distance = distance.Get();
        // GRAV_CAM="yaw pitch distancia x y z": un encuadre FIJO alrededor de
        // ese punto del mundo, en metros (sin la escala del nivel). Para
        // capturar de cerca una pieza en una corrida automatica, que no tiene
        // raton para orbitar. Sin la variable, nada.
        if (const std::string cam = gravitational::kit::EnvText("GRAV_CAM"); !cam.empty()) {
            std::istringstream in(cam);
            float v[6] = {};
            int n = 0;
            while (n < 6 && in >> v[n]) ++n;
            if (n == 6) {
                m_fixed = true;
                m_fixedView[0] = v[0], m_fixedView[1] = v[1], m_fixedView[2] = v[2];
                m_center = {v[3], v[4], v[5]};
            } else {
                Log::Warn(L"GRAV_CAM: se esperaban seis numeros (yaw pitch distancia x y z)");
            }
        }
        
        InputActions& in = InputService::Actions();
        in.BindMouseButton("orbitar", MouseButton::Right);
        // El zoom tambien por teclado y con el boton del medio, no solo con la
        // rueda: hasta el 22/10/2026 la rueda no le llegaba al juego (el motor
        // no la cableaba, ver Engine/src/Platform/Input.cpp), y un zoom que solo
        // existia en la rueda era un zoom que no existia. Ahora llega, y las
        // teclas quedan para quien juega con touchpad.
        in.ClearAction("zoom_acercar");
        in.BindKey("zoom_acercar", Key::E);
        in.BindKey("zoom_acercar", Key::Equals);
        in.ClearAction("zoom_alejar");
        in.BindKey("zoom_alejar", Key::Q);
        in.BindKey("zoom_alejar", Key::Minus);
        in.ClearAction("zoom_arrastrar");
        in.BindMouseButton("zoom_arrastrar", MouseButton::Middle);
    }

    void OnUpdate(float deltaTime) override {
        // mi vrg en el editor
        GameObject* p = pivot.Get();
        if (!p) return;
        
        InputActions& in = InputService::Actions();

        // Con un menu (Flow/GameFlow.h): publicar donde empieza la orbita, para
        // que el menu le entregue la camara ahi sin un salto, y no tocarla
        // mientras el menu la usa.
        auto& flow = Flow();
        PublishHome(*p);
        if (flow.resetOrbit) {
            flow.resetOrbit = false;
            m_yaw = startYaw.Get();
            m_pitch = startPitch.Get();
            m_distance = distance.Get();
            m_dragging = false;
        }
        if (m_fixed) {
            m_dragging = false;
            m_yaw = m_fixedView[0], m_pitch = m_fixedView[1], m_distance = m_fixedView[2];
        }
        if (!flow.InGame()) return;
        const float sens = sensitivity.Get() * (flow.hasMenu ? flow.Sensitivity() : 1.0f);
        const float pitchSign = flow.settings.invertOrbit ? -1.0f : 1.0f;

        if (in.Pressed("orbitar") && !SceneUi::PointerOverUi()) m_dragging = true;
        if (!in.Held("orbitar")) m_dragging = false;

        // cosa del click derecho orbitar alrededor de punto central XD
        // Sin limite vertical (pedido el 22/10/2026): la orbita da la vuelta
        // entera. Pasando por arriba la camara queda de cabeza, y entonces
        // "a la derecha" en pantalla es girar al reves: el arrastre horizontal
        // se invierte para que la mano siga sintiendo lo mismo.
        if (m_dragging){
            const float upright = std::cos(m_pitch * 3.141592f / 180.0f) >= 0.0f ? 1.0f : -1.0f;
            m_yaw += in.CursorDeltaX() * sens * upright;
            m_pitch += in.CursorDeltaY() * sens * pitchSign;
            m_pitch = std::remainder(m_pitch, 360.0f); // queda en -180..180, sin acumular
        } //pishi if to' complicao

        // El zoom: rueda, teclas Q/E o -/+, y arrastrar con
        // el boton del medio (hacia arriba acerca).
        float zoomSteps = 0.0f;
        if (!SceneUi::PointerOverUi()) zoomSteps -= in.WheelDelta(); // positivo = rueda hacia adelante = acercar
        if (in.Held("zoom_acercar")) zoomSteps -= 8.0f * deltaTime; // ocho muescas por segundo
        if (in.Held("zoom_alejar")) zoomSteps += 8.0f * deltaTime;
        if (in.Held("zoom_arrastrar")) zoomSteps += in.CursorDeltaY() * 0.05f;
        m_distance *= std::pow(1.0f + zoomSpeed.Get(), zoomSteps);
        m_distance = std::clamp(m_distance, minDistance.Get(), maxDistance.Get());
        if (m_fixed) m_distance = m_fixedView[2];
        
        const float KDegToRad = 3.141592f / 180.0f;
        const float yaw = m_yaw * KDegToRad;
        const float pitch = m_pitch * KDegToRad;
        const float cp = std::cos(pitch);
        
        // La distancia se multiplica por el tamano del nivel (cameraScale, lo
        // pone el director): un tablero de 7x5x5 no entra en la de 3x3x3, y
        // asi el zoom que eligio la persona se conserva de un nivel a otro.
        float dist = m_distance * flow.cameraScale;
        DirectX::XMFLOAT3 c = ComputeWorldPosition(*p);
        if (m_fixed) {
            dist = m_distance;
            c = m_center;
        }
        gameObject->transform.position[0] = c.x - dist * cp * std::sin(yaw);
        gameObject->transform.position[1] = c.y + dist * std::sin(pitch);
        gameObject->transform.position[2] = c.z - dist * cp * std::cos(yaw);
        
        // ahora el pivote >:3
        
        gameObject->transform.rotation[0] = m_pitch;
        gameObject->transform.rotation[1] = m_yaw;
        gameObject->transform.rotation[2] = 0.0f;
    }

private:
    // La misma cuenta que OnUpdate, con los valores de inicio.
    void PublishHome(const GameObject& p) {
        auto& flow = Flow();
        const float k = 3.141592f / 180.0f;
        const float yaw = startYaw.Get() * k, pitch = startPitch.Get() * k, d = distance.Get() * flow.cameraScale;
        const DirectX::XMFLOAT3 c = ComputeWorldPosition(p);
        flow.homePos[0] = c.x - d * std::cos(pitch) * std::sin(yaw);
        flow.homePos[1] = c.y + d * std::sin(pitch);
        flow.homePos[2] = c.z - d * std::cos(pitch) * std::cos(yaw);
        flow.homeRot[0] = startPitch.Get();
        flow.homeRot[1] = startYaw.Get();
        flow.homeRot[2] = 0.0f;
        flow.homeKnown = true;
    }

    float m_yaw = 0.0f;
    float m_pitch = 30.0f;
    float m_distance = 10.0f;
    bool m_dragging = false;
    // GRAV_CAM: el encuadre fijo (yaw, pitch, distancia) y el punto que mira.
    bool m_fixed = false;
    float m_fixedView[3] = {};
    DirectX::XMFLOAT3 m_center = {};
};

STRACE_REGISTER_SCRIPT(OrbitCamera)
