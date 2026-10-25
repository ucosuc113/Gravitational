#pragma once

// EL TABLERO 3D: la caja de la cuadricula, los dos planos, la fuente, el
// receptor, las piezas y el inventario. Se lee del formato de docs/reglas.md.
// Sin motor.
//
// La cuadricula es una CAJA de W x H x D celdas (x = 0..W-1 a la derecha,
// y = 0..H-1 hacia arriba, z = 0..D-1 hacia el fondo), alineada con el mundo.
// Cada plano es UNA CARA de esa caja, elegida por separado: la salida puede ser
// el piso y la llegada la pared derecha, o las dos paredes, o piso y techo.
//
// Una celda de un plano se nombra por la celda de la caja que lo toca (la de la
// capa pegada a esa cara): el contacto es el centro de esa cara de la celda.
//
// Un plano NO tiene que cubrir su cara entera: es un rectangulo de celdas
// dentro de ella (`lo`..`hi`). La luz pasa de un plano a otro por la arista
// donde se tocan, asi que la salida y la llegada tienen que estar SEPARADAS
// POR AIRE.
//
// Ademas de esos dos puede haber PLANOS DE PORTAL (23/10/2026): rectangulos en
// las caras como los otros, por los que la luz corre igual, y que son el UNICO
// sitio donde va un gusano (la regla, en LightTrace.h).
//
// --- Las piezas: celdas y PUERTOS ---
//
// Al principio habia dos figuras de una celda (esfera y cubo) y las reglas
// decian "toca por las seis caras". Con piezas de varias celdas (una esfera de
// 3, una rampa de 2x2) y piezas que NO tocan por todas sus caras (el toroide
// por su agujero, la piramide por sus lados inclinados), "toca" pasa a ser un
// dato de cada pieza: un PUERTO es una cara de una de sus celdas donde su
// superficie llega al centro de esa cara. Solo ahi se tocan dos superficies.
//
// Lo que no es puerto no toca: la luz que corre por el piso por debajo del ala
// de una esfera de 3 sigue de largo, y solo entra por el punto donde la esfera
// apoya. Es lo que se ve.
//
// Que hace la luz DENTRO de una pieza esta en LightTrace.h (Transfer); aqui
// solo donde esta, que ocupa y por donde toca.

#include "Space.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace gravitational {

// El orden importa: es el indice del inventario y el numero que viaja al
// shader. Las dos primeras son las de siempre.
enum class Shape {
    Sphere,    // 1x1x1, circulo maximo
    Cube,      // 1x1x1, cinturon alrededor de su eje
    Sphere3,   // 3x3x3, la misma regla que la esfera, mas larga
    Sphere5,   // 5x5x5
    Torus,     // 1x1x1, conduce alrededor de su eje; a lo largo del eje da la vuelta al tubo
    Prism,     // 1x1x1, cuna: dos caras que tocan y una hipotenusa que no
    Pyramid,   // 1x1x1, base y vertice: une dos caras opuestas
    Ramp,      // 2x2x1, cuarto de tubo: del piso a la pared, la luz sube
    Twist,     // 3x1x1, cinta torcida: del piso a la pared, la luz sigue recta
    Bend,      // 1x1x2, curva peraltada: del piso a la pared, la luz dobla a la izquierda
    Wormhole,  // de a pares, HUNDIDO en un plano de portal: lo que cae por un pozo sale por el otro
    BlackHole, // obstaculo: la luz que lo toca cae y no sale
    WhiteHole, // obstaculo: la luz que lo toca rebota por donde vino
    Count
};
inline constexpr int kShapeCount = static_cast<int>(Shape::Count);

struct ShapeInfo {
    const char* key;   // en el formato de nivel
    const char* label; // en el HUD (la fuente es ASCII: sin tildes)
    bool placeable;    // el jugador la puede tener en el inventario
    int size;          // lado, para las esferas (1 las demas)
};

inline const ShapeInfo& Info(Shape s) {
    static const ShapeInfo kInfo[kShapeCount] = {
        {"sphere", "SPHERE", true, 1},          {"cube", "CUBE", true, 1},         {"sphere3", "SPHERE 3", true, 3},
        {"sphere5", "SPHERE 5", true, 5},       {"torus", "TORUS", true, 1},     {"prism", "WEDGE", true, 1},
        {"pyramid", "PYRAMID", true, 1},       {"ramp", "RAMP", true, 1},        {"twist", "TWIST", true, 1},
        {"bend", "CURVE", true, 1},             {"wormhole", "WORMHOLE", true, 1},   {"blackhole", "BLACK HOLE", false, 1},
        {"whitehole", "WHITE HOLE", false, 1},
    };
    return kInfo[static_cast<int>(s)];
}

// El eje con sentido del cubo: 0..2 = +X +Y +Z, 3..5 = -X -Y -Z.
inline Int3 CubeAxis(int axis) {
    const Int3 a = axis % 3 == 0 ? kPosX : axis % 3 == 1 ? kPosY : kPosZ;
    return axis >= 3 ? -a : a;
}

inline bool IsSphere(Shape s) { return s == Shape::Sphere || s == Shape::Sphere3 || s == Shape::Sphere5; }
// El cubo y el toroide se giran por eje (0..2); el resto que tiene orientacion,
// por las 24 de Space.h.
inline bool UsesAxis(Shape s) { return s == Shape::Cube || s == Shape::Torus; }
inline bool UsesOrient(Shape s) {
    return s == Shape::Prism || s == Shape::Pyramid || s == Shape::Ramp || s == Shape::Twist || s == Shape::Bend || s == Shape::BlackHole ||
           s == Shape::WhiteHole;
}

struct Figure {
    Shape shape = Shape::Sphere;
    Int3 cell;               // el ANCLA: el centro en las esferas, la celda (0,0,0) local en las demas
    int axis = 1;            // toroide: 0 X, 1 Y, 2 Z; cubo: ademas el sentido (3..5 = -X -Y -Z, ver CubeAxis)
    int orient = kIdentity;  // las demas con orientacion
    bool fixed = false;      // viene con el nivel: no se quita ni se rota
    int pair = -1;           // agujero de gusano: las dos bocas comparten numero
    // Gusano: la normal (hacia AFUERA de la caja) del plano de portal en el que
    // esta hundido. Cero = todavia no se sabe (una linea ";! place" no la dice:
    // la deduce Board::Place del plano de portal que cubre la celda).
    Int3 sink{0, 0, 0};
    // Donde la puso el jugador: la celda pegada a la superficie del clic y la
    // normal de esa superficie. Rotar busca solo las posturas que siguen
    // tocandola (ver Board::PlacementsFor): girar una rampa no la despega del piso.
    bool hasContact = false;
    Int3 contact, contactN;
};

// --- Lo que ocupa y por donde toca cada pieza, en su marco LOCAL ---

struct LocalPort {
    Int3 cell, normal;
};

// Las celdas que ocupa, relativas al ancla (y antes de rotar).
inline std::vector<Int3> LocalCells(Shape s) {
    std::vector<Int3> out;
    const int h = (Info(s).size - 1) / 2;
    switch (s) {
        case Shape::Sphere3:
        case Shape::Sphere5:
            for (int x = -h; x <= h; ++x)
                for (int y = -h; y <= h; ++y)
                    for (int z = -h; z <= h; ++z) out.push_back({x, y, z});
            break;
        case Shape::Ramp: out = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}; break;
        case Shape::Twist: out = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}; break;
        case Shape::Bend: out = {{0, 0, 0}, {0, 0, 1}}; break;
        default: out = {{0, 0, 0}}; break;
    }
    return out;
}

// Los dos puertos de las piezas de recorrido fijo (rampa, torsion, curva): la
// ENTRADA (siempre por su -Y local, apoyada) y la salida. La luz los recorre
// en los dos sentidos.
//   rampa    entra yendo a +X; sale por la cara +X de (1,1,0) subiendo (+Y)
//   torsion  entra yendo a +X; sale por la cara +Z (la izquierda) de (2,0,0), yendo a +X
//   curva    entra yendo a +X; sale por la cara +X de (0,0,1), yendo a +Z (doblo a la izquierda)
// "Izquierda" es +Z: mirando hacia +X con +Y arriba, en este mundo zurdo, +Z
// queda a la izquierda.
struct PathPiece {
    LocalPort in, out;
    Int3 inDir, outDir;  // la direccion de la luz al entrar por `in` y al salir por `out`
};
inline PathPiece PathOf(Shape s) {
    switch (s) {
        case Shape::Ramp: return {{{0, 0, 0}, kNegY}, {{1, 1, 0}, kPosX}, kPosX, kPosY};
        case Shape::Twist: return {{{0, 0, 0}, kNegY}, {{2, 0, 0}, kPosZ}, kPosX, kPosX};
        default: return {{{0, 0, 0}, kNegY}, {{0, 0, 1}, kPosX}, kPosX, kPosZ}; // Bend
    }
}
inline bool IsPathPiece(Shape s) { return s == Shape::Ramp || s == Shape::Twist || s == Shape::Bend; }

inline Rot RotOf(const Figure& f) { return Orientation(f.orient); }

// Las celdas del mundo que ocupa la pieza.
inline std::vector<Int3> WorldCells(const Figure& f) {
    std::vector<Int3> out;
    const Rot r = RotOf(f);
    for (const Int3& c : LocalCells(f.shape)) out.push_back(f.cell + (UsesOrient(f.shape) ? ToWorld(r, c) : c));
    return out;
}

// ¿La superficie de la pieza llega al centro de la cara `normal` de la celda
// `cell`? (las dos en el mundo)
inline bool HasPort(const Figure& f, const Int3& cell, const Int3& normal) {
    switch (f.shape) {
        case Shape::Sphere:
        case Shape::Sphere3:
        case Shape::Sphere5: {
            const int h = (Info(f.shape).size - 1) / 2;
            return cell == f.cell + normal * h;
        }
        case Shape::Torus: return cell == f.cell && AxisOf(normal) != f.axis % 3; // por el agujero no toca
        case Shape::Pyramid: {
            if (cell != f.cell) return false;
            const Int3 n = ToLocal(RotOf(f), normal);
            return n == kPosY || n == kNegY; // base y vertice; los lados inclinados no llegan al centro
        }
        case Shape::Prism: {
            if (cell != f.cell) return false;
            const Int3 n = ToLocal(RotOf(f), normal);
            return n == kNegX || n == kNegY; // los dos catetos; la hipotenusa no toca nada de la cuadricula
        }
        case Shape::Ramp:
        case Shape::Twist:
        case Shape::Bend: {
            const Rot r = RotOf(f);
            const Int3 c = ToLocal(r, cell - f.cell), n = ToLocal(r, normal);
            const PathPiece p = PathOf(f.shape);
            return (c == p.in.cell && n == p.in.normal) || (c == p.out.cell && n == p.out.normal);
        }
        // El gusano es un POZO en el plano de portal: solo lo toca la luz que
        // corre por ESE plano y llega a su casilla. Una pieza de al lado no le
        // puede pasar la luz - si pudiera, el gusano volveria a ser una pieza
        // que se pone en cualquier parte, que es justo lo que se saco.
        case Shape::Wormhole: return cell == f.cell && normal == f.sink && f.sink != Int3{0, 0, 0};
        default: return cell == f.cell; // cubo, agujeros: las seis caras
    }
}

// Un plano: un rectangulo de celdas (inclusivo) y la cara de esas celdas en la
// que esta, por su `normal` (de las celdas hacia el plano: un piso es (0,-1,0)).
// La luz corre del lado de esas celdas. `whole` = la cara entera de la caja, que
// es lo que da un nivel que no dice nada.
//
// PLANOS FLOTANTES (24/10/2026). Hasta aqui un plano era siempre una cara de la
// caja, y todos los niveles tenian el mismo esqueleto: una caja con dos
// rectangulos en sus caras. Un plano no es una pared ni un piso: es una
// dimension espacial, y flota. Ahora puede estar en CUALQUIER lugar de la caja
// y en cualquier orientacion - horizontal a media altura, vertical en el
// medio, escalonado, en L -: el rectangulo dice en que celdas y la normal de
// que lado de ellas. Lo que no es plano es vacio, como siempre. De un plano a
// otro la luz pasa por sus aristas (TraceLight): concava (otro plano se le
// levanta enfrente), a ras (otro sigue en el mismo nivel) o convexa (otro
// cuelga del borde, del otro lado).
struct Face {
    Int3 normal = kNegX;
    Int3 lo{0, 0, 0}, hi{0, 0, 0};
    bool whole = true;
    bool portal = false; // plano de portal: ahi (y solo ahi) se hunden los gusanos
    bool panel = false;  // un plano mas, ni salida ni llegada ("panel" en el nivel)
};

class Board {
public:
    int W = 3, H = 3, D = 3;
    // 0 salida, 1 llegada, y de 2 en adelante los de mas (de portal y paneles).
    // Un vector y no un arreglo de dos: todo lo que recorre "los planos" (la luz
    // por la arista, el dibujo, el clic) los trata a todos igual.
    std::vector<Face> planes{Face{kNegX}, Face{kPosX}};
    Int3 sourceCell{0, 1, 1};            // una celda del plano de salida
    Int3 sourceDir = kPosY;              // en ese plano (perpendicular a su normal)
    Int3 receiverCell{2, 1, 1};          // una celda del plano de llegada
    int inventory[kShapeCount] = {};     // por Shape (el gusano cuenta BOCAS)
    std::vector<Figure> figures;
    std::string title;                   // "title ..." del nivel (opcional; el HUD lo muestra)
    std::string hint;                    // "hint ..." (opcional): una linea que ayuda
    int rules = 1;                       // "rules N": la version de las reglas con que se hizo (kRulesVersion en LightTrace.h)

    bool InBox(const Int3& c) const { return c.x >= 0 && c.x < W && c.y >= 0 && c.y < H && c.z >= 0 && c.z < D; }

    // La celda es parte del plano: en la caja y dentro de su rectangulo (la
    // normal ya esta en el rectangulo: es plano en ese eje).
    bool InPlane(const Int3& c, const Face& f) const {
        return InBox(c) && c.x >= f.lo.x && c.x <= f.hi.x && c.y >= f.lo.y && c.y <= f.hi.y && c.z >= f.lo.z && c.z <= f.hi.z;
    }

    // El plano (0 o 1) sobre la cara con esa normal que cubre esa celda, o -1.
    int PlaneOn(const Int3& normal, const Int3& cell) const {
        for (size_t i = 0; i < planes.size(); ++i)
            if (planes[i].normal == normal && InPlane(cell, planes[i])) return static_cast<int>(i);
        return -1;
    }

    // ¿La cara `normal` de la celda es un plano de portal? (donde puede ir un pozo)
    bool PortalAt(const Int3& normal, const Int3& cell) const {
        const int p = PlaneOn(normal, cell);
        return p >= 0 && planes[static_cast<size_t>(p)].portal;
    }

    // La normal del primer plano de portal que cubre la celda, o cero. Una
    // celda de la arista de una L esta en dos: gana el primero del nivel (quien
    // quiera el otro lo pone con el clic, que sabe que cara se toco).
    Int3 PortalSinkFor(const Int3& cell) const {
        for (const Face& f : planes)
            if (f.portal && InPlane(cell, f)) return f.normal;
        return {0, 0, 0};
    }

    bool PlanesTouch() const {
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
                for (int z = 0; z < D; ++z)
                    if (InPlane({x, y, z}, planes[0]) && InPlane({x, y, z}, planes[1])) return true;
        return false;
    }

    void FillWholeFaces() {
        for (Face& f : planes) {
            if (!f.whole) continue;
            f.lo = {f.normal.x > 0 ? W - 1 : 0, f.normal.y > 0 ? H - 1 : 0, f.normal.z > 0 ? D - 1 : 0};
            f.hi = {f.normal.x < 0 ? 0 : W - 1, f.normal.y < 0 ? 0 : H - 1, f.normal.z < 0 ? 0 : D - 1};
        }
    }

    // La pieza que OCUPA la celda (cualquiera de las suyas), o nullptr.
    int IndexAt(const Int3& c) const {
        for (size_t i = 0; i < figures.size(); ++i)
            for (const Int3& w : WorldCells(figures[i]))
                if (w == c) return static_cast<int>(i);
        return -1;
    }
    const Figure* FigureAt(const Int3& c) const {
        const int i = IndexAt(c);
        return i < 0 ? nullptr : &figures[static_cast<size_t>(i)];
    }
    Figure* FigureAt(const Int3& c) {
        const int i = IndexAt(c);
        return i < 0 ? nullptr : &figures[static_cast<size_t>(i)];
    }

    // La pieza cuya superficie llega al centro de esa cara de esa celda (la que
    // la luz tocaria ahi), o nullptr.
    const Figure* PortAt(const Int3& c, const Int3& normal) const {
        const Figure* f = FigureAt(c);
        return f && HasPort(*f, c, normal) ? f : nullptr;
    }

    // La otra boca de un gusano.
    const Figure* Twin(const Figure& f) const {
        if (f.shape != Shape::Wormhole || f.pair < 0) return nullptr;
        for (const Figure& o : figures)
            if (&o != &f && o.shape == Shape::Wormhole && o.pair == f.pair) return &o;
        return nullptr;
    }

    // Cabe: todas sus celdas dentro de la caja y libres (sin contar a `ignore`),
    // y ninguna sobre la FUENTE: una pieza ahi tapa la marca de donde sale la
    // luz, y el resolvedor encontraba "atajos" que la usaban de espejo. Sobre
    // el receptor si se puede (el tutorial 1 termina con una esfera encima).
    bool Fits(const Figure& f, int ignore = -1) const {
        for (const Int3& c : WorldCells(f)) {
            if (!InBox(c) || c == sourceCell) return false;
            if (f.shape == Shape::Wormhole && !PortalAt(f.sink, c)) return false; // un pozo, solo en un plano de portal
            const int i = IndexAt(c);
            if (i >= 0 && i != ignore) return false;
        }
        return true;
    }

    // --- Donde va una pieza nueva ---
    //
    // `p` es la celda pegada a la superficie del clic y `n` la normal de esa
    // superficie (de la superficie hacia p). La pieza tiene que TOCARLA: tener
    // un puerto en (p, -n). Devuelve todas las posturas distintas que lo
    // cumplen y caben, en el orden en que ROTAR las recorre; la primera es la
    // que se pone.
    //
    // Por que asi y no "en p con su orientacion de siempre": una rampa puesta
    // con la entrada en el aire no hace nada, y el jugador tendria que girarla
    // a ciegas hasta que toque. Una esfera de 3 se corre hacia adentro para
    // que su punto de apoyo caiga en p.
    std::vector<Figure> PlacementsFor(Shape s, const Int3& p, const Int3& n, int ignore = -1) const {
        std::vector<Figure> out;
        auto consider = [&](Figure f) {
            f.hasContact = true;
            f.contact = p;
            f.contactN = n;
            if (!HasPort(f, p, -n) || !Fits(f, ignore)) return;
            for (const Figure& o : out)
                if (SameStance(o, f)) return;
            out.push_back(f);
        };
        Figure base;
        base.shape = s;
        if (IsSphere(s)) {
            base.cell = p + n * ((Info(s).size - 1) / 2);
            consider(base);
        } else if (UsesAxis(s)) {
            base.cell = p;
            const int states = s == Shape::Cube ? 6 : 3;
            for (int k = 0; k < states; ++k) {
                base.axis = (1 + k) % states; // +Y primero: es la de siempre
                consider(base);
            }
        } else if (UsesOrient(s)) {
            // Por cada orientacion, cada celda propia que podria estar en p.
            for (int o = 0; o < kOrientations; ++o) {
                base.orient = (kIdentity + o) % kOrientations;
                const Rot r = Orientation(base.orient);
                for (const Int3& lc : LocalCells(s)) {
                    base.cell = p - ToWorld(r, lc);
                    consider(base);
                }
            }
        } else {
            base.cell = p;
            // El pozo se hunde en la superficie del clic: su normal es -n.
            if (s == Shape::Wormhole) base.sink = -n;
            consider(base);
        }
        return out;
    }

    // Dos posturas iguales a la vista Y para la luz: mismas celdas y mismos
    // puertos. Dos orientaciones de la piramide que solo giran alrededor de su
    // eje son la misma pieza, y ROTAR no tiene que gastar un clic en ellas.
    static bool SameStance(const Figure& a, const Figure& b) {
        if (a.shape != b.shape) return false;
        if (UsesAxis(a.shape)) return a.cell == b.cell && a.axis == b.axis;
        if (!UsesOrient(a.shape)) return a.cell == b.cell;
        std::vector<Int3> ca = WorldCells(a), cb = WorldCells(b);
        auto less = [](const Int3& l, const Int3& r) { return l.x != r.x ? l.x < r.x : l.y != r.y ? l.y < r.y : l.z < r.z; };
        std::sort(ca.begin(), ca.end(), less);
        std::sort(cb.begin(), cb.end(), less);
        if (!(ca == cb)) return false;
        for (const Int3& c : ca)
            for (const Int3& d : kDirs)
                if (HasPort(a, c, d) != HasPort(b, c, d)) return false;
        // La piramide boca abajo dobla para el otro lado: no es la misma.
        if (a.shape == Shape::Pyramid) return RotOf(a).u == RotOf(b).u;
        // Mismos puertos; en la cuna y las de recorrido fijo importa ademas
        // con que direccion se entra (una curva espejada tiene los mismos
        // puertos y lleva la luz a otro lado).
        if (a.shape == Shape::Prism || IsPathPiece(a.shape)) {
            const Rot ra = RotOf(a), rb = RotOf(b);
            if (a.shape == Shape::Prism) return ToWorld(ra, kPosZ) == ToWorld(rb, kPosZ) || ToWorld(ra, kPosZ) == -ToWorld(rb, kPosZ);
            // La misma, o la misma recorrida al reves (una rotacion de 180
            // grados sobre la diagonal cambia entrada por salida).
            const PathPiece p = PathOf(a.shape);
            const Int3 ia = ToWorld(ra, p.inDir), oa = ToWorld(ra, p.outDir), ib = ToWorld(rb, p.inDir), ob = ToWorld(rb, p.outDir);
            return (ia == ib && oa == ob) || (ib == -oa && ob == -ia);
        }
        return true;
    }

    // --- Lo que hace el jugador. Cada una devuelve si cambio algo. ---

    // Una pieza ya armada (postura incluida). El gusano se empareja solo: la
    // primera boca abre un par, la segunda lo cierra.
    bool Place(const Figure& placed) {
        Figure f = placed;
        f.fixed = false;
        int& left = inventory[static_cast<int>(f.shape)];
        if (f.shape == Shape::Wormhole && f.sink == Int3{0, 0, 0}) f.sink = PortalSinkFor(f.cell);
        if (left <= 0 || !Fits(f)) return false;
        if (f.shape == Shape::Wormhole) f.pair = OpenPair();
        figures.push_back(f);
        --left;
        return true;
    }

    // La forma corta de siempre (pruebas y niveles): la pieza en la celda, sin
    // contacto. Con orientacion o eje, los de Figure por defecto.
    bool Place(Shape s, const Int3& c, int orientOrAxis = -1) {
        Figure f;
        f.shape = s;
        f.cell = c;
        if (orientOrAxis >= 0) {
            if (UsesAxis(s)) f.axis = orientOrAxis;
            else f.orient = orientOrAxis;
        }
        return Place(f);
    }

    // Quita la pieza que ocupa la celda (si es del jugador). Un gusano se va
    // con su gemelo: una boca sola no es nada.
    bool Remove(const Int3& c) {
        const int i = IndexAt(c);
        if (i < 0 || figures[static_cast<size_t>(i)].fixed) return false;
        const Figure f = figures[static_cast<size_t>(i)];
        for (size_t k = figures.size(); k-- > 0;) {
            const bool twin = f.shape == Shape::Wormhole && f.pair >= 0 && figures[k].shape == Shape::Wormhole && figures[k].pair == f.pair;
            if (static_cast<int>(k) == i || twin) {
                ++inventory[static_cast<int>(figures[k].shape)];
                figures.erase(figures.begin() + static_cast<long>(k));
            }
        }
        return true;
    }

    // Rotar la pieza que ocupa la celda: la postura siguiente. Las esferas no
    // tienen (se ven igual; no cuenta como jugada), el gusano tampoco.
    //   cubo y toroide   el eje: X -> Y -> Z (como siempre)
    //   con contacto     la siguiente de PlacementsFor: sigue tocando lo que tocaba
    //   sin contacto     la siguiente orientacion que quepa, con el ancla quieta
    bool Rotate(const Int3& c) {
        const int i = IndexAt(c);
        if (i < 0) return false;
        Figure& f = figures[static_cast<size_t>(i)];
        if (f.fixed) return false;
        if (UsesAxis(f.shape)) {
            // El cubo tiene seis (el eje y hacia donde gira la franja); el toroide, tres.
            f.axis = (f.axis + 1) % (f.shape == Shape::Cube ? 6 : 3);
            return true;
        }
        if (!UsesOrient(f.shape)) return false;
        if (f.hasContact) {
            const std::vector<Figure> list = PlacementsFor(f.shape, f.contact, f.contactN, i);
            if (list.size() < 2) return false;
            size_t at = 0;
            for (size_t k = 0; k < list.size(); ++k)
                if (SameStance(list[k], f)) at = k;
            const Figure next = list[(at + 1) % list.size()];
            f.cell = next.cell;
            f.orient = next.orient;
            return true;
        }
        for (int k = 1; k < kOrientations; ++k) {
            Figure g = f;
            g.orient = (f.orient + k) % kOrientations;
            if (Fits(g, i) && !SameStance(g, f)) {
                f.orient = g.orient;
                return true;
            }
        }
        return false;
    }

    int InventoryTotal() const {
        int n = 0;
        for (int v : inventory) n += v;
        return n;
    }

    // Una linea de pieza: "<forma> x y z [postura]" (la de `fixed`, y la de las
    // soluciones ";! place" que leen las pruebas). La postura:
    //   cubo, toroide        x | y | z (el eje)
    //   piramide             hacia donde apunta el vertice (por defecto up)
    //   prisma               las dos caras que tocan (catetos): p. ej. "left down"
    //   rampa/torsion/curva  f u: hacia donde va la luz al entrar, y la normal
    //                        de la superficie donde apoya la entrada al reves
    //                        (u = up: entra apoyada en lo de abajo)
    //   agujeros             u: hacia donde mira la lamina (por defecto up)
    //   gusano               x2 y2 z2: la otra boca (las dos, casillas de un
    //                        plano de portal: el plano lo deduce Place)
    static bool ParseFigure(std::istringstream& in, std::vector<Figure>& out, std::string& error) {
        std::string name;
        Figure f;
        if (!(in >> name) || !ShapeName(name, f.shape)) {
            error = "forma desconocida '" + name + "'";
            return false;
        }
        if (!(in >> f.cell.x >> f.cell.y >> f.cell.z)) {
            error = "faltan las tres coordenadas de la pieza";
            return false;
        }
        std::string a, b;
        if (UsesAxis(f.shape)) {
            if (in >> a) {
                const bool neg = !a.empty() && a[0] == '-';
                const std::string k = neg ? a.substr(1) : a;
                f.axis = (k == "x" ? 0 : k == "z" ? 2 : 1) + (neg && f.shape == Shape::Cube ? 3 : 0);
            }
        } else if (f.shape == Shape::Pyramid || f.shape == Shape::BlackHole || f.shape == Shape::WhiteHole) {
            Int3 u = kPosY;
            if (in >> a && !DirName(a, u)) {
                error = "direccion desconocida '" + a + "'";
                return false;
            }
            f.orient = OrientationOf(PerpOf(u, 0), u);
        } else if (f.shape == Shape::Prism) {
            Int3 la = kNegX, lb = kNegY;
            if (in >> a >> b && (!DirName(a, la) || !DirName(b, lb) || Dot(la, lb) != 0)) {
                error = "el prisma son dos caras perpendiculares (p. ej. 'left down')";
                return false;
            }
            f.orient = OrientationOf(-la, -lb);
        } else if (IsPathPiece(f.shape)) {
            Int3 fd = kPosX, ud = kPosY;
            if (in >> a >> b && (!DirName(a, fd) || !DirName(b, ud) || Dot(fd, ud) != 0)) {
                error = "la postura son dos direcciones perpendiculares 'f u' (p. ej. 'right up')";
                return false;
            }
            f.orient = OrientationOf(fd, ud);
        } else if (f.shape == Shape::Wormhole) {
            Figure twin = f;
            if (!(in >> twin.cell.x >> twin.cell.y >> twin.cell.z)) {
                error = "el gusano son dos bocas: 'wormhole x y z x2 y2 z2'";
                return false;
            }
            out.push_back(f);
            out.push_back(twin);
            return true;
        }
        out.push_back(f);
        return true;
    }

    // Del texto de docs/reglas.md. Lo que no se entiende se rechaza con el
    // motivo, en vez de jugarse a medias.
    static bool Parse(std::string_view text, Board& out, std::string& error) {
        Board b;
        bool haveSize = false, haveSource = false, haveReceiver = false;
        std::istringstream lines{std::string(text)};
        std::string raw;
        int lineNo = 0;
        while (std::getline(lines, raw)) {
            ++lineNo;
            // Un nivel guardado en Windows termina cada linea en \r\n: el \r
            // llegaba al titulo y el HUD lo dibujaba como una letra de mas.
            if (!raw.empty() && raw.back() == '\r') raw.pop_back();
            const size_t semi = raw.find(';');
            if (semi != std::string::npos) raw.resize(semi);
            std::istringstream in(raw);
            std::string key;
            if (!(in >> key)) continue;
            const std::string where = " (linea " + std::to_string(lineNo) + ")";
            if (key == "size") {
                if (!(in >> b.W >> b.H >> b.D) || b.W < 1 || b.H < 1 || b.D < 1 || b.W > 16 || b.H > 16 || b.D > 16) {
                    error = "size necesita tres numeros entre 1 y 16" + where;
                    return false;
                }
                haveSize = true;
            } else if (key == "rules") {
                if (!(in >> b.rules) || b.rules < 1) {
                    error = "rules es 'rules N' (la version de las reglas, 1 o mas)" + where;
                    return false;
                }
            } else if (key == "title" || key == "hint") {
                std::string rest;
                std::getline(in, rest);
                while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
                (key == "title" ? b.title : b.hint) = rest;
            } else if (key == "start" || key == "end" || key == "portal" || key == "panel") {
                if (key == "portal" || key == "panel") {
                    b.planes.push_back(Face{});
                    b.planes.back().portal = key == "portal";
                    b.planes.back().panel = key == "panel";
                }
                Face& f = key == "portal" || key == "panel" ? b.planes.back() : b.planes[key == "start" ? 0 : 1];
                std::string name;
                if (!(in >> name) || !FaceName(name, f.normal)) {
                    error = key + " es '" + key + " left|right|floor|ceiling|front|back [x0 y0 z0 x1 y1 z1]'" + where;
                    return false;
                }
                Int3 lo, hi;
                if (in >> lo.x) {
                    if (!(in >> lo.y >> lo.z >> hi.x >> hi.y >> hi.z)) {
                        error = key + ": el rectangulo son seis numeros (dos celdas)" + where;
                        return false;
                    }
                    f.lo = {std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z)};
                    f.hi = {std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z)};
                    f.whole = false;
                }
            } else if (key == "source") {
                std::string dir;
                if (!(in >> b.sourceCell.x >> b.sourceCell.y >> b.sourceCell.z >> dir) || !DirName(dir, b.sourceDir)) {
                    error = "source es 'source x y z right|left|up|down|back|front'" + where;
                    return false;
                }
                haveSource = true;
            } else if (key == "receiver") {
                if (!(in >> b.receiverCell.x >> b.receiverCell.y >> b.receiverCell.z)) {
                    error = "receiver es 'receiver x y z'" + where;
                    return false;
                }
                haveReceiver = true;
            } else if (key == "inventory") {
                std::string name;
                int count = 0;
                while (in >> name >> count) {
                    Shape s;
                    if (!ShapeName(name, s) || count < 0 || !Info(s).placeable) {
                        error = "inventory es 'inventory sphere N cube N ...' (y solo piezas que se pueden poner)" + where;
                        return false;
                    }
                    // El gusano se da por PARES ("wormhole 1" = dos bocas).
                    b.inventory[static_cast<int>(s)] = s == Shape::Wormhole ? count * 2 : count;
                }
            } else if (key == "fixed") {
                std::vector<Figure> got;
                if (!ParseFigure(in, got, error)) {
                    error = "fixed: " + error + where;
                    return false;
                }
                // Los pares fijos se numeran desde 1000: los del jugador
                // empiezan en 0 y no se cruzan nunca.
                if (got.size() == 2) {
                    int top = 999;
                    for (const Figure& o : b.figures) top = std::max(top, o.pair);
                    got[0].pair = got[1].pair = top + 1;
                }
                for (Figure& f : got) {
                    f.fixed = true;
                    b.figures.push_back(f);
                }
            } else {
                error = "no se que es '" + key + "'" + where;
                return false;
            }
        }
        if (!haveSize || !haveSource || !haveReceiver) {
            error = "faltan size, source o receiver";
            return false;
        }
        b.FillWholeFaces();
        // Un rectangulo dentro de la caja y PLANO en el eje de su normal (lo y hi
        // en la misma capa). Ya no tiene que estar en una cara de la caja: los
        // planos flotan (ver Face).
        for (size_t i = 0; i < b.planes.size(); ++i) {
            const Face& f = b.planes[i];
            const int a = AxisOf(f.normal);
            const int loA = a == 0 ? f.lo.x : a == 1 ? f.lo.y : f.lo.z, hiA = a == 0 ? f.hi.x : a == 1 ? f.hi.y : f.hi.z;
            if (!b.InBox(f.lo) || !b.InBox(f.hi) || loA != hiA) {
                error = std::string(i == 0 ? "el plano de salida" : i == 1 ? "el plano de llegada" : f.panel ? "un panel" : "un plano de portal") +
                        " tiene un rectangulo fuera de la caja o que no es plano (sus dos celdas en distinta capa)";
                return false;
            }
        }
        // Un plano de portal no pisa a otro plano en su misma cara: la casilla
        // seria de dos planos a la vez (y la de la fuente o el receptor, un
        // pozo posible).
        for (size_t i = 1; i < b.planes.size(); ++i)
            for (size_t j = 0; j < i; ++j) {
                const Face &p = b.planes[i], &q = b.planes[j];
                if (p.normal != q.normal) continue;
                if (p.lo.x <= q.hi.x && q.lo.x <= p.hi.x && p.lo.y <= q.hi.y && q.lo.y <= p.hi.y && p.lo.z <= q.hi.z && q.lo.z <= p.hi.z) {
                    error = "dos planos se superponen (misma normal, mismas celdas)";
                    return false;
                }
            }
        if (b.PlanesTouch()) {
            error = "los planos se tocan: la salida y la llegada tienen que estar separadas por aire";
            return false;
        }
        if (!b.InPlane(b.sourceCell, b.planes[0])) {
            error = "la fuente no esta sobre el plano de salida";
            return false;
        }
        if (!b.InPlane(b.receiverCell, b.planes[1])) {
            error = "el receptor no esta sobre el plano de llegada";
            return false;
        }
        if (Dot(b.sourceDir, b.planes[0].normal) != 0) {
            error = "la direccion de la fuente se sale del plano de salida";
            return false;
        }
        // Las fijas: dentro de la caja y sin pisarse. Se comprueba de a una
        // contra las anteriores (Fits cuenta las que ya estan).
        std::vector<Figure> fixedOnes = std::move(b.figures);
        b.figures.clear();
        for (Figure f : fixedOnes) {
            if (f.shape == Shape::Wormhole) f.sink = b.PortalSinkFor(f.cell);
            if (!b.Fits(f)) {
                error = std::string("una pieza fija (") + Info(f.shape).key + ") queda fuera de la cuadricula o encima de otra";
                return false;
            }
            b.figures.push_back(f);
        }
        out = std::move(b);
        return true;
    }

    static bool FaceName(const std::string& s, Int3& out) {
        if (s == "left") out = kNegX;
        else if (s == "right") out = kPosX;
        else if (s == "floor") out = kNegY;
        else if (s == "ceiling") out = kPosY;
        else if (s == "front") out = kNegZ;
        else if (s == "back") out = kPosZ;
        else return false;
        return true;
    }
    static bool DirName(const std::string& s, Int3& out) {
        if (s == "right") out = kPosX;
        else if (s == "left") out = kNegX;
        else if (s == "up") out = kPosY;
        else if (s == "down") out = kNegY;
        else if (s == "back") out = kPosZ;
        else if (s == "front") out = kNegZ;
        else return false;
        return true;
    }
    static const char* DirKey(const Int3& d) {
        if (d == kPosX) return "right";
        if (d == kNegX) return "left";
        if (d == kPosY) return "up";
        if (d == kNegY) return "down";
        if (d == kPosZ) return "back";
        return "front";
    }
    static bool ShapeName(const std::string& s, Shape& out) {
        for (int i = 0; i < kShapeCount; ++i)
            if (s == Info(static_cast<Shape>(i)).key) {
                out = static_cast<Shape>(i);
                return true;
            }
        return false;
    }

private:
    // El numero del par de gusano que tiene una boca sola (del jugador), o uno nuevo.
    int OpenPair() const {
        int top = -1;
        for (const Figure& f : figures) {
            if (f.shape != Shape::Wormhole || f.fixed) continue;
            top = std::max(top, f.pair);
            if (!Twin(f)) return f.pair;
        }
        return top + 1;
    }
};

} // namespace gravitational
