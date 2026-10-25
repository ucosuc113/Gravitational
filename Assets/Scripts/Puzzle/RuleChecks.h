#pragma once

// LAS PRUEBAS DE LAS PIEZAS NUEVAS, sin motor: un tablero chico armado a mano
// por pieza, y lo que la luz tiene que hacer en el. Las corren TraceCheck (en
// el Play, al lado de las de siempre) y Tools/LevelLab (`levellab test`, en un
// segundo y sin abrir el Editor).
//
// Cada tablero se escribe en el formato de nivel y no armando el Board a mano:
// asi la prueba tambien dice si el formato entiende la pieza.
//
// `expect(ok, que, dio)`.

#include "BeamPath.h"
#include "PieceLook.h"

#include <cmath>
#include <string>

namespace gravitational {

template <class Expect>
void RunRuleChecks(Expect&& expect) {
    auto board = [&](const char* text) {
        Board b;
        std::string error;
        if (!Board::Parse(text, b, error)) expect(false, "un tablero de prueba se lee", error);
        return b;
    };
    auto end = [](const Trace& t) { return std::to_string(static_cast<int>(t.end)) + " / " + std::to_string(t.hops.size()) + " tramos"; };
    auto place = [](Board& b, const char* line) {
        std::istringstream in(line);
        std::vector<Figure> got;
        std::string error;
        if (!Board::ParseFigure(in, got, error)) return false;
        b.inventory[static_cast<int>(got[0].shape)] += static_cast<int>(got.size());
        for (const Figure& f : got)
            if (!b.Place(f)) return false;
        return true;
    };

    // --- Orientaciones: 24 distintas, propias, y la identidad donde se dice ---
    {
        bool distinct = true, proper = true;
        for (int i = 0; i < kOrientations; ++i) {
            const Rot a = Orientation(i);
            proper = proper && Dot(a.f, Cross(a.u, a.s)) == 1;
            for (int j = 0; j < i; ++j) {
                const Rot b = Orientation(j);
                if (a.f == b.f && a.u == b.u) distinct = false;
            }
        }
        const Rot id = Orientation(kIdentity);
        expect(distinct && proper, "las 24 orientaciones son distintas y propias", "repetidas o espejadas");
        expect(id.f == kPosX && id.u == kPosY && id.s == kPosZ, "la orientacion 8 es la identidad", "otra");
        // Cada una tiene sus angulos de Transform (90 grados por eje): si una
        // faltara, esa pieza se dibujaria sin girar y la luz pasaria por otro lado.
        int found = 0;
        for (int i = 0; i < kOrientations; ++i) {
            float e[3];
            found += EulerOf(Orientation(i), e) ? 1 : 0;
        }
        expect(found == kOrientations, "las 24 orientaciones tienen angulos para el motor", std::to_string(found));
    }

    // --- Esfera de 3: toca solo por el centro de cada cara de su caja ---
    {
        Board b = board("start floor\nend ceiling\nsize 4 3 3\nsource 0 0 1 right\nreceiver 0 2 1\ninventory sphere3 1\n");
        b.Place(Shape::Sphere3, {2, 1, 1});
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 3, "esfera de 3: del piso al techo por su media vuelta", end(t));
        if (t.hops.size() == 3)
            expect(t.hops[1].shape == Shape::Sphere3 && t.hops[1].quarters == 2 && std::fabs(t.hops[1].len - 1.5f * kPiF) < 1e-3f,
                   "esfera de 3: media vuelta mide 1,5 pi", std::to_string(t.hops[1].len));
        Board c = board("start floor\nend ceiling\nsize 4 3 3\nsource 0 0 0 right\nreceiver 0 2 1\ninventory sphere3 1\n");
        c.Place(Shape::Sphere3, {2, 1, 1});
        const Trace u = TraceLight(c);
        expect(u.end == TraceEnd::Lost && u.hops.size() == 1, "esfera de 3: la luz pasa por debajo de su ala", end(u));
    }

    // --- Toroide: anillo, agujero y tubo ---
    {
        const char* base = "start left\nend right\nsize 3 3 3\nsource 0 1 1 up\nreceiver 2 2 1\n";
        Board b = board(base);
        b.inventory[static_cast<int>(Shape::Torus)] = 3;
        b.Place(Shape::Torus, {0, 2, 1}, 2);
        b.Place(Shape::Torus, {1, 2, 1}, 2);
        b.Place(Shape::Torus, {2, 2, 1}, 2);
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 5, "toroide de eje z: el puente del tutorial", end(t));

        Board c = board(base);
        c.inventory[static_cast<int>(Shape::Torus)] = 1;
        c.Place(Shape::Torus, {0, 2, 1}, 0);
        const Trace u = TraceLight(c);
        expect(u.end == TraceEnd::Lost && u.hops.size() == 1, "toroide de eje x: por su agujero no toca la pared", end(u));

        Board d = board(base);
        d.inventory[static_cast<int>(Shape::Torus)] = 1;
        d.Place(Shape::Torus, {0, 2, 1}, 1);
        const Trace v = TraceLight(d);
        expect(v.end == TraceEnd::Lost && v.hops.size() == 3 && v.hops[1].tube, "toroide de eje y: da la vuelta al tubo y sigue", end(v));
    }

    // --- Piramide: base y punta, y en la punta DOBLA ---
    {
        // Entra por la base yendo a +X y sale por la punta yendo a +Z: dobla
        // 90 grados. La esfera del costado no la desvia (la piramide no toca
        // por sus lados).
        Board b = board("start floor\nend ceiling\nsize 3 2 3\nsource 0 0 1 right\nreceiver 1 1 0\ninventory sphere 2\n");
        place(b, "pyramid 1 0 1 up");
        b.Place(Shape::Sphere, {2, 0, 1});
        b.Place(Shape::Sphere, {1, 1, 1});
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 4 && t.hops[1].shape == Shape::Pyramid,
               "piramide: del piso a la esfera de arriba, sin desviarse por la del costado", end(t));
        if (t.hops.size() == 4) {
            // Apoyada en el piso, la luz del piso SUBE por la cara de enfrente
            // (de la arista a la punta, 1,118), no cruza la base por debajo.
            expect(t.hops[1].fromSeam && std::fabs(t.hops[1].len - kPyramidSlant) < 1e-4f, "piramide: sube por la cara de enfrente, 1,118",
                   std::to_string(t.hops[1].len));
            expect(t.hops[2].t == kPosZ, "piramide: en la punta la luz dobla de +X a +Z", Board::DirKey(t.hops[2].t));
        }
        // PEGADA A UNA PARED, con la punta al aire: sube, dobla, baja por la
        // cara de al lado y sigue por la pared. Antes daba la vuelta entera y
        // se quedaba en orbita (el nivel 13, 24/10/2026).
        Board w = board("start left\nend right\nsize 3 3 3\nsource 0 0 1 up\nreceiver 2 2 2\n");
        place(w, "pyramid 0 1 1 right");
        const Trace p = TraceLight(w);
        expect(p.hops.size() >= 3 && p.hops[1].shape == Shape::Pyramid && p.hops[1].fromSeam && p.hops[2].kind == Hop::Kind::Plane &&
                   p.hops[2].plane == 0 && p.hops[2].fromEdge && std::fabs(p.hops[1].len - 2.0f * kPyramidSlant) < 1e-4f,
               "piramide en la pared: sube, dobla y vuelve a la pared por la arista", end(p));
        expect(p.end != TraceEnd::Orbit, "piramide en la pared: no da la vuelta entera", end(p));
        Board c = board("start floor\nend ceiling\nsize 3 2 3\nsource 0 0 1 right\nreceiver 2 1 1\n");
        place(c, "pyramid 1 0 1 right");
        const Trace u = TraceLight(c);
        expect(u.end == TraceEnd::Lost && u.hops.size() == 1, "piramide acostada: su lado inclinado no toca el piso", end(u));
        // Base abajo o punta abajo doblan para lados distintos: son dos posturas.
        expect(c.PlacementsFor(Shape::Pyramid, {2, 0, 0}, kPosY).size() == 2, "piramide sobre el piso: dos posturas (dobla a un lado o al otro)",
               std::to_string(c.PlacementsFor(Shape::Pyramid, {2, 0, 0}, kPosY).size()));
    }

    // --- La luz SALE DE SU PLANO (el defecto del 21/10/2026: no salia nunca) ---
    {
        // Una esfera con un vecino al costado le pasa la luz a el: la luz deja
        // la rebanada z = 1 donde nacio.
        const char* base = "start left\nend right\nsize 3 3 3\nsource 0 1 1 up\nreceiver 2 2 2\ninventory sphere 4\n";
        Board b = board(base);
        b.Place(Shape::Sphere, {0, 2, 1});
        b.Place(Shape::Sphere, {0, 2, 2});
        b.Place(Shape::Sphere, {1, 2, 2});
        b.Place(Shape::Sphere, {2, 2, 2});
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached, "esfera: tira la luz hacia el vecino del costado y sale de su plano", end(t));
        if (t.hops.size() > 1) expect(t.hops[1].via == kPosZ, "esfera: el arco de la primera va al costado (+Z)", Board::DirKey(t.hops[1].via));

        // El cubo: la luz que llega a lo largo del eje, la franja la dobla
        // hacia donde gira; el sentido opuesto dobla al reves. Por la tapa, frena.
        // Desde OTRA pieza (D2: desde el plano, un cubo de eje a lo largo de la
        // luz la frena): la esfera de la pared le pasa al cubo de al lado la luz
        // que baja, por su cara -X, a lo largo de su eje Y.
        Board c = board(base);
        place(c, "sphere 0 2 1");
        place(c, "cube 1 2 1 y");
        const Trace u = TraceLight(c);
        expect(u.hops.size() > 2 && u.hops[2].shape == Shape::Cube && u.hops[2].via == kPosZ, "cubo +Y: la luz que llega por su eje la dobla a +Z",
               u.hops.size() > 2 ? Board::DirKey(u.hops[2].via) : end(u));
        Board d = board(base);
        place(d, "sphere 0 2 1");
        place(d, "cube 1 2 1 -y");
        const Trace v = TraceLight(d);
        expect(v.hops.size() > 2 && v.hops[2].via == kNegZ, "cubo -Y: la dobla al reves, a -Z", v.hops.size() > 2 ? Board::DirKey(v.hops[2].via) : end(v));
        Board e = board(base);
        place(e, "cube 0 2 1 x");
        expect(TraceLight(e).end == TraceEnd::Blocked, "cubo de eje X entrando por la tapa: frena", end(TraceLight(e)));
        Board g = board(base);
        place(g, "cube 0 2 1 y");
        int states = 1;
        while (g.Rotate({0, 2, 1}) && g.figures[0].axis != 1 && states < 10) ++states;
        expect(states == 6, "cubo: seis posturas (tres ejes, dos sentidos)", std::to_string(states));
    }

    // --- Cubos pegados: una sola caja; y la luz del plano los SUBE ---
    // Dos cubos en el piso, uno al lado del otro y con la franja en el mismo
    // eje: la luz sube por la cara de enfrente del primero, cruza por arriba de
    // los dos (la costura) y baja por la de atras del segundo al piso. Antes
    // subia por la cara de en medio y rebotaba entre los dos (siete tramos), y
    // el 22/10/2026 pasaba por debajo. Con ejes distintos la cara de en medio
    // sigue siendo un contacto.
    {
        const char* base = "start floor 0 0 0 4 0 2\nend ceiling 0 2 0 0 2 2\nsize 5 3 3\nsource 0 0 1 right\nreceiver 0 2 1\n";
        Board b = board(base);
        place(b, "cube 1 0 1 z");
        place(b, "cube 2 0 1 -z");
        const Trace t = TraceLight(b);
        expect(t.hops.size() == 4 && t.hops[1].fromSeam && t.hops[1].toSeam && t.hops[2].fromSeam && t.hops[3].kind == Hop::Kind::Plane &&
                   t.hops[3].from == Int3{3, 0, 1} && t.hops[3].fromEdge && t.hops[3].dir == kPosX,
               "cubos pegados: la luz sube, cruza por arriba de los dos y baja al piso", std::to_string(t.hops.size()) + " tramos, " + end(t));
        if (t.hops.size() == 4)
            expect(std::fabs(t.hops[1].len - 2.0f) < 1e-4f && std::fabs(t.hops[2].len - 2.0f) < 1e-4f,
                   "cubos pegados: dos metros sobre cada uno (una cara entera y la mitad de dos)", std::to_string(t.hops[1].len) + " / " + std::to_string(t.hops[2].len));

        Board s1 = board(base);
        place(s1, "cube 1 0 1 z");
        const Trace u1 = TraceLight(s1);
        expect(u1.hops.size() == 3 && std::fabs(u1.hops[1].len - 3.0f) < 1e-4f && u1.hops[2].from == Int3{2, 0, 1},
               "cubo en el piso: un bache - sube, cruza y baja al piso del otro lado", std::to_string(u1.hops.size()) + " tramos, " + end(u1));

        Board s2 = board(base);
        place(s2, "cube 1 0 1 z");
        place(s2, "cube 1 1 1 -z");
        const Trace u2 = TraceLight(s2);
        expect(u2.hops.size() == 5 && u2.hops[2].cell == Int3{1, 1, 1} && u2.hops[4].from == Int3{2, 0, 1},
               "cubos apilados: la luz sube por los dos y baja del otro lado", std::to_string(u2.hops.size()) + " tramos, " + end(u2));

        Board s3 = board(base);
        place(s3, "cube 1 0 1 x");
        const Trace u3 = TraceLight(s3);
        expect(u3.end == TraceEnd::Blocked && u3.hops.size() == 1 && u3.hops[0].toEdge && u3.hops[0].to == Int3{0, 0, 1},
               "D2: cubo con el eje a lo largo de la luz: la tapa la frena en su borde (no entra por abajo)", end(u3));
        Board s4 = board(base);
        place(s4, "cube 1 0 1 y");
        const Trace u4 = TraceLight(s4);
        expect(u4.end == TraceEnd::Blocked && u4.hops.size() == 1 && u4.hops[0].toEdge, "D2: cubo de eje vertical: su franja no sube, la frena en su borde",
               end(u4));

        Board c = board(base);
        place(c, "cube 1 0 1 z");
        place(c, "cube 2 0 1 y");
        const Trace u = TraceLight(c);
        expect(u.hops.size() > 2 && !u.hops[1].toSeam && u.hops[2].nIn == kNegX, "cubos de ejes distintos: la cara de en medio sigue siendo un contacto",
               u.hops.size() > 2 ? Board::DirKey(u.hops[2].nIn) : end(u));
    }

    // --- La cara pegada a un plano: la luz baja a el por la arista ---
    // Un cubo contra la pared, la luz bajando por su frente y el receptor en la
    // pared justo debajo: pasa a la pared en la arista de abajo y sigue BAJANDO.
    // Antes llegaba al centro de la cara pegada y salia hacia arriba.
    {
        Board b = board("start ceiling 0 3 0 3 3 2\nend back 0 0 3 3 3 3\nsize 4 4 4\nsource 1 3 0 back\nreceiver 1 2 3\n");
        b.inventory[static_cast<int>(Shape::Sphere)] = 1;
        b.Place(Shape::Sphere, {1, 3, 2});
        place(b, "cube 1 3 3 x");
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 4 && t.hops[2].toSeam && t.hops[3].fromEdge,
               "cara pegada: el cubo contra la pared baja la luz a la pared por la arista", std::to_string(t.hops.size()) + " tramos, " + end(t));
    }
    // PLANOS FLOTANTES (24/10/2026): un plano puede estar en cualquier lugar.
    // En el piso se levanta una pared (un plano vertical en medio de la caja) y
    // de su borde de arriba sale un estante del otro lado. La luz del piso sube
    // por la pared (arista CONCAVA, aunque el piso siga mas alla), da la vuelta
    // por arriba y sigue por el estante (CONVEXA); al final del estante, nada:
    // se pierde. Sin piezas.
    {
        const char* base = "start floor\nend ceiling 5 3 0 5 3 2\nsize 6 4 3\nsource 0 0 1 right\nreceiver 5 3 1\n";
        Board b = board((std::string(base) + "panel right 2 0 0 2 1 2\npanel floor 3 2 0 4 2 2\n").c_str());
        const Trace t = TraceLight(b);
        expect(t.hops.size() == 3 && t.hops[1].plane == 2 && t.hops[1].dir == kPosY && t.hops[2].plane == 3 && t.hops[2].fromEdge &&
                   t.hops[2].dir == kPosX && t.end == TraceEnd::Lost,
               "planos flotantes: sube por una pared en medio del piso y da la vuelta a un estante", std::to_string(t.hops.size()) + " tramos, " + end(t));
        // Dos planos EN EL MISMO NIVEL y pegados: la luz pasa de uno al otro a ras.
        // Con un hueco entre los dos, se pierde en el borde.
        const char* row = "end ceiling 5 3 0 5 3 2\nsize 6 4 3\nsource 0 0 1 right\nreceiver 5 3 1\nstart floor 0 0 0 1 0 2\n";
        Board f = board((std::string(row) + "panel floor 2 0 0 3 0 2\n").c_str());
        const Trace u = TraceLight(f);
        expect(u.hops.size() == 2 && u.hops[1].plane == 2 && u.hops[1].fromEdge && u.hops[1].dir == kPosX,
               "planos flotantes: de un plano al de al lado, en el mismo nivel", std::to_string(u.hops.size()) + " tramos, " + end(u));
        Board g = board((std::string(row) + "panel floor 3 0 0 4 0 2\n").c_str());
        const Trace v = TraceLight(g);
        expect(v.hops.size() == 1 && v.end == TraceEnd::Lost, "planos flotantes: con un hueco entre los dos, se pierde", end(v));
        // A RAS POR ARRIBA: un estante una casilla mas alto empieza justo donde
        // termina la franja. La cuna (pendiente de frente) y el cubo suben la
        // luz a el: la punta de la pendiente y la tapa del cubo estan a su nivel.
        const char* step = "start floor 0 0 0 2 0 0\nend ceiling 5 3 0 5 3 0\nsize 6 4 1\nsource 0 0 0 right\nreceiver 5 3 0\npanel floor 3 1 0 5 1 0\n";
        for (const char* piece : {"prism 2 0 0 down right", "cube 2 0 0 z"}) {
            Board s = board(step);
            place(s, piece);
            const Trace w = TraceLight(s);
            expect(w.hops.size() >= 3 && w.hops[2].kind == Hop::Kind::Plane && w.hops[2].plane == 2 && w.hops[2].fromEdge && w.hops[2].dir == kPosX,
                   std::string("a ras por arriba: ") + piece + " sube la luz al estante", std::to_string(w.hops.size()) + " tramos, " + end(w));
        }
    }

    // D3: una esfera SOLA en el piso. La luz entra por el punto de apoyo, da la
    // vuelta y sale por el mismo punto al piso, siguiendo para el mismo lado.
    // Hasta el 24/10/2026 orbitaba para siempre.
    {
        Board b = board("start floor\nend ceiling\nsize 5 2 3\nsource 0 0 1 right\nreceiver 4 1 1\n");
        place(b, "sphere 2 0 1");
        const Trace t = TraceLight(b);
        expect(t.end != TraceEnd::Orbit && t.hops.size() == 3 && t.hops[1].quarters == 4 && t.hops[2].kind == Hop::Kind::Plane &&
                   t.hops[2].from == Int3{2, 0, 1} && t.hops[2].dir == kPosX,
               "D3: la esfera apoyada da la vuelta y devuelve la luz al piso", std::to_string(t.hops.size()) + " tramos, " + end(t));
    }
    // D4: la luz que se cruza consigo misma se FRENA en el borde de la casilla
    // por la que ya paso. Tres piramides en el piso la hacen girar tres veces
    // (en la punta dobla siempre para el mismo lado) y la vuelven a la casilla
    // 1 0 1, por la que salio.
    {
        Board b = board("start floor\nend ceiling\nsize 5 2 5\nsource 0 0 1 right\nreceiver 4 1 4\n");
        place(b, "pyramid 3 0 1 up");
        place(b, "pyramid 3 0 3 up");
        place(b, "pyramid 1 0 3 up");
        const Trace t = TraceLight(b);
        const Hop& last = t.hops.back();
        expect(t.end == TraceEnd::Blocked && last.kind == Hop::Kind::Plane && last.to == Int3{1, 0, 2} && last.toEdge,
               "D4: la luz no vuelve a pisar una casilla: se frena en su borde", std::to_string(t.hops.size()) + " tramos, " + end(t));
        // Salvo lo que devuelve un agujero blanco: vuelve por donde vino.
        Board w = board("start floor\nend ceiling\nsize 5 2 3\nsource 0 0 1 right\nreceiver 4 1 1\nfixed whitehole 3 0 1\n");
        const Trace r = TraceLight(w);
        expect(r.end == TraceEnd::Blocked && r.hops.size() == 3 && r.hops[2].dir == kNegX && r.hops[2].to == Int3{0, 0, 1},
               "D4: el agujero blanco devuelve la luz por las casillas por las que vino", std::to_string(r.hops.size()) + " tramos, " + end(r));
    }

    // --- Prisma (la cuna) apoyado: la luz del plano lo SUBE ---
    // Desde el lado de la pendiente sube por la hipotenusa y baja por el
    // cateto de pie, que en su centro le pasa la luz a lo que lo toque; desde
    // el lado recto sube por el cateto y baja por la pendiente al piso.
    {
        const char* base = "start floor 0 0 0 1 0 0\nend right\nsize 3 3 1\nsource 0 0 0 right\nreceiver 2 0 0\n";
        Board b = board(base);
        b.inventory[static_cast<int>(Shape::Sphere)] = 1;
        place(b, "prism 1 0 0 right down");
        b.Place(Shape::Sphere, {2, 0, 0});
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 4, "prisma: sube la pendiente y el cateto de pie le pasa la luz a la esfera", end(t));
        if (t.hops.size() == 4)
            expect(std::fabs(t.hops[1].len - (kPrismLong - kPrismShort + 0.5f)) < 1e-4f, "prisma: la pendiente y medio cateto miden raiz 2 + 0,5",
                   std::to_string(t.hops[1].len));

        Board c = board("start floor 0 0 0 3 0 0\nend ceiling 0 2 0 0 2 0\nsize 4 3 1\nsource 0 0 0 right\nreceiver 0 2 0\n");
        place(c, "prism 1 0 0 left down");
        const Trace u = TraceLight(c);
        expect(u.hops.size() == 3 && std::fabs(u.hops[1].len - kPrismLong) < 1e-4f &&
                   u.hops[2].fromEdge && u.hops[2].from == Int3{2, 0, 0},
               "prisma: desde el lado recto sube el cateto, baja la pendiente y sigue por el piso", std::to_string(u.hops.size()) + " tramos, " + end(u));
        expect(c.PlacementsFor(Shape::Prism, {2, 0, 0}, kPosY).size() == 4, "prisma sobre el piso: cuatro posturas",
               std::to_string(c.PlacementsFor(Shape::Prism, {2, 0, 0}, kPosY).size()));
    }

    // --- Rampa: del piso a la pared, y de vuelta ---
    {
        Board b = board("start floor 0 0 0 1 0 0\nend right 2 1 0 2 2 0\nsize 3 3 1\nsource 0 0 0 right\nreceiver 2 2 0\n");
        place(b, "ramp 1 0 0 right up");
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 3, "rampa: sube del piso a la pared", end(t));
        const BeamPath path = BuildBeamPath(b, t);
        expect(std::fabs(path.length - (2.0f + kRampLen)) < 1e-3f, "rampa: el recorrido mide 2 + 0,75 pi", std::to_string(path.length));

        Board c = board("start right 2 1 0 2 2 0\nend floor 0 0 0 1 0 0\nsize 3 3 1\nsource 2 2 0 down\nreceiver 0 0 0\n");
        place(c, "ramp 1 0 0 right up");
        const Trace u = TraceLight(c);
        expect(u.end == TraceEnd::Reached && u.hops.size() == 3, "rampa: al reves, baja de la pared al piso", end(u));

        Board d = board("start floor 0 0 0 1 0 0\nend right 2 1 0 2 2 0\nsize 3 3 1\nsource 0 0 0 right\nreceiver 2 2 0\n");
        const std::vector<Figure> list = d.PlacementsFor(Shape::Ramp, {1, 0, 0}, kPosY);
        expect(!list.empty() && list[0].orient == kIdentity && list[0].cell == Int3{1, 0, 0},
               "rampa puesta en el piso: entra apoyada, mirando a +X", list.empty() ? "ninguna" : std::to_string(list[0].orient));
    }

    // --- Torsion: sigue recta, del piso a la pared de la izquierda ---
    {
        Board b = board("start floor 0 0 0 1 0 0\nend back 3 0 0 3 0 0\nsize 4 1 1\nsource 0 0 0 right\nreceiver 3 0 0\n");
        place(b, "twist 1 0 0 right up");
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 3, "torsion: del piso a la pared, sin doblar", end(t));
        if (t.hops.size() == 3) expect(t.hops[2].dir == kPosX, "torsion: sale yendo para el mismo lado", Board::DirKey(t.hops[2].dir));
    }

    // --- Curva: del piso a la pared, doblando a la izquierda ---
    {
        Board b = board("start floor 0 0 0 1 0 0\nend right 1 0 1 1 0 2\nsize 2 1 3\nsource 0 0 0 right\nreceiver 1 0 2\n");
        place(b, "bend 1 0 0 right up");
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached && t.hops.size() == 3, "curva: del piso a la pared, a la izquierda", end(t));
        if (t.hops.size() == 3) expect(t.hops[2].dir == kPosZ, "curva: sale yendo a +Z (la izquierda de +X)", Board::DirKey(t.hops[2].dir));
    }

    // --- Agujero de gusano: pozos hundidos en los planos de portal ---
    {
        // La luz baja por la pared de salida, cruza al piso de portal y cae en
        // el pozo; sale por el pozo del TECHO de portal yendo hacia el mismo
        // lado (+X) y por la arista llega a la pared de llegada.
        const char* base = "start left\nend right\nsize 4 3 3\nsource 0 2 1 down\nreceiver 3 1 1\n"
                           "portal floor 0 0 0 1 0 2\nportal ceiling 2 2 0 3 2 2\n";
        Board e = board(base);
        const Trace none = TraceLight(e);
        expect(none.end == TraceEnd::Lost, "gusano: sin pozos, la luz se cae del piso de portal", end(none));
        Board b = board(base);
        expect(place(b, "wormhole 1 0 1 2 2 1"), "gusano: los dos pozos entran en sus planos de portal", "no entran");
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Reached, "gusano: cae por el pozo del piso y sale por el del techo", end(t));
        int out = -1;
        for (size_t k = 0; k < t.hops.size(); ++k)
            if (t.hops[k].outMouth) out = static_cast<int>(k);
        expect(out >= 0 && t.hops[static_cast<size_t>(out)].cell == Int3{2, 2, 1} && t.hops[static_cast<size_t>(out)].t == kPosX,
               "gusano: sale por el pozo gemelo yendo hacia el mismo lado", "otro");
        if (out >= 0 && out + 1 < static_cast<int>(t.hops.size()))
            expect(t.hops[static_cast<size_t>(out) + 1].kind == Hop::Kind::Plane && t.hops[static_cast<size_t>(out) + 1].plane == 3,
                   "gusano: despues del pozo sigue por el plano de portal de salida", "otro");

        // Entre planos perpendiculares la direccion se dobla como en la arista:
        // la que iba hacia la pared sube por ella.
        expect(CarryDir(kPosX, kNegY, kPosX) == kPosY && CarryDir(kNegX, kNegY, kPosX) == kNegY && CarryDir(kPosZ, kNegY, kPosX) == kPosZ &&
                   CarryDir(kPosX, kNegY, kPosY) == kPosX,
               "gusano: la direccion entre planos (paralelos igual, perpendiculares doblada)", "otra");

        // Fuera de un plano de portal no hay pozo, y una pieza de al lado no le
        // pasa la luz: el pozo es del plano.
        Board c = board((std::string(base) + "inventory wormhole 1 sphere 1\n").c_str());
        Figure m;
        m.shape = Shape::Wormhole;
        m.cell = {1, 1, 1};
        expect(!c.Place(m), "gusano: no se hunde en el aire (fuera de un plano de portal)", "entro");
        m.cell = {1, 0, 1};
        expect(c.Place(m), "gusano: se hunde en una casilla del plano de portal", "no entro");
        const Trace u = TraceLight(c);
        expect(u.end == TraceEnd::Lost, "gusano: un pozo solo no lleva a ningun lado", end(u));
        expect(c.PortAt({1, 0, 1}, kNegY) && !c.PortAt({1, 0, 1}, kPosX) && !c.PortAt({1, 0, 1}, kPosY),
               "gusano: el pozo solo toma la luz de su plano", "toca por otra cara");
        m.cell = {2, 2, 2};
        expect(c.Place(m), "gusano: el segundo pozo cierra el par", "no entro");
        expect(c.figures.size() == 2 && c.figures[0].pair == c.figures[1].pair && c.figures[0].pair >= 0 && c.figures[1].sink == kPosY,
               "gusano: el par se arma y el segundo pozo sabe su plano", "sin par");
        expect(c.Remove({2, 2, 2}) && c.figures.empty() && c.inventory[static_cast<int>(Shape::Wormhole)] == 2,
               "gusano: quitar un pozo devuelve los dos", std::to_string(c.figures.size()));
        // Con el clic: sobre el plano de portal hay postura, sobre el de llegada no.
        expect(!c.PlacementsFor(Shape::Wormhole, {1, 0, 2}, kPosY).empty() && c.PlacementsFor(Shape::Wormhole, {3, 1, 1}, kNegX).empty(),
               "gusano: el clic solo lo hunde en un plano de portal", "otro");
        std::string error;
        Board bad;
        expect(!Board::Parse("start left\nend right\nsize 4 3 3\nsource 0 2 1 down\nreceiver 3 1 1\nportal left 0 0 0 0 1 1\n", bad, error),
               "gusano: un plano de portal no pisa a otro plano de su cara", "se acepto");
    }

    // --- Agujero negro y agujero blanco (el puente del tutorial, con un obstaculo debajo) ---
    {
        const char* base = "start left\nend right\nsize 3 3 3\nsource 0 1 1 up\nreceiver 2 2 1\ninventory sphere 3\n";
        Board b = board((std::string(base) + "fixed blackhole 1 1 1\n").c_str());
        b.Place(Shape::Sphere, {0, 2, 1});
        b.Place(Shape::Sphere, {1, 2, 1});
        b.Place(Shape::Sphere, {2, 2, 1});
        const Trace t = TraceLight(b);
        expect(t.end == TraceEnd::Absorbed, "agujero negro: la esfera de al lado se la entrega y no sale", end(t));

        Board c = board((std::string(base) + "fixed whitehole 1 1 1\n").c_str());
        c.Place(Shape::Sphere, {0, 2, 1});
        c.Place(Shape::Sphere, {1, 2, 1});
        c.Place(Shape::Sphere, {2, 2, 1});
        const Trace u = TraceLight(c);
        bool bounced = false;
        for (const Hop& h : u.hops) bounced = bounced || h.shape == Shape::WhiteHole;
        expect(u.end == TraceEnd::Blocked && bounced, "agujero blanco: la devuelve por donde vino, hasta la fuente", end(u));

        Board bad;
        std::string error;
        expect(!Board::Parse(std::string(base) + "inventory blackhole 1\n", bad, error), "un agujero negro no se da en el inventario", "se acepto");
        expect(!Board::Parse(std::string(base) + "fixed sphere3 0 0 0\n", bad, error), "una esfera de 3 fija que no entra se rechaza", "se acepto");
    }
}

} // namespace gravitational
