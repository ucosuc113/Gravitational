#pragma once

// LA LUZ EN 3D: de la fuente, por los planos y las piezas que se tocan, hasta
// el receptor. Las reglas son las de docs/reglas.md, y esta es su unica
// implementacion. Sin motor: se prueba con un `for` (TraceCheck.cpp) y con el
// resolvedor de Tools/LevelLab.
//
// Devuelve el recorrido como una lista de TRAMOS, uno por superficie: un tramo
// recto sobre un plano, o un recorrido sobre una pieza. De esa lista salen a la
// vez el resultado y el dibujo (BeamPath.h): decidir y dibujar no pueden
// discrepar.
//
// --- Una pieza, vista por la luz ---
//
// Toda pieza contesta lo mismo (Transfer): "entro por esta cara de esta celda,
// yendo hacia aca; ¿por donde puedo salir?". La respuesta es una lista
// ORDENADA de salidas (cara, direccion, cuanto recorrio hasta ahi), y la luz
// sale por la primera que toque algo. Si ninguna toca, cada pieza dice que
// pasa: la esfera orbita, la rampa se acaba en el aire. Asi la esfera de
// siempre y la rampa nueva son la misma maquina con otra tabla, y el trazado
// no sabe que piezas existen.

#include "Board.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gravitational {

// --- Los largos: las MISMAS cuentas que los shaders y las mallas ---
//
// Si discreparan, la punta de la luz saltaria al pasar de una superficie a otra.
inline constexpr float kPiF = 3.14159265f;
inline constexpr float kTorusMajor = 0.35f;   // las de la primitiva Torus del motor
inline constexpr float kTorusMinor = 0.15f;
inline const float kPyramidHalf = 0.5f + std::sqrt(1.25f); // del centro de la base al vertice (1,618)
inline const float kPyramidSlant = std::sqrt(1.25f);         // de una arista de la base al vertice (1,118)
inline const float kPrismShort = 1.0f;                     // cateto a cateto por la esquina recta
inline const float kPrismLong = 1.0f + std::sqrt(2.0f);    // cateto a cateto por la hipotenusa
inline constexpr float kRampLen = 0.75f * kPiF;            // cuarto de circulo de radio 1,5
// Torsion y curva no son circulos: el largo lo mide Tools/Meshes/gen_pieces.py
// al generar la malla (la UV va por largo de arco) y se copia aca.
inline constexpr float kTwistLen = 2.1743f;
inline constexpr float kBendLen = 1.3443f;
inline constexpr float kMouthLen = 1.5f;  // la espiral del borde del pozo a la garganta (Piece.hlsl: 3/4 de vuelta)
inline constexpr float kHoleLen = 0.5f;   // del contacto al fondo de un agujero

// El largo del cinturon del cubo hasta un angulo (centro de cara a centro de
// cara es 1; ver tambien Figure.hlsl).
inline float CubeArc(float angle) {
    const float quarter = kPiF * 0.5f;
    const float k = std::floor(angle / quarter);
    const float phi = angle - k * quarter;
    const float f = phi <= kPiF * 0.25f ? 0.5f * std::tan(phi) : 1.0f - 0.5f * std::tan(quarter - phi);
    return k + f;
}

struct Hop {
    enum class Kind { Plane, Figure } kind = Kind::Plane;

    // Plane: sobre el plano `plane` (0 salida, 1 llegada), de la celda `from` a
    // la `to` en direccion `dir`. Con toEdge sigue media celda mas, hasta el
    // borde; con fromEdge empieza en el borde (llego por la arista).
    int plane = 0;
    Int3 from, to, dir;
    bool toEdge = false;
    bool fromEdge = false;

    // Figure: la celda por la que entro (un puerto de la pieza), la forma, la
    // cara de entrada (su normal hacia afuera), la direccion tangente al
    // entrar, y cuanto recorrio sobre la pieza.
    //   quarters  esfera, cubo y toroide (anillo): cuartos de vuelta, 0 = se
    //             freno al entrar, 4 = vuelta entera (lo que lee Figure.hlsl)
    //   len       metros sobre esta superficie (todas las piezas)
    //   outMouth  el pozo de SALIDA de un gusano (la luz sale de la garganta;
    //             nIn es la normal de su plano y t hacia donde sigue)
    //   tube      el toroide recorrido a lo largo de su eje: la vuelta al tubo
    //   via       por donde la luz SALIO de la entrada: para dibujar el arco. Es
    //             t si siguio derecho; el costado si una esfera la tiro hacia
    //             alli; la direccion nueva si la franja de un cubo la doblo.
    //   fromSeam  cubo: llego por una COSTURA (ver "Cubos pegados" en
    //             TransferOf): empieza en la arista de la cara nIn, medio
    //             cuarto antes de su centro, no en el centro
    //   toSeam    cubo: se va por una costura (o baja a un plano por la arista
    //             de una cara pegada), en la arista medio cuarto antes
    //             de la cara de `quarters` (o sea, termina en quarters - 0,5)
    Int3 cell;
    Shape shape = Shape::Sphere;
    Int3 nIn, t, via;
    int quarters = 0;
    float len = 0.0f;
    bool outMouth = false;
    bool tube = false;
    bool fromSeam = false;
    bool toSeam = false;
};

enum class TraceEnd { Reached, Lost, Blocked, Orbit, Loop, Absorbed };

struct Trace {
    std::vector<Hop> hops;
    TraceEnd end = TraceEnd::Lost;
};

// Una salida de una pieza: por la cara `normal` de la celda `cell` (las dos del
// mundo), con la luz yendo hacia `dir`, despues de recorrer `len`.
//
// Una COSTURA (seam) no toca un puerto: la luz pasa de largo al cubo pegado del
// otro lado de `normal`, sobre su cara `seamFace` (la misma en la que iba) y
// yendo hacia `normal`. Siempre "toca": el cubo vecino esta ahi.
struct Exit {
    Int3 cell, normal, dir;
    float len = 0.0f;
    int quarters = 0;
    Int3 via; // ver Hop::via
    bool seam = false;
    Int3 seamFace;
    bool atEdge = false; // baja a un plano por la ARISTA de la cara pegada (ver el cubo)
};

struct Transfer {
    bool blocked = false;  // la pieza no toma la luz por ahi: se frena en el contacto
    bool absorbed = false; // la toma y no la suelta (agujero negro)
    Exit exits[8];
    int count = 0;
    // Si ninguna salida toca nada:
    TraceEnd whenNone = TraceEnd::Orbit;
    float noneLen = 0.0f;
    int noneQuarters = 4;
    Int3 noneVia;
    bool noneToSeam = false; // cubo: se termina en una arista (ver Hop::toSeam)
    bool tube = false;
    void Add(const Int3& cell, const Int3& normal, const Int3& dir, float len, int quarters, const Int3& via) {
        exits[count++] = Exit{cell, normal, dir, len, quarters, via};
    }
    void AddSeam(const Int3& cell, const Int3& normal, const Int3& onFace, float len, int quarters, const Int3& via) {
        exits[count++] = Exit{cell, normal, normal, len, quarters, via, true, onFace};
    }
    // Al plano por la arista: la luz cae en `cell` (la celda de la capa de al
    // lado de la del cubo) desde su borde, yendo hacia `dir`.
    void AddEdge(const Int3& cell, const Int3& normal, const Int3& dir, float len, int quarters, const Int3& via) {
        exits[count++] = Exit{cell, normal, dir, len, quarters, via, false, Int3{}, true};
    }
};

// ¿Hay del otro lado de esa cara de un cubo otro cubo con la franja en el MISMO
// eje (en cualquier sentido)? Entonces los dos son una sola caja: ver "Cubos
// pegados" en TransferOf.
inline bool SeamAt(const Board& b, const Int3& cell, const Int3& face, const Int3& axis) {
    const Int3 q = cell + face;
    if (!b.InBox(q)) return false;
    const Figure* n = b.FigureAt(q);
    return n && n->shape == Shape::Cube && AxisOf(CubeAxis(n->axis)) == AxisOf(axis);
}

// --- EL MODELO (24/10/2026, D1-D4 de docs/arquitectura-luz.md) ---
//
// La luz camina por UNA superficie: los planos y las caras expuestas de las
// piezas. Una cara apoyada en un plano NO existe para la luz: es el plano. De
// ahi salen las cuatro reglas que antes eran parches sueltos (22-24/10), cada uno
// para la pieza que se veia mal ese dia:
//
//   D1  la luz nunca va por una cara apoyada: llega a su ARISTA y sigue por el
//       plano, hacia afuera de la pieza (si el plano sigue ahi; si no, se pierde)
//   D2  la luz del plano que se encuentra una pieza apoyada en una cara entera
//       SUBE por la cara de enfrente, o se FRENA en su borde si esa cara no la
//       lleva hacia arriba. Nunca entra por debajo
//   D3  la que da la vuelta a una pieza apoyada en un PUNTO y vuelve a el, baja
//       al plano por ese punto. Solo orbita una pieza que no toca nada
//   D4  la luz no pasa dos veces por la misma pieza ni por la misma casilla de
//       un plano: la segunda vez se frena. Salvo lo que devuelve un agujero
//       blanco, que es justamente volver (ver TraceLight)
//
// Las cuatro estan escritas como invariantes en TraceInvariants.h y
// `levellab probe` las pasa por miles de tableros: una regla nueva que las
// rompa, falla ahi.

// LA VERSION DE LAS REGLAS (D5). Cada nivel dice con que version se hizo
// (`rules N`; sin la linea, 1) y `levellab check` falla con uno viejo: un
// cambio de reglas rompe soluciones y abre atajos, y hasta el 24/10 eso se
// descubria jugando. Si cambia lo que hace la luz, se sube este numero y se
// rehacen los niveles que `levellab check` senale.
//   1  hasta el 24/10/2026: puertos, mas los parches de costura, arista y subida
//   2  24/10/2026: el modelo de superficie, D1-D4 (abajo)
//   3  24/10/2026: planos FLOTANTES (Board.h: Face) - en cualquier lugar de la
//      caja; la luz pasa de uno a otro por aristas concavas, a ras y convexas
//   4  27/10/2026: los planos tienen DOS CARAS (Board::PlaneOn). La luz que
//      corre por un plano toca lo que este de cualquiera de sus lados, y sube
//      por una pared que se le cruce de cualquier lado
inline constexpr int kRulesVersion = 4;

// Como toca una pieza al plano sobre el que esta, por la cara `planeNormal`
// de su celda. Es lo que decide que hace la luz que llega por ese plano:
//   Full     una cara ENTERA apoyada (cubo, cateto de la cuna, base de la
//            piramide): D2, sube o se frena
//   Point    un punto (esfera, toroide, punta de la piramide): la luz pasa por
//            el, y D3
//   Tangent  una cinta que arranca tangente al plano (rampa, torsion, curva)
//   Sheet    una lamina hundida en el plano (agujeros, pozo de gusano)
enum class Contact { Full, Point, Tangent, Sheet };

inline Contact ContactOf(const Figure& f, const Int3& planeNormal) {
    switch (f.shape) {
        case Shape::Cube:
        case Shape::Prism: return Contact::Full;
        case Shape::Pyramid: return ToLocal(RotOf(f), planeNormal) == kNegY ? Contact::Full : Contact::Point;
        case Shape::Ramp:
        case Shape::Twist:
        case Shape::Bend: return Contact::Tangent;
        case Shape::BlackHole:
        case Shape::WhiteHole:
        case Shape::Wormhole: return Contact::Sheet;
        default: return Contact::Point; // esferas, toroide
    }
}

// El plano pegado a la cara `n` de la celda (un plano la cubre, del lado de
// esa celda), o -1. Con planos flotantes, una pieza puede estar pegada a uno en
// medio de la caja.
inline int GluedPlane(const Board& b, const Int3& cell, const Int3& n) { return b.PlaneOn(n, cell); }

// D1 en una arista: la luz llega al borde de la cara pegada `n` (plano
// `plane`) y sigue por ese plano hacia `d`, en la celda de al lado. Si el plano
// sigue ahi, sale por la arista; si no, se pierde en ella (nunca va por debajo).
// `plane` -1: no hay plano que siga por la arista (la junta de dos cubos, ver
// la costura): la luz se pierde en ella.
inline void DownToPlane(const Board& b, Transfer& x, const Int3& cell, int plane, const Int3& n, const Int3& d, float len, int quarters,
                        const Int3& via) {
    const Int3 beyond = cell + d;
    if (plane >= 0 && b.InBox(beyond) && b.PlaneOn(n, beyond) == plane) {
        x.AddEdge(beyond, n, d, len, quarters, via);
    } else {
        // A RAS (planos flotantes, 24/10/2026): el plano en el que se apoya la
        // pieza se acaba justo en su borde, y del borde sigue otro plano EN EL
        // MISMO NIVEL que la cara por la que va la luz (un cubo al borde de un
        // plano del que cuelga otro). La luz sigue derecho, sin arista, por ese,
        // hacia `n`. Concava (arriba) o a ras (aca): la superficie siguiendo.
        const Int3 flat = cell + n + d;
        if (b.InBox(flat) && b.PlaneOn(-d, flat) >= 0) x.AddEdge(flat, -d, n, len, quarters, via);
    }
    x.whenNone = TraceEnd::Lost;
    x.noneLen = len;
    x.noneQuarters = quarters;
    x.noneVia = via;
    x.noneToSeam = true;
}

// LA LUZ QUE LLEGA POR UN PLANO A UNA PIEZA APOYADA EN UNA CARA ENTERA (D2).
// La cara apoyada y el plano son una sola superficie, asi que la luz no se puede
// meter entre los dos: se encuentra con la cara que tiene ENFRENTE y sube
// pegada a ella, como por un bache - sube, cruza por arriba y baja del otro
// lado, y por la arista de abajo vuelve al plano (D1). Hasta el 23/10 entraba
// por el centro de la cara apoyada y cruzaba por DEBAJO de la pieza.
//
// Si la cara de enfrente no la lleva hacia arriba, la luz se FRENA en su borde
// (TraceLight): la tapa de un cubo con el eje a lo largo de la luz, la cara de un
// cubo de eje vertical (su franja va de costado, no hacia arriba), la punta
// triangular de una cuna recorrida a lo largo de su arista. Hasta el 24/10 el
// cubo de eje a lo largo entraba por debajo y la franja doblaba la luz sobre el
// plano: el nivel 5 se armo con eso. Ese giro sigue existiendo, pero solo con la
// luz que le llega al cubo desde OTRA pieza.
//
// Devuelve con que cara y direccion entra: el cubo, por una costura sobre la
// cara de enfrente subiendo (ver "Cubos pegados" en TransferOf); la cuna y la
// piramide, por su cara apoyada con la direccion del plano, y TransferOf sabe
// que es una subida porque llega con `seam`.
inline bool ClimbsOnto(const Figure& f, const Int3& planeNormal, const Int3& dir, Int3& nIn, Int3& t) {
    if (f.shape == Shape::Cube) {
        const int a = AxisOf(CubeAxis(f.axis));
        if (a == AxisOf(dir) || a == AxisOf(planeNormal)) return false;
        nIn = -dir;
        t = -planeNormal;
        return true;
    }
    if (f.shape == Shape::Prism) {
        if (ToLocal(RotOf(f), dir).z != 0) return false;
        nIn = planeNormal;
        t = dir;
        return true;
    }
    if (f.shape == Shape::Pyramid) {
        if (ToLocal(RotOf(f), planeNormal) != kNegY) return false; // de punta: la toca en un punto
        nIn = planeNormal;
        t = dir;
        return true;
    }
    return false;
}

// Lo que hace la luz dentro de una pieza, segun por donde entro. La tabla de
// cada una esta en docs/reglas.md; aqui la cuenta. `seam`: entro a un cubo por
// una costura, o subio a un cubo o a una cuna desde el plano en el que se apoya
// (ver ClimbsOnto).
inline Transfer TransferOf(const Board& b, const Figure& f, const Int3& cell, const Int3& nIn, const Int3& t, bool seam = false) {
    Transfer x;
    x.noneVia = t;
    // Las tres caras que una vuelta "redonda" toca en orden, y la direccion con
    // la que sale por cada una (cubo, anillo del toroide).
    const Int3 roundFace[3] = {t, -nIn, -t};
    const Int3 roundDir[3] = {-nIn, -t, nIn};
    switch (f.shape) {
        case Shape::Sphere:
        case Shape::Sphere3:
        case Shape::Sphere5: {
            // LA ESFERA TIRA HACIA LO QUE TOCA. Hasta el 21/10/2026 seguia solo
            // su circulo maximo: adelante, la cara opuesta, atras. Ese circulo
            // vive en el plano de la entrada y la direccion, y como todas las
            // piezas hacian lo mismo, la luz no salia NUNCA de la rebanada
            // donde nacia: el juego era 2D y lo de los costados, inalcanzable.
            // Ahora, a un cuarto de vuelta, ademas de adelante estan los dos
            // COSTADOS (a la misma distancia sobre la esfera): la luz va al
            // primero que toque algo, adelante primero. Una esfera con un
            // vecino al costado dobla la luz de eje.
            const int n = Info(f.shape).size, h = (n - 1) / 2;
            const float quarter = kPiF * 0.5f * n * 0.5f;
            const Int3 side = Cross(t, nIn);
            const Int3 face[5] = {t, side, -side, -nIn, -t};
            const Int3 dir[5] = {-nIn, -nIn, -nIn, -t, nIn};
            const Int3 via[5] = {t, side, -side, t, t};
            const int q[5] = {1, 1, 1, 2, 3};
            for (int k = 0; k < 5; ++k) x.Add(f.cell + face[k] * h, face[k], dir[k], quarter * q[k], q[k], via[k]);
            // D3: si entro desde un PLANO (apoyada en el), la vuelta entera la
            // devuelve a ese punto yendo para el mismo lado, y baja al plano: la
            // esfera la dejo pasar dando una vuelta. Hasta el 24/10 orbitaba
            // para siempre, y una esfera sola en el piso era una trampa. Entrando
            // desde otra pieza no: volver a ella seria un rebote (D4).
            if (GluedPlane(b, cell, nIn) >= 0) x.Add(cell, nIn, t, quarter * 4.0f, 4, t);
            x.noneLen = quarter * 4.0f;
            return x;
        }
        case Shape::Cube: {
            // La franja alrededor de su eje, con SENTIDO (axis 0..5: X, Y, Z,
            // -X, -Y, -Z). Por las tapas (las caras del eje) no entra. La luz
            // que llega por el costado de la franja:
            //   - yendo a lo largo de la franja, la recorre como siempre;
            //   - yendo a lo largo del EJE, la franja la atrapa y la DOBLA 90
            //     grados hacia donde gira (cross(eje, entrada)). Es el giro de
            //     eje que se controla rotando el cubo.
            const Int3 A = CubeAxis(f.axis);
            if (AxisOf(nIn) == AxisOf(A)) {
                x.blocked = true;
                return x;
            }
            const Int3 tt = AxisOf(t) == AxisOf(A) ? Cross(A, nIn) : t;
            // CUBOS PEGADOS (22/10/2026): dos cubos vecinos con la franja en el
            // mismo eje son UNA caja, y la cara que comparten no es superficie
            // de nadie. Antes cada cubo era una vuelta aparte y la cara de en
            // medio un contacto mas: la luz que corria por debajo de dos cubos
            // apoyados en el piso subia por esa cara interior, daba la vuelta
            // al segundo, volvia al piso, entraba otra vez al primero... siete
            // tramos de ida y vuelta donde se veia un rectangulo que la luz
            // tenia que cruzar por abajo.
            //
            // Ahora, al llegar a la arista de una cara pegada, la luz sigue
            // DERECHO sobre la misma cara del cubo vecino (la costura), y en el
            // centro de esa cara - un contacto como cualquiera - sale si algo
            // la toca: por debajo de dos cubos en el piso, vuelve al piso del
            // otro lado. Solo con el mismo eje: con ejes distintos las dos
            // franjas no forman una caja, y la cara de en medio sigue siendo el
            // contacto de antes (es como un cubo le pasa la luz a otro que la
            // dobla).
            //
            // Cada salida va en el orden en que la luz la encuentra; la costura
            // la corta ahi (la luz ya se fue al vecino). Llegando por una
            // costura se empieza medio cuarto antes, en la arista: ese medio
            // metro va en todos los largos.
            const float lead = seam ? 0.5f : 0.0f;
            x.noneLen = 4.0f; // la vuelta entera (si no se pierde antes en una arista: abajo)
            if (seam) x.Add(cell, nIn, tt, lead, 0, tt); // el centro de la cara por la que llego
            const Int3 face[4] = {tt, -nIn, -tt, nIn};
            const Int3 dir[3] = {-nIn, -tt, nIn};
            const Int3 onFace[4] = {nIn, tt, -nIn, -tt}; // la cara que la luz recorre antes de cada arista
            //
            // LA CARA PEGADA A UN PLANO es lo mismo con un plano en vez de un
            // cubo: ese plano y la cara son una sola superficie. La luz que
            // llega a esa arista pasa al plano ahi mismo y sigue DERECHO, hacia
            // afuera de la cara en la que iba. Antes iba hasta el centro de la
            // cara pegada y salia con la direccion de la franja, o sea como si
            // hubiera pasado por debajo del cubo: un cubo contra la pared con
            // la luz bajando por su frente la dejaba en la pared yendo hacia
            // ARRIBA, lejos del receptor que tenia justo debajo. Si el plano no
            // sigue del otro lado de la arista, la luz se pierde ahi (D1): hasta
            // el 24/10 eso valia solo llegando por una costura, y entrando por
            // un centro iba a la cara pegada - por debajo del cubo.
            //
            // La luz nunca ENTRA por una cara pegada (D2: desde el plano sube o
            // se frena), asi que nunca va por una: la arista k = 3, que lleva de
            // vuelta a la cara de entrada, no puede ser de una cara pegada.
            for (int k = 0; k < 4; ++k) {
                if (SeamAt(b, cell, face[k], A)) {
                    // Si en el vecino esa misma cara esta pegada a un plano, la
                    // luz no puede seguir por ella (D1: iria entre el cubo y el
                    // plano): se pierde en la junta. Pasaba en n23 (24/10/2026):
                    // por debajo de un cubo al aire y, por la costura, por debajo
                    // del de al lado, que si estaba apoyado.
                    if (GluedPlane(b, cell + face[k], onFace[k]) >= 0)
                        DownToPlane(b, x, cell, -1, face[k], onFace[k], lead + k + 0.5f, k + 1, tt);
                    else
                        x.AddSeam(cell, face[k], onFace[k], lead + k + 0.5f, k + 1, tt);
                    break;
                }
                if (k == 3) break;
                const int glued = GluedPlane(b, cell, face[k]);
                if (glued >= 0) {
                    DownToPlane(b, x, cell, glued, face[k], onFace[k], lead + k + 0.5f, k + 1, tt);
                    break;
                }
                // A RAS POR ARRIBA (planos flotantes, 24/10/2026): la cara por
                // la que va la luz sigue, pasada esta arista, en un plano en su
                // MISMO nivel (un estante pegado a la tapa del cubo). La luz se
                // va por el, derecho: el cubo es un escalon de una dimension a
                // la de arriba. Es la misma superficie siguiendo (D1).
                const Int3 flat = cell + face[k] + onFace[k];
                if (b.InBox(flat) && b.PlaneOn(-onFace[k], flat) >= 0) {
                    x.AddEdge(flat, -onFace[k], face[k], lead + k + 0.5f, k + 1, tt);
                    break;
                }
                x.Add(cell, face[k], dir[k], lead + CubeArc((k + 1) * kPiF * 0.5f), k + 1, tt);
            }
            x.noneVia = tt;
            return x;
        }
        case Shape::Torus: {
            if (AxisOf(t) == f.axis) {
                // A lo largo del eje la luz da la vuelta al TUBO (un circulo
                // chico que pasa por adentro del agujero) y vuelve al mismo
                // contacto yendo para el mismo lado: sigue como si el toroide
                // no estuviera. Es lo que lo distingue del cubo, que ahi frena.
                x.tube = true;
                x.Add(cell, nIn, t, 2.0f * kPiF * kTorusMinor, 0, t);
                x.whenNone = TraceEnd::Orbit;
                x.noneLen = 2.0f * kPiF * kTorusMinor;
                return x;
            }
            // Alrededor del eje: el ecuador de afuera (radio 0,5), como una esfera
            // que solo toca por el anillo.
            const float quarter = kPiF * 0.5f * 0.5f;
            for (int k = 0; k < 3; ++k) x.Add(cell, roundFace[k], roundDir[k], quarter * (k + 1), k + 1, t);
            // D3, como la esfera: apoyado en un plano, la vuelta entera lo
            // devuelve al punto de contacto y baja al plano.
            if (GluedPlane(b, cell, nIn) >= 0) x.Add(cell, nIn, t, quarter * 4.0f, 4, t);
            x.noneLen = quarter * 4.0f;
            return x;
        }
        case Shape::Prism: {
            // La cuna: catetos en -X y -Y locales, hipotenusa mirando a +X+Y,
            // eje Z. La luz da la vuelta al triangulo; de un cateto solo puede
            // pasar al otro, por la esquina recta (corto) o por la hipotenusa
            // (largo), y sale con la direccion que trae.
            const Rot r = RotOf(f);
            const Int3 ln = ToLocal(r, nIn), lt = ToLocal(r, t);
            if (lt.z != 0) {
                x.blocked = true; // a lo largo de la arista: se iria por la tapa
                return x;
            }
            if (seam) {
                // SUBE DESDE EL PLANO en el que se apoya (ver ClimbsOnto): `nIn`
                // es la cara apoyada. Desde el lado de la pendiente, sube por la
                // hipotenusa y baja por el otro cateto; desde el lado recto,
                // sube por ese cateto y baja por la pendiente. En el centro del
                // cateto de pie (un contacto) sale si algo lo toca; al llegar
                // abajo, vuelve al plano por la arista si el plano sigue, y si
                // no, se pierde. Con el cateto de pie pegado a una pared, la
                // pendiente la lleva hasta esa pared y sigue por ella.
                const Int3 v = ln == kNegY ? kNegX : kNegY; // el cateto de pie (local)
                const Int3 gW = nIn, vW = ToWorld(r, v);
                const float slope = kPrismLong - kPrismShort; // la hipotenusa
                const int ground = b.PlaneOn(gW, cell);
                x.whenNone = TraceEnd::Lost;
                auto down = [&](const Int3& d, float len) {
                    const Int3 beyond = cell + d;
                    if (b.InBox(beyond) && b.PlaneOn(gW, beyond) == ground) x.AddEdge(beyond, gW, d, len, 0, t);
                    x.noneLen = len;
                };
                if (lt == v) {
                    const int wall = GluedPlane(b, cell, vW);
                    if (wall >= 0) {
                        const Int3 beyond = cell - gW;
                        if (b.InBox(beyond) && b.PlaneOn(vW, beyond) == wall) x.AddEdge(beyond, vW, -gW, slope, 0, t);
                        x.noneLen = slope;
                        return x;
                    }
                    // Arriba de la pendiente, un plano en ese nivel que empieza
                    // justo ahi (un estante): la luz sigue por el (a ras, como
                    // el cubo). La cuna sube la luz de un plano al de arriba.
                    const Int3 shelf = cell + vW - gW;
                    if (b.InBox(shelf) && b.PlaneOn(gW, shelf) >= 0) {
                        x.AddEdge(shelf, gW, vW, slope, 0, t);
                        x.noneLen = slope;
                        return x;
                    }
                    x.Add(cell, vW, gW, slope + 0.5f, 0, t);
                    down(vW, slope + 1.0f);
                } else {
                    x.Add(cell, vW, -gW, 0.5f, 0, t);
                    down(t, 1.0f + slope);
                }
                return x;
            }
            Int3 face, dir;
            float len;
            if (ln == kNegX) {
                face = kNegY;
                dir = lt == kNegY ? kPosX : kNegX;
                len = lt == kNegY ? kPrismShort : kPrismLong;
            } else {
                face = kNegX;
                dir = lt == kNegX ? kPosY : kNegY;
                len = lt == kNegX ? kPrismShort : kPrismLong;
            }
            // D1: si el cateto de salida esta apoyado en un plano, la luz no
            // llega a su centro (es el plano): en la arista de ese cateto sigue
            // por el plano, hacia afuera de la cuna - por la esquina recta, hacia
            // donde miraba el cateto de entrada; por la hipotenusa, al reves.
            // Hasta el 24/10 salia por el centro del cateto: por debajo.
            const Int3 faceW = ToWorld(r, face);
            const int glued = GluedPlane(b, cell, faceW);
            if (glued >= 0) {
                DownToPlane(b, x, cell, glued, faceW, len == kPrismShort ? nIn : -nIn, len - 0.5f, 0, t);
                return x;
            }
            x.Add(cell, faceW, ToWorld(r, dir), len, 0, t);
            x.noneLen = kPrismShort + kPrismLong;
            return x;
        }
        case Shape::Pyramid: {
            // De la base al vertice (o al reves) por la cara hacia donde iba, y
            // EN LA PUNTA DOBLA 90 GRADOS: ahi se juntan cuatro caras, y la luz
            // baja por la de al lado en vez de por la de enfrente. La direccion
            // de salida es cross(entrada, t): dar vuelta la piramide (base
            // arriba) dobla para el otro lado. Por los lados inclinados no
            // toca nada: no la desvia lo que tenga al costado.
            const Int3 turn = Cross(nIn, t);
            if (seam) {
                // SUBE DESDE EL PLANO en el que apoya la base (ClimbsOnto): por
                // la cara de enfrente hasta la punta, sin cruzar la base. En la
                // punta dobla y sale si algo la toca; si no, baja por la cara de
                // al lado y vuelve al plano por esa arista, si el plano sigue
                // ahi. Si no sigue, se pierde: nunca vuelve por debajo.
                x.Add(cell, -nIn, turn, kPyramidSlant, 0, turn);
                DownToPlane(b, x, cell, b.PlaneOn(nIn, cell), nIn, turn, 2.0f * kPyramidSlant, 0, turn);
                return x;
            }
            const bool fromApex = ToLocal(RotOf(f), nIn) == kPosY;
            // D1: entrando por la punta, baja por la cara de `-turn` (Piece.hlsl)
            // hasta la base; si la base esta apoyada en un plano, en esa arista
            // sigue por el plano en vez de cruzar la base hasta su centro.
            const int base = fromApex ? GluedPlane(b, cell, -nIn) : -1;
            if (base >= 0) {
                DownToPlane(b, x, cell, base, -nIn, -turn, kPyramidSlant, 0, turn);
                return x;
            }
            x.Add(cell, -nIn, turn, kPyramidHalf, 0, turn);
            // D3: apoyada de PUNTA en un plano, si la base no toca nada la luz
            // cruza la base, baja por la cara de enfrente hasta la punta y ahi
            // vuelve al plano, yendo hacia `-turn` (lo que trae al bajar).
            if (fromApex && GluedPlane(b, cell, nIn) >= 0) x.Add(cell, nIn, -turn, 2.0f * kPyramidHalf, 0, turn);
            x.noneLen = 2.0f * kPyramidHalf;
            x.noneVia = turn;
            return x;
        }
        case Shape::Ramp:
        case Shape::Twist:
        case Shape::Bend: {
            // Recorrido fijo, en los dos sentidos; cualquier otra entrada frena.
            const Rot r = RotOf(f);
            const PathPiece p = PathOf(f.shape);
            const Int3 lc = ToLocal(r, cell - f.cell), ln = ToLocal(r, nIn), lt = ToLocal(r, t);
            const float len = f.shape == Shape::Ramp ? kRampLen : f.shape == Shape::Twist ? kTwistLen : kBendLen;
            if (lc == p.in.cell && ln == p.in.normal && lt == p.inDir)
                x.Add(f.cell + ToWorld(r, p.out.cell), ToWorld(r, p.out.normal), ToWorld(r, p.outDir), len, 0, t);
            else if (lc == p.out.cell && ln == p.out.normal && lt == -p.outDir)
                x.Add(f.cell + ToWorld(r, p.in.cell), ToWorld(r, p.in.normal), ToWorld(r, -p.inDir), len, 0, t);
            else {
                x.blocked = true;
                return x;
            }
            x.whenNone = TraceEnd::Lost; // se acaba la rampa y no hay nada: al vacio
            x.noneLen = len;
            return x;
        }
        case Shape::BlackHole:
            x.absorbed = true;
            x.noneLen = kHoleLen;
            return x;
        case Shape::WhiteHole:
            // Sube la loma y la loma la devuelve: sale por donde entro, al reves.
            x.Add(cell, nIn, -t, 2.0f * kHoleLen, 0, t);
            x.whenNone = TraceEnd::Blocked;
            x.noneLen = 2.0f * kHoleLen;
            return x;
        default: // el gusano lo resuelve TraceLight (son dos piezas)
            return x;
    }
}

// Hacia donde sigue la luz al salir de un pozo de gusano: la direccion `d`
// que traia sobre el plano de normal `from`, llevada al plano de normal `to`.
// Entre planos paralelos (o enfrentados: piso y techo) es la misma. Entre
// perpendiculares se DOBLA como al cruzar la arista de la caja entre esos dos
// planos: la que iba hacia la pared sube por ella, la que iba a lo largo de la
// arista sigue igual. Es la unica regla que no hay que explicar: "sale hacia
// el mismo lado", y si ese lado no esta en el plano, como si el plano se
// hubiera doblado.
inline Int3 CarryDir(const Int3& d, const Int3& from, const Int3& to) {
    if (to == from || to == -from) return d;
    if (d == to) return -from;
    if (d == -to) return from;
    return d;
}

inline Trace TraceLight(const Board& b) {
    Trace tr;

    // El estado es "estoy sobre un plano" o "acabo de entrar a una pieza".
    bool onPlane = true;
    int plane = 0;
    Int3 at = b.sourceCell, dir = b.sourceDir; // sobre el plano
    bool justLanded = false;                    // recien llegada a un plano desde una pieza
    bool viaEdge = false;                       // llego por la arista desde el otro plano
    Int3 cell, nIn, t;                          // en una pieza
    bool seamIn = false;                        // entro a un cubo por una costura (ver TransferOf)

    // Cada entrada a una pieza (celda, cara, direccion) se visita una vez; el
    // tope de iteraciones solo se alcanzaria con algo roto, y entonces mejor
    // terminar que colgar el Play.
    std::vector<bool> seen(static_cast<size_t>(b.W * b.H * b.D * 72), false);
    const int maxIterations = b.W * b.H * b.D * 40 + 16;

    // D4, SIN REBOTES: por donde ya paso la luz. Las piezas por indice y las
    // casillas por plano. Entrar otra vez a una pieza o pisar otra vez una
    // casilla FRENA la luz ahi. Hasta el 24/10 se podia, y salian soluciones
    // como la de n10: ida y vuelta entre dos esferas y la pared, con el ultimo
    // tramo invisible (el plano no dibuja mas de dos).
    //
    // Dos excepciones, y las dos son la misma idea - no es volver, es seguir:
    //   - la casilla por la que la luz ENTRO a una pieza apoyada en un punto
    //     (D3): da la vuelta y sale por el mismo punto;
    //   - el agujero blanco: devolver la luz por donde vino es lo que hace.
    //     Al tocarlo se olvida el camino de antes (si no, la luz devuelta se
    //     frenaria en la primera casilla), y el mismo agujero queda usado.
    const int cellsPerPlane = b.W * b.H * b.D;
    std::vector<bool> usedPiece(b.figures.size(), false);
    std::vector<bool> usedCell(b.planes.size() * static_cast<size_t>(cellsPerPlane), false);
    auto cellSlot = [&](int p, const Int3& c) { return static_cast<size_t>(p) * cellsPerPlane + static_cast<size_t>((c.x * b.H + c.y) * b.D + c.z); };
    auto visited = [&](int p, const Int3& c) { return b.InBox(c) && usedCell[cellSlot(p, c)]; };
    auto mark = [&](int p, const Int3& c) {
        if (b.InBox(c)) usedCell[cellSlot(p, c)] = true;
    };
    auto pieceUsed = [&](const Int3& c) {
        const int i = b.IndexAt(c);
        return i >= 0 && usedPiece[static_cast<size_t>(i)];
    };
    int entryPlane = -1; // el plano y la casilla por los que entro a la pieza actual (D3)
    Int3 entryCell;

    // Probar una salida que TOCA algo (otra pieza por un puerto suyo, o un
    // plano). Deja el estado listo para seguir y devuelve 1; si lo que toca ya
    // es parte del camino (D4), devuelve 2 y la luz se frena ahi.
    auto tryExit = [&](const Exit& e) {
        const Int3 q = e.cell + e.normal;
        if (e.seam) {
            // Por una costura NO es volver: dos cubos pegados son una sola
            // caja (D1), y subir por una pila de dos pasa por el de abajo dos
            // veces - por su frente al subir, por su espalda al bajar. Si la
            // luz da la vuelta entera a una caja, `seen` la corta (ciclo).
            onPlane = false;
            cell = q;
            nIn = e.seamFace;
            t = e.dir;
            seamIn = true;
            entryPlane = -1;
            return 1;
        }
        seamIn = false;
        // Primero el PLANO: si esa cara de la celda es parte de uno, la luz cae
        // en el (un plano flotante puede estar entre dos celdas de la caja, y lo
        // que haya del otro lado queda detras de el). Si no, una pieza vecina.
        // `home`: la celda del lado declarado del plano. Una pieza del otro lado
        // (reglas 4) le devuelve la luz desde su reves, pero la luz corre por
        // las casillas del plano, que son una sola de los dos lados.
        Int3 home;
        const int p = b.PlaneOn(e.normal, e.cell, &home);
        if (p < 0) {
            if (!b.InBox(q) || !b.PortAt(q, -e.normal)) return 0;
            if (pieceUsed(q)) return 2;
            onPlane = false;
            cell = q;
            nIn = -e.normal;
            t = e.dir;
            entryPlane = -1;
            return 1;
        }
        if (visited(p, home) && !(p == entryPlane && home == entryCell)) return 2;
        onPlane = true;
        plane = p;
        at = home; // la celda de la capa: el contacto es su cara hacia el plano
        dir = e.dir;
        justLanded = true;
        viaEdge = e.atEdge; // por la arista de un cubo: el tramo empieza en el borde
        return 1;
    };

    for (int guard = 0; guard < maxIterations; ++guard) {
        if (onPlane) {
            const Face& face = b.planes[plane];
            Hop h;
            h.kind = Hop::Kind::Plane;
            h.plane = plane;
            h.from = at;
            h.dir = dir;
            h.fromEdge = viaEdge;
            viaEdge = false;
            auto isReceiver = [&](const Int3& p) { return plane == 1 && p == b.receiverCell; };
            auto isSource = [&](const Int3& p) { return plane == 0 && p == b.sourceCell; };

            // Al llegar desde una pieza, la celda de llegada ya puede ser el
            // receptor (o la fuente, que frena).
            if (justLanded && (isReceiver(at) || isSource(at))) {
                h.to = at;
                tr.hops.push_back(h);
                tr.end = isReceiver(at) ? TraceEnd::Reached : TraceEnd::Blocked;
                return tr;
            }
            justLanded = false;
            mark(plane, at);
            // Una pieza apoyada en la celda `p` del plano. Devuelve 0 si no hay,
            // 1 si la luz paso a ella y 2 si se FRENO en su borde (el tramo ya
            // quedo en el recorrido y el recorrido termina).
            //   - apoyada en una cara entera (D2): SUBE por la de enfrente si la
            //     lleva hacia arriba (ClimbsOnto); si no, se frena en su borde;
            //   - en un punto, una cinta o una lamina: entra por el centro de la
            //     celda como siempre, salvo que la pieza no la tome por ahi (una
            //     cinta de costado): entonces se frena en su borde. Hasta el
            //     24/10 el tramo llegaba hasta el centro - por debajo de la
            //     pieza - y ahi dibujaba un punto;
            //   - una pieza por la que la luz ya paso (D4): se frena en su borde.
            // El tramo que se frena en el borde termina en la arista de la celda
            // de antes, como el que sube.
            //
            // DE LOS DOS LADOS (reglas 4): primero la pieza del lado declarado
            // del plano (la celda `p`), y si no hay, la del reves (la celda de
            // enfrente, tocando con su cara `-normal`). Para la pieza es lo mismo
            // - una cara o un punto apoyado en una superficie -, solo que el
            // "arriba" de su lado es el contrario: todo lo de abajo usa `cn`, la
            // cara de la pieza que toca el plano, y nunca face.normal.
            auto meet = [&](const Int3& p) {
                Int3 pc = p, cn = face.normal;
                const Figure* f = b.PortAt(p, face.normal);
                if (!f) {
                    pc = p + face.normal;
                    cn = -face.normal;
                    f = b.InBox(pc) ? b.PortAt(pc, cn) : nullptr;
                }
                if (!f) return 0;
                Int3 n2, t2;
                bool stop = pieceUsed(pc);
                const bool climbs = !stop && ClimbsOnto(*f, cn, dir, n2, t2);
                if (!climbs && !stop)
                    stop = ContactOf(*f, cn) == Contact::Full ||
                           (f->shape != Shape::Wormhole && TransferOf(b, *f, pc, cn, dir).blocked);
                if (climbs || stop) {
                    // Si llego por una arista justo a esta celda, no hay tramo de plano.
                    if (!(p == h.from && h.fromEdge)) {
                        h.to = p - dir;
                        h.toEdge = true;
                        tr.hops.push_back(h);
                    }
                    if (stop) {
                        tr.end = TraceEnd::Blocked;
                        return 2;
                    }
                    cell = pc;
                    nIn = n2;
                    t = t2;
                    seamIn = true;
                } else {
                    h.to = p;
                    tr.hops.push_back(h);
                    cell = pc;
                    nIn = cn;
                    t = dir;
                    seamIn = false;
                }
                entryPlane = plane;
                entryCell = p;
                onPlane = false;
                return 1;
            };
            // Llegando por una arista (de la caja, o bajando de un cubo o una
            // cuna) la celda de llegada ya puede tener una pieza apoyada.
            if (h.fromEdge) {
                const int m = meet(at);
                if (m == 2) return tr;
                if (m == 1) continue;
            }
            Int3 pos = at;
            while (true) {
                // DE UN PLANO A OTRO (planos flotantes, 24/10/2026). Tres
                // aristas por las que la luz sigue; las tres son "la superficie
                // sigue", como sobre la arista de un cubo:
                //   - CONCAVA: otro plano se le levanta ENFRENTE, en el borde de
                //     esta casilla y mirando hacia ella: la luz sube por el. En
                //     la caja era solo su borde (el piso contra la pared); con
                //     planos flotantes puede estar en medio de este;
                //   - A RAS: este se acabo y otro sigue en el mismo nivel;
                //   - CONVEXA: este se acabo y otro cuelga de su borde, del lado
                //     de afuera: la luz da la vuelta y baja por el.
                // Si no, se pierde en el borde: lo que no es plano es vacio.
                //
                // Con dos caras (reglas 4), cada busqueda encuentra el plano de
                // cualquiera de sus lados (`at2` queda en su lado declarado), y
                // la pared CONCAVA puede levantarse tambien del reves: si del
                // lado declarado no hay, una que cruce por debajo lleva la luz
                // hacia ese lado.
                const Int3 next = pos + dir;
                int other = -1;
                Int3 at2 = pos, dir2 = dir;
                const Int3 under = pos + face.normal;
                int wall = b.PlaneOn(dir, pos, &at2);
                dir2 = -face.normal;
                if (!(wall >= 0 && wall != plane) && b.InBox(under)) {
                    wall = b.PlaneOn(dir, under, &at2);
                    dir2 = face.normal;
                }
                if (wall >= 0 && wall != plane) {
                    other = wall;
                } else if (!b.InPlane(next, face)) {
                    dir2 = dir;
                    other = b.PlaneOn(face.normal, next, &at2);
                    if (other < 0) {
                        const Int3 hang = next + face.normal;
                        other = b.InBox(hang) ? b.PlaneOn(-dir, hang, &at2) : -1;
                        dir2 = face.normal;
                    }
                    if (other < 0) {
                        h.to = pos;
                        h.toEdge = true;
                        tr.hops.push_back(h);
                        tr.end = TraceEnd::Lost;
                        return tr;
                    }
                }
                if (other >= 0) {
                    h.to = pos;
                    h.toEdge = true;
                    tr.hops.push_back(h);
                    if (visited(other, at2)) {
                        tr.end = TraceEnd::Blocked; // D4
                        return tr;
                    }
                    dir = dir2;
                    plane = other;
                    at = at2;
                    viaEdge = true;
                    justLanded = true;
                    break;
                }
                // D4: una casilla por la que ya paso la luz la frena en su borde.
                if (visited(plane, next) && !isReceiver(next)) {
                    h.to = pos;
                    h.toEdge = true;
                    tr.hops.push_back(h);
                    tr.end = TraceEnd::Blocked;
                    return tr;
                }
                pos = next;
                mark(plane, pos);
                if (isReceiver(pos) || isSource(pos)) {
                    h.to = pos;
                    tr.hops.push_back(h);
                    tr.end = isReceiver(pos) ? TraceEnd::Reached : TraceEnd::Blocked;
                    return tr;
                }
                // Una pieza cuya superficie llega al plano EN esta celda: la luz
                // pasa a ella. Una que ocupa la celda pero no apoya ahi (el ala
                // de una esfera de 3, un toroide acostado) la deja pasar por
                // debajo.
                const int m = meet(pos);
                if (m == 2) return tr;
                if (m == 1) break;
            }
            continue;
        }

        // --- En una pieza ---
        const Figure* f = b.PortAt(cell, nIn);
        const size_t key = static_cast<size_t>(((((cell.x * b.H + cell.y) * b.D + cell.z) * 6 + DirIndex(nIn)) * 6 + DirIndex(t)) * 2 + (seamIn ? 1 : 0));
        if (!f || (key < seen.size() && seen[key])) {
            tr.end = TraceEnd::Loop;
            return tr;
        }
        if (key < seen.size()) seen[key] = true;
        // D4: el agujero blanco olvida el camino (la luz vuelve por el, ver
        // arriba); cualquier pieza queda usada.
        if (f->shape == Shape::WhiteHole) {
            std::fill(usedPiece.begin(), usedPiece.end(), false);
            std::fill(usedCell.begin(), usedCell.end(), false);
        }
        if (const int i = b.IndexAt(cell); i >= 0) usedPiece[static_cast<size_t>(i)] = true;

        Hop h;
        h.kind = Hop::Kind::Figure;
        h.cell = cell;
        h.shape = f->shape;
        h.nIn = nIn;
        h.t = t;
        h.via = t;
        h.fromSeam = seamIn;

        // EL GUSANO es un pozo HUNDIDO en un plano de portal (23/10/2026): la
        // luz que corre por ese plano y llega a su casilla cae por la
        // garganta y sale por el pozo gemelo, sobre SU plano, siguiendo hacia
        // el mismo lado (CarryDir). Son dos tramos, uno por pozo, y despues la
        // luz sigue por el plano de salida como si hubiera llegado ahi.
        //
        // Antes fue una pieza que se ponia en cualquier celda: primero salia
        // por la MISMA cara de la otra boca (nadie lo adivinaba jugando: la luz
        // volvia hacia donde venia) y despues, por unas horas, por la cara
        // OPUESTA. Como portal libre era demasiado fuerte: dos bocas llevaban
        // la luz de cualquier punto de la caja a la pared del receptor, y los
        // niveles grandes tenian atajos de tres piezas que ningun obstaculo
        // cerraba. Atado a los planos de portal, el NIVEL decide por donde se
        // puede saltar, y eso son caminos obligatorios.
        if (f->shape == Shape::Wormhole) {
            h.len = kMouthLen;
            tr.hops.push_back(h);
            const Figure* twin = b.Twin(*f);
            const int out = twin ? b.PlaneOn(twin->sink, twin->cell) : -1;
            if (out < 0) {
                tr.end = TraceEnd::Lost; // un pozo solo no lleva a ningun lado
                return tr;
            }
            const Int3 d2 = CarryDir(t, nIn, twin->sink);
            Hop o = h;
            o.cell = twin->cell;
            o.nIn = twin->sink;
            o.t = d2;
            o.via = d2;
            o.outMouth = true;
            tr.hops.push_back(o);
            if (const int i = b.IndexAt(twin->cell); i >= 0) usedPiece[static_cast<size_t>(i)] = true;
            if (visited(out, twin->cell)) {
                tr.end = TraceEnd::Blocked; // D4: sale a una casilla por la que ya paso
                return tr;
            }
            entryPlane = -1;
            onPlane = true;
            plane = out;
            at = twin->cell;
            dir = d2;
            justLanded = false;
            viaEdge = false;
            continue;
        }

        const Transfer x = TransferOf(b, *f, cell, nIn, t, seamIn);
        h.tube = x.tube;
        if (x.blocked) {
            tr.hops.push_back(h);
            tr.end = TraceEnd::Blocked;
            return tr;
        }
        if (x.absorbed) {
            h.len = x.noneLen;
            tr.hops.push_back(h);
            tr.end = TraceEnd::Absorbed;
            return tr;
        }
        bool moved = false;
        for (int k = 0; k < x.count && !moved; ++k) {
            // El estado de tryExit se escribe solo si toca; el tramo tiene que
            // ir antes que lo que siga.
            const Exit& e = x.exits[k];
            const Int3 q = e.cell + e.normal;
            const bool touches = e.seam || b.PlaneOn(e.normal, e.cell) >= 0 || (b.InBox(q) && b.PortAt(q, -e.normal) != nullptr);
            if (!touches) continue;
            h.len = e.len;
            h.quarters = e.quarters;
            h.via = e.via;
            h.toSeam = e.seam || e.atEdge;
            tr.hops.push_back(h);
            const int r = tryExit(e);
            if (r == 2) {
                tr.end = TraceEnd::Blocked; // D4: lo que toca ya es parte del camino
                return tr;
            }
            moved = r == 1;
        }
        if (!moved) {
            h.len = x.noneLen;
            h.quarters = x.noneQuarters;
            h.via = x.noneVia;
            h.toSeam = x.noneToSeam;
            tr.hops.push_back(h);
            tr.end = x.whenNone;
            return tr;
        }
    }
    tr.end = TraceEnd::Loop;
    return tr;
}

} // namespace gravitational
