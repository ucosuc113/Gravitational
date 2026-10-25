#pragma once

// DEL RECORRIDO AL MUNDO: los tramos de LightTrace, en metros y en
// coordenadas del mundo, listos para que cada SUPERFICIE dibuje la parte de la
// luz que le toca.
//
// La luz no es un objeto: es un dibujo sobre las superficies. Es la idea del
// juego - una superficie 2D representa el espacio 3D, y lo que viaja por ese
// espacio viaja POR la superficie, no flotando en el vacio. Asi que cada tramo
// se describe como lo necesita el shader de su superficie:
//
//   plano   un segmento recto (de A a B) sobre el plano
//   figura  un arco de su geodesica: centro, cara de entrada, direccion y
//           cuartos de vuelta; el shader mide sobre la propia superficie a que
//           distancia esta cada pixel de ese arco y cuanto recorrio la luz ahi
//
// Los largos son EXACTOS (no de una polilinea): el shader los recalcula pixel a
// pixel con las mismas formulas (Figure.hlsl, Surface.hlsl), y si discreparan
// la punta de la luz saltaria al pasar de una superficie a otra.
//
// Tambien vive aqui el paso de la caja de celdas al mundo. Sin motor ni
// DirectXMath.

#include "LightTrace.h"

#include <cmath>
#include <vector>

namespace gravitational {

struct Vec3f {
    float x = 0, y = 0, z = 0;
};
inline Vec3f operator+(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3f operator-(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3f operator*(Vec3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Length(Vec3f a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline Vec3f ToF(const Int3& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }

inline constexpr float kPi = 3.14159265f;

// Cuantos tramos de luz puede dibujar UNA superficie (un plano o una pieza):
// los 8 float4 del material dan para dos (SurfaceLight.cpp). Un recorrido que
// le pase mas a una superficie se dibuja cortado - n10 terminaba con el tramo
// que entra al receptor invisible -, asi que es una invariante firme
// (TraceInvariants.h) y levellab rechaza las soluciones que la rompen. La
// Fase 3 de docs/arquitectura-luz.md la saca para los planos.
inline constexpr int kDrawSlots = 2;

// --- De la caja al mundo ---
//
// La caja esta alineada con el mundo y centrada en el origen (donde esta el
// Pivote de la camara). Que un plano sea pared, piso o techo es solo en que
// cara de la caja esta: no hay marcos girados que mantener.
inline Vec3f CellCenter(const Board& b, const Int3& c) {
    return {static_cast<float>(c.x) - (b.W - 1) * 0.5f, static_cast<float>(c.y) - (b.H - 1) * 0.5f,
            static_cast<float>(c.z) - (b.D - 1) * 0.5f};
}

// De un punto del mundo a su celda (redondeando).
inline Int3 CellAt(const Board& b, Vec3f p) {
    return {static_cast<int>(std::floor(p.x + (b.W - 1) * 0.5f + 0.5f)), static_cast<int>(std::floor(p.y + (b.H - 1) * 0.5f + 0.5f)),
            static_cast<int>(std::floor(p.z + (b.D - 1) * 0.5f + 0.5f))};
}

// El punto de contacto de una celda de la capa de un plano: el centro de su
// cara hacia ese plano.
inline Vec3f PlanePoint(const Board& b, int plane, const Int3& c) {
    return CellCenter(b, c) + ToF(b.planes[plane].normal) * 0.5f;
}

// El centro de una superficie (un plano del tablero, o una pared o una cara de
// bloque de la sala, que no son planos): el punto medio de los contactos de sus
// dos esquinas (un plano puede cubrir solo parte de su cara).
inline Vec3f FaceCenter(const Board& b, const Face& f) {
    const Vec3f n = ToF(f.normal) * 0.5f;
    return (CellCenter(b, f.lo) + n + CellCenter(b, f.hi) + n) * 0.5f;
}
inline Vec3f PlaneCenter(const Board& b, int plane) { return FaceCenter(b, b.planes[plane]); }

// Donde va el objeto de una pieza: el centro de su celda ancla, salvo el pozo
// de gusano, que esta HUNDIDO en su plano de portal - su lamina a ras del
// plano (un pelo hacia afuera: las lineas del plano quedan delante y no
// pelean con las de la lamina) y el embudo hacia afuera de la caja.
inline Vec3f FigurePos(const Board& b, const Figure& f) {
    const Vec3f c = CellCenter(b, f.cell);
    if (f.shape != Shape::Wormhole || f.sink == Int3{0, 0, 0}) return c;
    return c + ToF(f.sink) * 0.505f;
}

// --- Largos sobre las figuras ---
//
// Los calcula el trazado (LightTrace.h: TransferOf), que es donde vive cada
// regla; aqui solo se suman. Antes estaban aqui las formulas de la esfera y el
// cubo, y con doce piezas eso eran dos sitios que mantener iguales.
inline float SphereArc(float angle) { return angle * 0.5f; }

struct DrawHop {
    bool onPlane = true;
    // Plano
    int plane = 0;
    Vec3f a, b; // del punto a al b, en el mundo
    // Pieza
    Int3 cell;
    Shape shape = Shape::Sphere;
    Vec3f nIn, t, via; // en el mundo (via: ver Hop::via)
    int quarters = 0;
    float len = 0.0f;     // metros sobre esta pieza
    bool outMouth = false; // boca de salida de un gusano
    bool tube = false;     // toroide: la vuelta al tubo
    bool fromSeam = false, toSeam = false; // cubo: ver Hop
    // Distancia recorrida por la luz al empezar y al terminar este tramo
    float s0 = 0.0f, s1 = 0.0f;
};

struct BeamPath {
    std::vector<DrawHop> hops;
    std::vector<float> hopEnd; // = hops[i].s1: el boton PASO salta de uno a otro
    float length = 0.0f;
};

inline BeamPath BuildBeamPath(const Board& b, const Trace& tr) {
    BeamPath path;
    float s = 0.0f;
    for (size_t i = 0; i < tr.hops.size(); ++i) {
        const Hop& h = tr.hops[i];
        DrawHop d;
        d.s0 = s;
        if (h.kind == Hop::Kind::Plane) {
            d.onPlane = true;
            d.plane = h.plane;
            d.a = PlanePoint(b, h.plane, h.from);
            if (h.fromEdge) d.a = d.a - ToF(h.dir) * 0.5f; // llego por la arista: empieza en el borde
            d.b = PlanePoint(b, h.plane, h.to);
            if (h.toEdge) d.b = d.b + ToF(h.dir) * 0.5f; // hasta el borde: se pierde, o pasa al otro plano
            // Un pozo de gusano: el plano deja la luz en el BORDE del embudo
            // (el borde de su casilla) y el pozo la baja en espiral desde ahi
            // (Piece.hlsl). Hasta el centro de la casilla, el rayo cruzaba el
            // embudo flotando y se cortaba en el medio. Y al reves despues del
            // pozo de salida: el plano la toma en el borde por donde se va.
            const bool intoMouth = i + 1 < tr.hops.size() && tr.hops[i + 1].kind == Hop::Kind::Figure &&
                                   tr.hops[i + 1].shape == Shape::Wormhole && !tr.hops[i + 1].outMouth;
            const bool fromMouth = i > 0 && tr.hops[i - 1].outMouth;
            if (intoMouth && !h.toEdge) d.b = d.b - ToF(h.dir) * 0.5f;
            if (fromMouth && !h.fromEdge) d.a = d.a + ToF(h.dir) * 0.5f;
            s += Length(d.b - d.a);
        } else {
            d.onPlane = false;
            d.cell = h.cell;
            d.shape = h.shape;
            d.nIn = ToF(h.nIn);
            d.t = ToF(h.t);
            d.via = ToF(h.via);
            d.quarters = h.quarters;
            d.len = h.len;
            d.outMouth = h.outMouth;
            d.tube = h.tube;
            d.fromSeam = h.fromSeam;
            d.toSeam = h.toSeam;
            s += h.len;
        }
        d.s1 = s;
        path.hops.push_back(d);
        path.hopEnd.push_back(s);
    }
    path.length = s;
    return path;
}

} // namespace gravitational
