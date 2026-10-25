#pragma once

// EL ESPACIO DEL PUZZLE: posiciones enteras en la cuadricula 3D y las seis
// direcciones de los ejes. Sin motor y sin DirectXMath: la luz se decide con
// enteros, y solo al dibujarla pasa a metros (ver Board::CellCenter).
//
// Con enteros, "la cara opuesta", "girar" y "es perpendicular" son exactos.
// Con flotantes, la luz que da tres cuartos de vuelta saldria por una cara a
// 1e-7 de la que tocaba, y decidir con un umbral cual toca es la clase de
// error que aparece una vez cada cien partidas.

namespace gravitational {

struct Int3 {
    int x = 0, y = 0, z = 0;
    bool operator==(const Int3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Int3& o) const { return !(*this == o); }
    Int3 operator+(const Int3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Int3 operator-(const Int3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Int3 operator-() const { return {-x, -y, -z}; }
};

inline int Dot(const Int3& a, const Int3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Int3 Cross(const Int3& a, const Int3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Las seis direcciones de los ejes, y su indice 0..5 (para las tablas de
// estados visitados).
inline constexpr Int3 kPosX{1, 0, 0}, kNegX{-1, 0, 0}, kPosY{0, 1, 0}, kNegY{0, -1, 0}, kPosZ{0, 0, 1}, kNegZ{0, 0, -1};

inline int DirIndex(const Int3& d) {
    if (d.x == 1) return 0;
    if (d.x == -1) return 1;
    if (d.y == 1) return 2;
    if (d.y == -1) return 3;
    if (d.z == 1) return 4;
    return 5;
}

// El eje (0 = X, 1 = Y, 2 = Z) de una direccion, sin el signo.
inline int AxisOf(const Int3& d) { return d.x != 0 ? 0 : d.y != 0 ? 1 : 2; }

inline Int3 operator*(const Int3& v, int s) { return {v.x * s, v.y * s, v.z * s}; }

inline constexpr Int3 kDirs[6] = {kPosX, kNegX, kPosY, kNegY, kPosZ, kNegZ};

// --- LA ORIENTACION DE UNA PIEZA: las 24 rotaciones de la cuadricula ---
//
// Una pieza se describe en su marco LOCAL (su malla, sus puertos, sus celdas)
// y una rotacion la pone en el mundo: `f`, `u` y `s` son a donde van los ejes
// locales X, Y y Z. Solo hay 24 (las del cubo), y con enteros "rotar" es
// exacto: una rampa girada no queda a 1e-7 de tocar la pared, la toca.
//
// El indice es (u, giro alrededor de u): o = indiceDe(u) * 4 + k. Ordenado
// asi, sumar 1 a k gira la pieza SOBRE la cara en la que esta apoyada (su -Y
// local), que es lo que uno espera al rotar una rampa puesta en el piso; el
// indice 8 es la identidad (u = +Y, f = +X).
//
// `s = Cross(f, u)` y no al reves: con esa cuenta la terna es una rotacion
// propia (determinante +1), y es la misma cuenta que hace el shader con
// cross(): no hay una tercera fila que pasar ni que pueda discrepar.
struct Rot {
    Int3 f = kPosX, u = kPosY, s = kPosZ;
};

inline constexpr int kOrientations = 24;
inline constexpr int kIdentity = 8;

// Las cuatro direcciones perpendiculares a d, en el orden de kDirs.
inline Int3 PerpOf(const Int3& d, int k) {
    int n = 0;
    for (const Int3& c : kDirs)
        if (Dot(c, d) == 0 && n++ == k) return c;
    return kPosX;
}

inline Rot Orientation(int o) {
    o = ((o % kOrientations) + kOrientations) % kOrientations;
    Rot r;
    r.u = kDirs[o / 4];
    r.f = PerpOf(r.u, o % 4);
    r.s = Cross(r.f, r.u);
    return r;
}

// La orientacion con esos f y u (perpendiculares), o -1.
inline int OrientationOf(const Int3& f, const Int3& u) {
    for (int o = 0; o < kOrientations; ++o) {
        const Rot r = Orientation(o);
        if (r.f == f && r.u == u) return o;
    }
    return -1;
}

inline Int3 ToWorld(const Rot& r, const Int3& v) { return r.f * v.x + r.u * v.y + r.s * v.z; }
inline Int3 ToLocal(const Rot& r, const Int3& w) { return {Dot(w, r.f), Dot(w, r.u), Dot(w, r.s)}; }

} // namespace gravitational
