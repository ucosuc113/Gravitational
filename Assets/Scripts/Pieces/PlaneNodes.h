#pragma once

// LOS OBJETOS DE LOS PLANOS, en el orden de Board::planes: 0 salida, 1 llegada
// y de 2 en adelante los de portal. Los dos primeros son de la escena (campos
// del Inspector); los de portal los crea el director al cargar cada nivel,
// porque cada nivel trae los suyos.
//
// Por que una lista compartida y no campos: quien dibuja la luz (SurfaceLight),
// quien lee el clic (FigureBuilder) y la prueba del clic (PickCheck) necesitan
// "el objeto del plano i" para TODOS los planos, y los de portal no existen en
// la escena. Una lista que llena uno solo (el director, en el mismo momento en
// que acomoda los planos) es la manera de que los cuatro no discrepen. Es una
// estatica del Scripts.dll: los scripts de la partida la comparten.

#include "Engine/Scene/GameObject.h"

#include <vector>

namespace gravitational {

inline std::vector<strace::GameObject*>& PlaneNodes() {
    static std::vector<strace::GameObject*> nodes;
    return nodes;
}

// El indice de plano de un objeto, o -1.
inline int PlaneIndexOf(const strace::GameObject* node) {
    const std::vector<strace::GameObject*>& all = PlaneNodes();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i] && all[i] == node) return static_cast<int>(i);
    return -1;
}

} // namespace gravitational
