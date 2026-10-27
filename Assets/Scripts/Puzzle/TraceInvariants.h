#pragma once

// LO QUE TODA LUZ TIENE QUE CUMPLIR, sea cual sea el tablero. RuleChecks.h
// prueba casos armados a mano ("esta cuna, con esta luz, hace esto"); esto
// prueba PROPIEDADES sobre cualquier recorrido, y Tools/LevelLab las pasa por
// miles de tableros al azar (`levellab probe`) y por las soluciones de los
// niveles (`levellab check`).
//
// Por que hace falta (docs/arquitectura-luz.md): cada parche de reglas de la
// semana del 22/10 arreglo la combinacion que se veia y dejo otras rotas, y
// ninguna prueba lo noto - las pruebas por caso solo miran los casos que
// alguien penso. "La luz no se dibuja" o "la luz pasa por debajo de un cubo"
// no estaban escritas en ningun lado: ahora son invariantes con nombre.
//
// Cada invariante es FIRME (ya se cumple: si falla, es un error) o PENDIENTE
// (decidida el 24/10/2026, D1-D4 de la revision, pero su fase todavia no la
// implemento). Las pendientes se cuentan y se muestran sin hacer fallar nada;
// cuando su fase las cumpla, pasan a firmes poniendo `phase` en 0 aca, y desde
// ahi una regla nueva que las rompa falla en `levellab test`.
//
// Sin motor.

#include "BeamPath.h"

#include <map>
#include <string>
#include <vector>

namespace gravitational {

// Como toca cada pieza al plano (Contact, ContactOf) vive en LightTrace.h: es
// lo que el trazado usa para decidir, y aca solo se comprueba.

struct Invariant {
    const char* id;    // corto, para agrupar
    const char* what;  // la frase
    int phase;         // 0 = firme; si no, la fase de docs/arquitectura-luz.md que la cumple
    bool solutions;    // firme YA para las soluciones de los niveles (levellab check y gen)
    bool pending() const { return phase > 0; }
    bool HardFor(bool solution) const { return !pending() || (solution && solutions); }
};

// El orden es el del informe.
//
// `dibujo` es firme para las SOLUCIONES y pendiente en general: un jugador que
// prueba puede armar un recorrido que pase tres veces por un plano (eso lo
// arregla la Fase 3, dibujando sin limite), pero un nivel no puede pedirle una
// solucion que no se ve entera. Las de D2-D4 son firmes desde la Fase 2
// (24/10/2026): `levellab probe` las dejo en cero en 13.580 recorridos.
inline const Invariant kInvariants[] = {
    {"dibujo", "cada pieza recibe como mucho kDrawSlots tramos, y cada plano kPlaneDrawSlots (los demas no se dibujan)", 3, true},
    {"continuidad", "un tramo empieza donde termino el anterior (arista con arista, centro con centro)", 0, true},
    {"por-debajo", "D2: la luz del plano no entra (ni sale) por una cara ENTERA apoyada: sube por la de enfrente o se frena en ella", 0, true},
    {"frena-debajo", "D2: la luz que se frena contra una pieza se frena en su borde, no debajo de ella", 0, true},
    {"orbita-apoyada", "D3: la luz que da la vuelta a una pieza apoyada vuelve al plano por el punto de contacto", 0, true},
    {"rebote", "D4: la luz no pasa dos veces por la misma pieza ni por la misma casilla de un plano (salvo el agujero blanco)", 0, true},
};

inline const Invariant& InvariantById(const std::string& id) {
    for (const Invariant& i : kInvariants)
        if (id == i.id) return i;
    return kInvariants[0];
}

struct Violation {
    const char* id;
    std::string detail;
};

// Las invariantes que el recorrido `t` sobre el tablero `b` no cumple.
inline std::vector<Violation> CheckTrace(const Board& b, const Trace& t) {
    std::vector<Violation> out;
    auto at = [](const Int3& c) { return std::to_string(c.x) + " " + std::to_string(c.y) + " " + std::to_string(c.z); };

    // --- dibujo ---
    std::map<int, int> perPlane, perFigure;
    for (const Hop& h : t.hops) {
        if (h.kind == Hop::Kind::Plane) ++perPlane[h.plane];
        else ++perFigure[b.IndexAt(h.cell)];
    }
    for (const auto& [plane, n] : perPlane)
        if (n > kPlaneDrawSlots) out.push_back({"dibujo", "plano " + std::to_string(plane) + " tiene " + std::to_string(n) + " tramos"});
    for (const auto& [index, n] : perFigure)
        if (index >= 0 && n > kDrawSlots)
            out.push_back({"dibujo", std::string(Info(b.figures[static_cast<size_t>(index)].shape).key) + " " +
                                         at(b.figures[static_cast<size_t>(index)].cell) + " tiene " + std::to_string(n) + " tramos"});

    for (size_t i = 0; i + 1 < t.hops.size(); ++i) {
        const Hop &p = t.hops[i], &q = t.hops[i + 1];
        // --- continuidad --- (el gusano tiene su propio empalme: BeamPath.h)
        const bool mouth = p.outMouth || q.outMouth || (p.kind == Hop::Kind::Figure && p.shape == Shape::Wormhole) ||
                           (q.kind == Hop::Kind::Figure && q.shape == Shape::Wormhole);
        const bool pEdge = p.kind == Hop::Kind::Plane ? p.toEdge : p.toSeam;
        const bool qEdge = q.kind == Hop::Kind::Plane ? q.fromEdge : q.fromSeam;
        if (!mouth && pEdge != qEdge) out.push_back({"continuidad", "tramo " + std::to_string(i) + (pEdge ? " termina en arista" : " termina en centro") +
                                                                      (qEdge ? ", el siguiente empieza en arista" : ", el siguiente empieza en centro")});

        // --- por-debajo, al SALIR: de una pieza apoyada en una cara entera al
        // plano por el centro de esa cara (la luz cruzo por debajo) ---
        if (p.kind == Hop::Kind::Figure && !p.toSeam && q.kind == Hop::Kind::Plane && !q.fromEdge) {
            // De cualquiera de los dos lados del plano (reglas 4): la pieza que
            // sale puede estar apoyada en su reves.
            const Int3 n = b.planes[static_cast<size_t>(q.plane)].normal;
            const bool back = b.IndexAt(q.from) != b.IndexAt(p.cell);
            const Int3 c = back ? q.from + n : q.from;
            const Figure* f = b.InBox(c) ? b.FigureAt(c) : nullptr;
            if (f && b.IndexAt(c) == b.IndexAt(p.cell) && ContactOf(*f, back ? -n : n) == Contact::Full)
                out.push_back({"por-debajo", std::string(Info(f->shape).key) + " " + at(p.cell) + " (sale por la cara apoyada)"});
        }

        // --- por-debajo y frena-debajo, al ENTRAR ---
        if (p.kind != Hop::Kind::Plane || q.kind != Hop::Kind::Figure || q.fromSeam) continue;
        // La cara de entrada es la que toca el plano, de su lado declarado (n) o
        // del reves (-n, reglas 4).
        const Int3 n = b.planes[static_cast<size_t>(p.plane)].normal;
        const Figure* f = b.PortAt(q.cell, q.nIn);
        if (!f || (q.nIn != n && q.nIn != -n)) continue;
        if (q.nIn == -n && q.cell != p.to + n) continue;
        const Contact c = ContactOf(*f, q.nIn);
        const bool stops = i + 2 == t.hops.size() && t.end == TraceEnd::Blocked;
        if (c == Contact::Full) out.push_back({"por-debajo", std::string(Info(f->shape).key) + " " + at(q.cell) + (stops ? " (y se frena ahi)" : " (y sigue)")});
        else if (c == Contact::Tangent && stops) out.push_back({"frena-debajo", std::string(Info(f->shape).key) + " " + at(q.cell)});
    }

    // --- orbita-apoyada ---
    if (t.end == TraceEnd::Orbit && t.hops.size() >= 2 && t.hops.back().kind == Hop::Kind::Figure &&
        t.hops[t.hops.size() - 2].kind == Hop::Kind::Plane)
        out.push_back({"orbita-apoyada", std::string(Info(t.hops.back().shape).key) + " " + at(t.hops.back().cell)});

    // --- rebote --- con las dos excepciones de TraceLight: lo que devuelve un
    // agujero blanco empieza de cero, y la casilla por la que la luz entro a
    // una pieza apoyada en un punto es tambien por la que sale (D3).
    std::map<int, int> pieces;
    std::map<std::pair<int, std::string>, int> cells;
    for (size_t i = 0; i < t.hops.size(); ++i) {
        const Hop& h = t.hops[i];
        if (h.kind == Hop::Kind::Figure) {
            if (h.shape == Shape::WhiteHole) {
                pieces.clear();
                cells.clear();
            }
            // Por una costura es la misma caja (ver tryExit en TraceLight).
            const bool bySeam = h.fromSeam && i > 0 && t.hops[i - 1].kind == Hop::Kind::Figure;
            const int index = b.IndexAt(h.cell);
            if (index >= 0 && !bySeam && ++pieces[index] == 2)
                out.push_back({"rebote", std::string(Info(h.shape).key) + " " + at(b.figures[static_cast<size_t>(index)].cell)});
            continue;
        }
        const bool through = !h.fromEdge && i >= 2 && t.hops[i - 1].kind == Hop::Kind::Figure && t.hops[i - 2].kind == Hop::Kind::Plane &&
                             t.hops[i - 2].plane == h.plane && t.hops[i - 2].to == h.from;
        Int3 c = h.from;
        for (int step = 0; step <= 32; ++step, c = c + h.dir) { // un tramo no cruza mas de 16 casillas: el tope es solo por si acaso
            if (!(step == 0 && through) && ++cells[{h.plane, at(c)}] == 2)
                out.push_back({"rebote", "plano " + std::to_string(h.plane) + ", la casilla " + at(c)});
            if (c == h.to) break;
        }
    }
    return out;
}

} // namespace gravitational
