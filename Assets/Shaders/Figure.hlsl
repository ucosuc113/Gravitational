// LAS FIGURAS REDONDAS Y EL CUBO (esfera de 1, 3 y 5, cubo): superficie negra,
// lo que el jugador necesita leer de cada una, y la LUZ que la recorre -
// dibujada sobre la propia superficie (la superficie es el espacio por el que
// viaja; la forma de la figura es lo que representa la dimension de arriba).
// Las piezas nuevas (toroide, prisma, rampas, agujeros) van en Piece.hlsl.
//
//   cubo    su FRANJA: el cinturon alrededor de su eje, el unico por donde la
//           luz lo puede recorrer (docs/reglas.md). Rotar el cubo la mueve.
//   esfera  conduce por cualquier circulo maximo: no lleva marca propia.
//   todas   la cuadricula del MUNDO (WorldGrid), la misma que la de los planos,
//           y un contorno tenue (lo que da la forma sobre el vacio negro).
//   fija    un tinte rojo (el del logo): no se mueve ni se quita.
//
// La luz: por cada tramo, el arco de la geodesica desde la cara de entrada.
// Para cada pixel se mide a que distancia esta del plano de esa vuelta y
// cuanto recorrio la luz hasta ahi, con las MISMAS formulas que LightTrace.h
// (si discreparan, la punta saltaria al cambiar de superficie).
//
// UserParams[0] = (progreso en metros, segundos desde resuelto o -1, tramos, LADO de la esfera)
// UserParams[1].rgb = color de las lineas; .w = origen de las casillas (ver WorldGrid)
// UserParams[2] = (eje del cubo xyz, 1 si es cubo)           -- PuzzleDirector
// UserParams[3] = (centro de la figura xyz, 1 si es fija)    -- idem
// UserParams[4],[5] y [6],[7] = un tramo: (cara de entrada xyz, cuartos), (direccion xyz, s al entrar)
//   s al entrar NEGATIVO: al salir, la luz cae en un agujero negro (ver GravFall)
//   cuartos en un cubo: donde TERMINA el arco (x,5 = se va por una costura) y
//   +8 si empezo en una (ver "Cubos pegados" en LightTrace.h)

// ===== EL TRAZO (IDENTICO en Surface.hlsl, Figure.hlsl y Piece.hlsl) =====
//
// La imagen del motor es de 8 bits y el bloom del SubViewport empieza en un
// UMBRAL (0,6): no hay "mas blanco que el blanco", asi que la jerarquia de la
// imagen es de VALORES. Todo lo que es dibujo de la geometria (cuadricula,
// bordes, contornos) queda por debajo del umbral y no sangra; solo la luz, las
// marcas y el destello de resuelto lo pasan. Antes la casilla iba a 0,85 de
// blanco y el rayo a 1: con bloom, las dos brillaban igual y el rayo dejaba de
// ser lo que se mira.
//
// Las lineas se miden en PIXELES (fwidth): una linea de casilla es 1,1 px a
// cualquier distancia. En metros, de lejos se volvian hilos que titilan y de
// cerca, bandas.
static const float3 kLine = float3(0.60, 0.67, 0.78);      // el blanco frio de la cuadricula
static const float kMajor = 0.40;                         // la casilla del tablero
static const float kMinor = 0.12;                         // su subdivision
static const float3 kBeamHot = float3(0.88, 0.97, 1.00);  // el centro de la luz: pasa el umbral
static const float3 kBeamHalo = float3(0.16, 0.55, 1.00); // su halo cercano: lo abre el bloom

// Lo lejano se apaga hacia el vacio: sin esto todas las lineas pesan igual y
// la caja no tiene fondo.
float GravDepthFade(float3 P) {
    float d = distance(P, CameraWorldPosPad.xyz);
    return lerp(1.0f, 0.5f, saturate((d - 7.0f) / 20.0f));
}

// Una linea de la cuadricula: g es la coordenada en celdas, la linea es donde
// cae entera. Cobertura de una linea de `widthPx` pixeles con un pixel de
// rampa, y desvanecida cuando las lineas se juntan (el moire).
float GridLine(float g, float widthPx) {
    float w = fwidth(g) + 1e-6f;
    float d = abs(g - round(g)) / w;
    return saturate(widthPx * 0.5f + 0.5f - d) * (1.0f - saturate((w - 0.16f) * 5.0f));
}

// La cuadricula del MUNDO: una linea es donde x, y o z caen en un multiplo de
// la celda, asi dos superficies que se tocan comparten exactamente las mismas
// lineas. x: la casilla, y: la subdivision (0,25 m). El origen depende de la
// paridad del tablero (UserParams[1].w, bit 0 x, 1 y, 2 z: medio metro
// corrido). Un eje perpendicular a la superficie no dibuja: se apaga con la
// normal GEOMETRICA (de las derivadas).
float2 WorldGrid(float3 P, float majorPx) {
    float3 N = normalize(cross(ddx(P), ddy(P)));
    uint mask = (uint)(UserParams[1].w + 0.5f);
    float3 origin = float3((mask & 1u) ? 0.5f : 0.0f, (mask & 2u) ? 0.5f : 0.0f, (mask & 4u) ? 0.5f : 0.0f);
    float3 g = P - origin;
    float major = 0.0f, minor = 0.0f;
    [unroll] for (int a = 0; a < 3; ++a) {
        float vis = saturate((1.0f - abs(N[a]) - 0.03f) * 12.0f);
        major = max(major, GridLine(g[a], majorPx) * vis);
        minor = max(minor, GridLine(g[a] * 4.0f, 0.8f) * vis);
    }
    return float2(major, minor);
}

// El tamano de un pixel en metros, sobre esta superficie.
float GravPixel(float3 P) { return length(fwidth(P)) * 0.7071f + 1e-5f; }

// La luz a `d` metros del trazo. x: el nucleo (nunca mas fino que ~2 px, para
// que de lejos no titile), y: el halo cercano (el lejano es del bloom).
float2 BeamProfile(float d, float px) {
    float hw = max(0.022f, 0.9f * px);
    float core = saturate((hw + 0.5f * px - d) / px);
    float halo = exp(-d / max(0.045f, 2.5f * px)) * 0.5f + exp(-d / 0.2f) * 0.1f;
    return float2(core, halo);
}

// La corriente: un latido suave que avanza con la luz, para que se lea hacia
// donde va sin flechas. `along` en metros recorridos.
float BeamFlow(float along) { return 0.86f + 0.14f * sin((along - Time.x * 2.4f) * 7.0f); }

// Resuelto: destella y despues late, mas tenue. La animacion de fin.
float SolvedBoost(float solved) {
    return solved >= 0.0f ? 1.0f + 1.4f * exp(-solved * 1.4f) + 0.25f * (0.5f + 0.5f * sin(solved * 4.5f)) : 1.0f;
}

// LA CAIDA: la luz que va a un agujero negro no se corta, se apaga. `x` son
// los metros que le faltan (negativo) o que lleva (positivo) desde el
// contacto con el agujero; sin agujero en el camino, x = -1000 y todo queda
// como siempre. El corrimiento empieza kFallLead metros ANTES del contacto, en
// la superficie que la entrega (plano, esfera, cubo, pieza), y termina
// kFallDepth metros despues, en el pozo (el kHoleLen de LightTrace.h): por eso
// esta aqui y no en Piece.hlsl, y es la misma cuenta en las tres superficies -
// si cada una tuviera la suya, el color saltaria en el borde del pozo.
//
// Pierde ENERGIA (por debajo del umbral del bloom deja de sangrar: se lee como
// luz que se va, no como una linea que cambia de color), se corre al ROJO
// (blanco frio -> calido -> naranja -> el rojo del horizonte) y se AFINA.
static const float kFallLead = 0.5f;
static const float kFallDepth = 0.5f;
void GravFall(float x, out float energy, out float3 hot, out float3 halo, out float thin) {
    float k = saturate((x + kFallLead) / (kFallLead + kFallDepth));
    energy = 1.0f - smoothstep(0.45f, 0.97f, k);
    hot = lerp(kBeamHot, float3(1.00f, 0.82f, 0.64f), saturate(k / 0.5f));
    hot = lerp(hot, float3(1.00f, 0.34f, 0.13f), saturate((k - 0.5f) / 0.25f));
    hot = lerp(hot, float3(0.55f, 0.03f, 0.03f), saturate((k - 0.75f) / 0.25f));
    halo = lerp(kBeamHalo, float3(0.85f, 0.10f, 0.05f), smoothstep(0.3f, 0.8f, k));
    thin = lerp(1.0f, 0.45f, smoothstep(0.4f, 1.0f, k));
}
// ===== fin de EL TRAZO =====

// El contorno: lo que mira de canto a la camara. Sobre negro total es lo que
// hace que una esfera se lea como una esfera y no como un disco de lineas.
float Rim(PSInput input) {
    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    float3 N = normalize(input.worldNormal);
    return pow(1.0f - saturate(abs(dot(N, V))), 3.0f);
}

float Wrap(float v, float period) { return v - floor(v / period) * period; }

// El largo del cinturon del cubo hasta un angulo (ver CubeArc en LightTrace.h).
// Con angulos negativos tambien: -45 grados (la arista de antes) da -0,5.
float FigCubeArc(float angle) {
    const float quarter = 1.5707963f;
    float k = floor(angle / quarter);
    float phi = angle - k * quarter;
    float f = phi <= 0.7853982f ? 0.5f * tan(phi) : 1.0f - 0.5f * tan(quarter - phi);
    return k + f;
}

float4 PSMain(PSInput input) : SV_TARGET {
    // Sin return temprano para la seleccion, y UNA sola salida: en GLES el
    // traductor convierte cada return temprano en un do{...}while(false) que
    // envuelve el cuerpo, y con un cuerpo grande el compilador de ANGLE/D3D11
    // de Chrome se cae (0xC0000005, se lleva el proceso de GPU entero). El
    // tinte va al final (ver Piece.hlsl para la nota completa).

    // La superficie es NEGRO TOTAL, sin luz: como los planos en modo "solo
    // lineas", donde lo que no es cuadricula es el vacio. La forma la dan las
    // lineas, el contorno y la luz, que se suman encima.
    float3 P = input.worldPos;
    float px = GravPixel(P);
    float fade = GravDepthFade(P);
    float3 lineColor = kLine * UserParams[1].rgb;
    // LA CUADRICULA DE LA FIGURA. El cubo usa la del mundo (sus caras son
    // planos de la caja: los bordes caen en las lineas de casilla). La esfera
    // NO: la cuadricula del mundo son planos x, y, z enteros, y una esfera de
    // una celda solo toca los de casilla en seis puntos, asi que se veia negra
    // con hilos al 12%. En la esfera la cuadricula es la de SU superficie:
    // meridianos y paralelos cada 22,5 grados, y mas marcados los tres
    // circulos maximos de los ejes (el ecuador y los dos meridianos de 0 y 90)
    // - las geodesicas por donde la luz la recorre.
    float3 pc = P - UserParams[3].xyz;
    float3 lineAmount;
    if (UserParams[2].w > 0.5f) {
        float2 wg = WorldGrid(P, 1.1f);
        lineAmount = float3(wg.x * kMajor, wg.y * 0.22f, 0.0f);
    } else {
        float3 d = normalize(pc);
        float lon = atan2(d.z, d.x) / 6.2831853f * 16.0f; // 16 meridianos
        float lat = asin(clamp(d.y, -1.0f, 1.0f)) / 6.2831853f * 16.0f;
        float meridian = GridLine(lon, 1.0f) * saturate((1.0f - abs(d.y)) * 6.0f); // se apagan en los polos
        float parallel = GridLine(lat, 1.0f);
        float axes = max(max(GridLine(lon / 4.0f, 1.3f) * saturate((1.0f - abs(d.y)) * 6.0f), GridLine(lat / 4.0f, 1.3f)), 0.0f);
        lineAmount = float3(axes * kMajor, max(meridian, parallel) * 0.22f, 0.0f);
    }
    float3 lit = lineColor * max(lineAmount.x, lineAmount.y) * fade;
    lit += lineColor * Rim(input) * 0.32f * fade;

    float3 p = P - UserParams[3].xyz;
    bool isCube = UserParams[2].w > 0.5f;
    float size = max(1.0f, UserParams[0].w);

    if (isCube) {
        float d = abs(dot(p, UserParams[2].xyz));
        float bpx = fwidth(d) + 1e-5f;
        float band = saturate((0.09f - d) / bpx + 0.5f);
        float edge = saturate((0.6f * bpx + 0.5f * bpx - abs(d - 0.09f)) / bpx);
        lit = lerp(lit, lineColor * 0.16f, band * 0.7f);
        lit = max(lit, lineColor * 0.5f * edge * fade);
        // HACIA DONDE GIRA: la franja tiene sentido (UserParams[2].xyz es el
        // eje con signo) y dobla la luz que llega a lo largo del eje hacia
        // cross(eje, entrada). Sin dibujarlo, el sentido seria invisible y
        // rotar el cubo, adivinar. Cheurones que avanzan en ese sentido.
        float3 A = UserParams[2].xyz;
        float3 B1 = normalize(cross(A, abs(A.y) > 0.5f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f)));
        float3 B2 = cross(A, B1);
        float theta = atan2(dot(p, B2), dot(p, B1)) / 6.2831853f;
        float v = dot(p, A) / 0.09f;
        float chev = frac(theta * 12.0f + abs(v) * 0.35f - Time.x * 0.6f); // la punta hacia +theta
        float cpx = fwidth(theta * 12.0f) + 1e-5f;
        float arrow = saturate((0.12f - abs(chev - 0.5f)) / cpx) * band;
        lit = max(lit, lineColor * 0.42f * arrow * fade);
    }
    if (UserParams[3].w > 0.5f) lit = lerp(lit, float3(0.62f, 0.07f, 0.07f), 0.30f);

    // --- La luz ---
    float progress = UserParams[0].x;
    float3 beam = 0.0f, glow = 0.0f;
    [unroll] for (int i = 0; i < 2; ++i) {
        if ((float)i < UserParams[0].z) {
            float3 nIn = UserParams[4 + i * 2].xyz;
            float quarters = UserParams[4 + i * 2].w;
            float3 t = UserParams[5 + i * 2].xyz;
            bool falls = UserParams[5 + i * 2].w < 0.0f;
            float s0 = abs(UserParams[5 + i * 2].w);
            // CUBOS PEGADOS (ver TransferOf en LightTrace.h): +8 = la luz entro
            // por una costura, en la arista de la cara de entrada (medio cuarto
            // ANTES de su centro, -45 grados); y un final en x,5 = se fue por
            // otra, en la arista y no en el centro de la cara siguiente. Todo
            // se mide desde donde empezo: a0.
            bool seamIn = quarters > 7.5f;
            if (seamIn) quarters -= 8.0f;
            float a0 = seamIn ? -0.7853982f : 0.0f;
            float arc0 = isCube ? FigCubeArc(a0) : 0.0f;
            // Donde la entrega al agujero: al final del arco de este tramo.
            float sEnd = s0 + (isCube ? FigCubeArc(quarters * 1.5707963f) - arc0 : quarters * 1.5707963f * 0.5f * size);
            float dist = 1000.0f, along = 0.0f;
            if (quarters < 0.5f && !seamIn) {
                // Se freno al entrar (un cubo con el eje equivocado): un punto
                // en el contacto, para que se vea DONDE.
                dist = max(0.0f, length(p - nIn * 0.5f * size) - 0.05f);
                along = s0;
            } else {
                float angle = atan2(dot(p, t), dot(p, nIn));
                angle = a0 + Wrap(angle - a0, 6.2831853f);
                if (angle <= quarters * 1.5707963f + 1e-3f) {
                    dist = abs(dot(p, cross(nIn, t)));
                    along = s0 + (isCube ? FigCubeArc(angle) - arc0 : angle * 0.5f * size);
                }
            }
            if (along <= progress && dist < 999.0f) {
                float energy, thin;
                float3 hotC, haloC;
                GravFall(falls ? along - sEnd : -1000.0f, energy, hotC, haloC, thin);
                dist /= thin;
                float2 b = BeamProfile(dist, px);
                float tip = exp(-(progress - along) * 5.0f) * BeamProfile(dist * 0.6f, px).x;
                beam = max(beam, hotC * saturate(b.x * BeamFlow(along) + tip * 0.8f) * energy);
                glow = max(glow, haloC * (b.y + tip * 0.3f) * energy);
            }
        }
    }
    float boost = SolvedBoost(UserParams[0].y);
    lit += (beam + glow) * boost;

    float4 result = float4(lit, AlbedoColor.a);
    if (EmissiveSelected.a > 0.5f) result = float4(StraceSelectionTint(input), 1.0f);
    return result;
}
