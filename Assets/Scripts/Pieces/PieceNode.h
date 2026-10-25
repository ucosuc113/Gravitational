#pragma once

// UNA PIEZA COMO OBJETO DE LA ESCENA: lo de PieceLook.h llevado a un
// GameObject (primitiva o malla, giro, escala, caras). Lo usan el director
// (las piezas) y FigureBuilder (el fantasma): si cada uno lo armara por su
// cuenta, el fantasma podria prometer una postura que la pieza no tiene.

#include "../Puzzle/PieceLook.h"

#include "Engine/Scene/GameObject.h"
#include "Engine/Text.h"

namespace gravitational {

inline void ApplyLook(strace::GameObject& node, const PieceLook& look) {
    using namespace strace;
    node.meshPath.clear();
    switch (look.kind) {
        case LookKind::Sphere: node.primitive = PrimitiveType::Sphere; break;
        case LookKind::Cube: node.primitive = PrimitiveType::Cube; break;
        case LookKind::Torus: node.primitive = PrimitiveType::Torus; break;
        case LookKind::Pyramid: node.primitive = PrimitiveType::Pyramid; break;
        case LookKind::Mesh:
            node.primitive = PrimitiveType::Empty;
            node.meshPath = Utf8ToWide(look.mesh);
            break;
    }
    for (int i = 0; i < 3; ++i) {
        node.transform.rotation[i] = look.rotation[i];
        node.transform.scale[i] = look.scale;
    }
    // Sin descarte de caras, en TODAS. Antes solo las laminas (rampas, pozos)
    // se dibujaban por los dos lados, porque una pieza cerrada "no necesita la
    // cara de atras". Pero algunas piezas perdian caras de verdad: los giros y
    // las escalas de cada postura (y las mallas generadas) dejan triangulos con
    // el orden al reves, y el descarte se los llevaba. Dibujar las dos caras de
    // un puñado de piezas no cuesta nada medible.
    node.faceCulling = false;
    node.castShadows = false;
}

} // namespace gravitational
