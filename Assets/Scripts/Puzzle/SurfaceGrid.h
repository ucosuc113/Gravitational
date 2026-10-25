#pragma once

// LA SUPERFICIE COMO CUADRICULA: que celdas hay y a cual se llega dando un paso.
//
// Sin motor a proposito (ningun "Engine/..."): la luz se va a probar con un
// `for`, sin ventana ni GPU, y eso solo se puede si esto no depende de nada.
//
// Las coordenadas son las de la UV de la superficie, que es lo que dibuja
// Surface.hlsl: la celda (x, y) es la que cubre
//     u en [x/N, (x+1)/N)   y   v en [y/N, (y+1)/N)
// asi que x crece con u e y crece con v. En la primitiva Plane, v = 0 queda del
// lado -Z (el de la camara en su posicion inicial) y v crece hacia +Z: medido
// con una pantalla de prueba atada al plano. Por eso NORTE es y + 1: hacia +Z,
// lejos de la camara, que en pantalla se ve HACIA ARRIBA. Con Norte = y - 1 la
// luz "al Norte" bajaba en la pantalla, y disenar un puzzle asi es pelear con
// la brujula. Fijarlo aqui y no en el shader es lo que va a hacer que el Dia 3
// la luz se dibuje en la misma celda en la que la simulacion dice que esta.

namespace gravitational {

// El sentido en que VIAJA la luz, no un lado de la celda. "Entro por el lado
// Oeste" es "viaja hacia el Este".
enum class Dir { North, East, South, West };

struct Cell {
    int x = 0;
    int y = 0;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y; }
};

// Un paso: a que celda se llega, y con que sentido se SIGUE viajando.
//
// Que `heading` vuelva aunque en el plano sea siempre el mismo es la decision
// de este archivo. En una cinta de Mobius la luz que cruza la costura sale del
// otro lado dada vuelta (Norte pasa a ser Sur), y en un toro sale por un borde
// y entra por el opuesto. Si Neighbor solo devolviera la celda, esas
// superficies obligarian a reescribir la luz; asi, cambian Neighbor y nada mas.
struct Step {
    bool valid = false; // false = el paso se sale de la superficie
    Cell cell;
    Dir heading = Dir::North;
};

inline Dir Opposite(Dir d) {
    switch (d) {
        case Dir::North: return Dir::South;
        case Dir::East: return Dir::West;
        case Dir::South: return Dir::North;
        case Dir::West: return Dir::East;
    }
    return d;
}

// En el sentido de las agujas del reloj, mirando la superficie desde arriba
// con el Norte hacia arriba.
inline Dir TurnRight(Dir d) {
    switch (d) {
        case Dir::North: return Dir::East;
        case Dir::East: return Dir::South;
        case Dir::South: return Dir::West;
        case Dir::West: return Dir::North;
    }
    return d;
}

inline Dir TurnLeft(Dir d) { return TurnRight(TurnRight(TurnRight(d))); }

// Cuanto se mueve una celda al dar un paso en ese sentido.
inline int DeltaX(Dir d) { return d == Dir::East ? 1 : d == Dir::West ? -1 : 0; }
inline int DeltaY(Dir d) { return d == Dir::North ? 1 : d == Dir::South ? -1 : 0; }

// Un plano de N x N celdas. Sus bordes son bordes: la luz que sale, se pierde.
class SurfaceGrid {
public:
    explicit SurfaceGrid(int size) : m_size(size > 0 ? size : 1) {}

    int Size() const { return m_size; }

    bool InBounds(Cell c) const { return c.x >= 0 && c.y >= 0 && c.x < m_size && c.y < m_size; }

    Step Neighbor(Cell from, Dir heading) const {
        Step s;
        s.cell = Cell{from.x + DeltaX(heading), from.y + DeltaY(heading)};
        s.heading = heading; // en el plano el sentido no cambia nunca
        s.valid = InBounds(s.cell);
        return s;
    }

private:
    int m_size;
};

} // namespace gravitational
