#pragma once

// DEL PIXEL A LA CELDA: que hay bajo el cursor (una pieza o un plano) y en que
// celda caeria una pieza nueva. Lo usan FigureBuilder (el raton) y PickCheck
// (la prueba, que proyecta una celda a la pantalla y pregunta lo mismo). Una
// sola cuenta: si la prueba tuviera la suya, probaria otra cosa.

#include "../Puzzle/BeamPath.h"

#include "Engine/Rendering/Camera.h"
#include "Engine/Scene/GameObject.h"
#include "Engine/Scene/Raycast.h"
#include "Engine/Scene/SceneService.h"

#include <cmath>
#include <vector>

namespace gravitational {

struct Pick {
    int figure = -1;  // la pieza bajo el cursor, o -1 (un plano)
    bool canPlace = false;
    Int3 cell;        // la celda pegada a la superficie del clic
    Int3 normal;      // la normal de esa superficie (hacia la celda)
};

inline Int3 DominantAxis(const Vec3f& v) {
    const float ax = std::fabs(v.x), ay = std::fabs(v.y), az = std::fabs(v.z);
    if (ax >= ay && ax >= az) return {v.x > 0 ? 1 : -1, 0, 0};
    if (ay >= az) return {0, v.y > 0 ? 1 : -1, 0};
    return {0, 0, v.z > 0 ? 1 : -1};
}

// El primer acierto que sea una pieza (hijo de `container`) o un plano (el
// objeto i de `planes` es el plano i del tablero: salida, llegada y los de
// portal, ver PlaneNodes.h); el fantasma y lo demas se atraviesan. `w` x `h`
// es el tamano de la imagen del juego, con la MISMA matriz con la que se
// dibujo (ver Raycast.h).
inline bool PickAt(const Board& b, const strace::GameObject& cam, const strace::GameObject* container,
                   const std::vector<const strace::GameObject*>& planes, float x, float y, float w, float h, Pick& out) {
    using namespace strace;
    if (!cam.camera || w <= 0.0f || h <= 0.0f) return false;
    const DirectX::XMMATRIX view = ComputeGameCameraView(cam);
    const DirectX::XMMATRIX proj = ComputeGameCameraProjection(*cam.camera, SceneService::Type(), w / h);
    Ray ray;
    if (!ScreenPointToRay(x, y, w, h, view * proj, ray)) return false;

    std::vector<RaycastHit> hits;
    SceneService::RaycastAll(ray, hits);
    for (const RaycastHit& hit : hits) {
        const Vec3f point{hit.point.x, hit.point.y, hit.point.z};
        if (container && hit.object->parent == container) {
            // El hijo i es la pieza i (el director los crea en ese orden).
            int index = -1;
            for (size_t i = 0; i < container->children.size(); ++i)
                if (container->children[i].get() == hit.object) index = static_cast<int>(i);
            if (index < 0 || index >= static_cast<int>(b.figures.size())) continue;
            out.figure = index;
            // La cara del clic: el eje dominante de la normal del acierto. La
            // celda nueva es la primera, saliendo por esa cara, que ya no es de
            // la pieza (una esfera de 3 ocupa tres de ancho).
            const Int3 n = DominantAxis({hit.normal.x, hit.normal.y, hit.normal.z});
            Int3 c = CellAt(b, point - ToF(n) * 0.25f);
            if (b.IndexAt(c) != index) c = b.figures[static_cast<size_t>(index)].cell;
            while (b.IndexAt(c) == index) c = c + n;
            out.cell = c;
            out.normal = n;
            out.canPlace = b.InBox(c) && !b.FigureAt(c);
            return true;
        }
        int plane = -1;
        for (size_t i = 0; i < planes.size() && i < b.planes.size(); ++i)
            if (planes[i] && planes[i] == hit.object) plane = static_cast<int>(i);
        if (plane >= 0) {
            // Del punto del plano (en el borde de la caja) a la celda de la
            // capa pegada a el: medio paso hacia adentro.
            const Int3 n = b.planes[static_cast<size_t>(plane)].normal;
            out.cell = CellAt(b, point - ToF(n) * 0.5f);
            out.normal = -n;
            out.canPlace = b.InBox(out.cell) && !b.FigureAt(out.cell);
            return true;
        }
    }
    return false;
}

} // namespace gravitational
