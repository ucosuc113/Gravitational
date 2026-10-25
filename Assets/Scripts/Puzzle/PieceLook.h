#pragma once

// COMO SE VE CADA PIEZA: que primitiva o malla, con que giro, con que material
// y que le pasa a su shader. Lo usan el director (las piezas del tablero) y
// FigureBuilder (el fantasma): las dos tienen que verse IGUAL, o el fantasma
// promete una postura que la pieza no tiene. Sin motor (los nombres de las
// primitivas van como numero; el director los traduce).

#include "Board.h"

#include <cstdio>
#include <string>

namespace gravitational {

enum class LookKind { Sphere, Cube, Torus, Pyramid, Mesh };

struct PieceLook {
    LookKind kind = LookKind::Sphere;
    const char* mesh = "";      // con kind == Mesh: la ruta del .strmesh
    float rotation[3] = {0, 0, 0}; // grados, en el orden del Transform del motor (x, y, z)
    float scale = 1.0f;
    bool pieceShader = false;   // Piece.hlsl (las nuevas) o Figure.hlsl (esferas y cubo)
    bool twoSided = false;      // laminas: se ven de los dos lados
    Rot frame;                  // el marco local en el mundo (para Piece.hlsl)
};

// Los angulos que ponen el marco `r` en el mundo, con la convencion del motor:
// Transform::rotation = (pitch, yaw, roll) en grados, y la matriz es
// XMMatrixRotationRollPitchYaw = Rz * Rx * Ry con vectores fila (ver
// SceneTransform.cpp). Son 90 grados por eje, asi que se BUSCA: 64
// combinaciones con enteros, en vez de despejar angulos con atan2 y redondear.
inline bool EulerOf(const Rot& r, float out[3]) {
    struct M {
        int m[3][3];
    };
    auto mul = [](const M& a, const M& b) {
        M c{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) c.m[i][j] += a.m[i][k] * b.m[k][j];
        return c;
    };
    const int cs[4] = {1, 0, -1, 0}, sn[4] = {0, 1, 0, -1};
    for (int p = 0; p < 4; ++p)
        for (int y = 0; y < 4; ++y)
            for (int z = 0; z < 4; ++z) {
                const M rx{{{1, 0, 0}, {0, cs[p], sn[p]}, {0, -sn[p], cs[p]}}};
                const M ry{{{cs[y], 0, -sn[y]}, {0, 1, 0}, {sn[y], 0, cs[y]}}};
                const M rz{{{cs[z], sn[z], 0}, {-sn[z], cs[z], 0}, {0, 0, 1}}};
                const M m = mul(mul(rz, rx), ry);
                const Int3 row[3] = {{m.m[0][0], m.m[0][1], m.m[0][2]}, {m.m[1][0], m.m[1][1], m.m[1][2]}, {m.m[2][0], m.m[2][1], m.m[2][2]}};
                if (row[0] == r.f && row[1] == r.u && row[2] == r.s) {
                    out[0] = 90.0f * p;
                    out[1] = 90.0f * y;
                    out[2] = 90.0f * z;
                    return true;
                }
            }
    out[0] = out[1] = out[2] = 0.0f;
    return false;
}

// El marco de una pieza que se gira por eje (cubo, toroide): su Y local es el eje.
inline Rot AxisFrame(int axis) {
    const Int3 u = axis == 0 ? kPosX : axis == 2 ? kPosZ : kPosY;
    return Orientation(OrientationOf(PerpOf(u, 0), u));
}

inline PieceLook LookOf(const Figure& f) {
    PieceLook l;
    l.frame = UsesOrient(f.shape) ? RotOf(f) : UsesAxis(f.shape) ? AxisFrame(f.axis) : Orientation(kIdentity);
    switch (f.shape) {
        case Shape::Sphere:
        case Shape::Sphere3:
        case Shape::Sphere5:
            l.kind = LookKind::Sphere;
            l.scale = static_cast<float>(Info(f.shape).size);
            break;
        case Shape::Cube: l.kind = LookKind::Cube; break;
        case Shape::Torus:
            l.kind = LookKind::Torus;
            l.pieceShader = true;
            break;
        case Shape::Pyramid:
            l.kind = LookKind::Pyramid;
            l.pieceShader = true;
            break;
        case Shape::Prism: l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/prism.strmesh", l.pieceShader = true; break;
        case Shape::Ramp: l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/ramp.strmesh", l.pieceShader = l.twoSided = true; break;
        case Shape::Twist: l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/twist.strmesh", l.pieceShader = l.twoSided = true; break;
        case Shape::Bend: l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/bend.strmesh", l.pieceShader = l.twoSided = true; break;
        case Shape::Wormhole:
            l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/wormhole.strmesh", l.pieceShader = l.twoSided = true;
            // Hundido en su plano: la lamina (el XZ local) acostada sobre el
            // plano y el embudo (el -Y local) hacia afuera de la caja.
            if (f.sink != Int3{0, 0, 0}) l.frame = Orientation(OrientationOf(PerpOf(-f.sink, 0), -f.sink));
            break;
        case Shape::BlackHole: l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/blackhole.strmesh", l.pieceShader = l.twoSided = true; break;
        case Shape::WhiteHole: l.kind = LookKind::Mesh, l.mesh = "Assets/Meshes/whitehole.strmesh", l.pieceShader = l.twoSided = true; break;
        default: break;
    }
    // La cara de la esfera y el cubo no gira con la pieza: su dibujo es del
    // mundo (la franja del cubo la pone el shader por el eje).
    if (l.kind != LookKind::Sphere && l.kind != LookKind::Cube) EulerOf(l.frame, l.rotation);
    return l;
}

// Para la Jerarquia y el log: "Rampa 2 0 1".
inline std::string NodeName(const Figure& f) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %d %d %d", Info(f.shape).label, f.cell.x, f.cell.y, f.cell.z);
    return buf;
}

} // namespace gravitational
