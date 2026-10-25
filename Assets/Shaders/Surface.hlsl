// LOS PLANOS: la cuadricula, la luz que corre por ellos y las marcas de la
// fuente y del receptor. Todo es dibujo SOBRE la superficie: el plano 2D
// representa el espacio 3D, y lo que viaja por ese espacio viaja por el plano.
//
// UserParams[0] = (-, grosor de la linea de casilla en px (0 = el de siempre),
//                  1 = solo lineas, -) -- PuzzleDirector
// UserParams[1].rgb = tinte de la cuadricula (blanco = el de EL TRAZO); .w = origen de las casillas
// UserParams[2] = (progreso de la luz en metros, segundos desde resuelto o -1,
//                  tramos (0..2), 1 si la luz llego)  -- SurfaceLight
// UserParams[3] = (punto de la marca xyz, 1 fuente / 2 receptor / 0 nada)
// UserParams[4],[5] y [6],[7] = un tramo: (A xyz, s en A), (B xyz, s en B)
//   s en B NEGATIVO: el tramo entrega la luz a un agujero negro (ver GravFall)

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

// Los planos de portal: el cian de sus casillas y el casi blanco de su borde.
// Multiplicados por kMajor y por 0,55 quedan debajo del umbral del bloom, como
// toda la cuadricula: el portal se lee por el color, no por brillar.
static const float3 kPortalLine = float3(0.20f, 0.85f, 1.00f);
static const float3 kPortalEdge = float3(0.80f, 0.97f, 1.00f);

float Ring(float r, float radius, float hw, float px) { return saturate((hw + 0.5f * px - abs(r - radius)) / px); }

float4 PSMain(PSInput input) : SV_TARGET {
    if (EmissiveSelected.a > 0.5f) return float4(StraceSelectionTint(input), 1.0f);

    float3 P = input.worldPos;
    float px = GravPixel(P);
    float fade = GravDepthFade(P);
    float3 lineColor = kLine * (any(UserParams[1].rgb) ? UserParams[1].rgb : float3(1.0f, 1.0f, 1.0f));

    // La cuadricula, y el borde del plano un poco mas firme: es la silueta de
    // la superficie (la UV de la primitiva va de 0 a 1 de lado a lado).
    float thick = UserParams[0].y > 0.0f ? UserParams[0].y : 1.1f;
    float2 wg = WorldGrid(P, thick);
    float2 e = min(input.uv, 1.0f - input.uv) / max(fwidth(input.uv), 1e-6f);
    float edge = saturate(1.6f - min(e.x, e.y));
    // PLANO DE PORTAL (UserParams[0].z = 2): donde se hunden los gusanos. Se
    // distingue por el COLOR - casillas cian, borde casi blanco - y por no
    // tener subdivisiones: un pozo ocupa una casilla entera, y las lineas de
    // a 25 cm cruzarian su boca como una reja (el plano esta a ras de la
    // lamina del pozo). Las de casilla coinciden con el borde del pozo.
    bool portal = abs(UserParams[0].z - 2.0f) < 0.5f;
    if (portal) {
        wg.y = 0.0f;
        lineColor = lerp(kPortalLine, kPortalEdge, saturate(edge * 1.5f - wg.x * 0.5f));
    }
    float grid = max(max(wg.x * kMajor, wg.y * kMinor), edge * 0.55f) * fade;

    // --- La luz: hasta dos segmentos, revelados hasta el progreso ---
    float progress = UserParams[2].x;
    float core = 0.0f, halo = 0.0f;
    float3 beam = 0.0f, glow = 0.0f;
    [unroll] for (int i = 0; i < 2; ++i) {
        if ((float)i < UserParams[2].z) {
            float4 A = UserParams[4 + i * 2];
            float4 B = UserParams[5 + i * 2];
            // B.w NEGATIVO: este tramo entrega la luz a un agujero negro en B
            // (SurfaceLight). El largo es su valor absoluto.
            bool falls = B.w < 0.0f;
            float sB = abs(B.w);
            float3 ab = B.xyz - A.xyz;
            float t = saturate(dot(P - A.xyz, ab) / max(dot(ab, ab), 1e-6f));
            float along = lerp(A.w, sB, t);
            if (along <= progress) {
                float energy, thin;
                float3 hotC, haloC;
                GravFall(falls ? along - sB : -1000.0f, energy, hotC, haloC, thin);
                float d = length(P - (A.xyz + ab * t)) / thin;
                float2 b = BeamProfile(d, px);
                float flow = BeamFlow(along);
                // La punta: los ultimos 40 cm brillan mas, y un poco mas ancha.
                float tip = exp(-(progress - along) * 5.0f) * BeamProfile(d * 0.6f, px).x;
                core = max(core, b.x * flow * energy);
                halo = max(halo, b.y * energy);
                beam = max(beam, hotC * saturate(b.x * flow + tip * 0.8f) * energy);
                glow = max(glow, haloC * (b.y + tip * 0.3f) * energy);
            }
        }
    }

    // --- La marca: la fuente (un anillo con su punto) o el receptor (dos
    // anillos, que se llenan cuando la luz llega; mientras no llega, el de
    // afuera late despacio para que se encuentre) ---
    float mark = 0.0f, markHalo = 0.0f;
    float r = length(P - UserParams[3].xyz);
    float hw = max(0.018f, 0.8f * px);
    if (UserParams[3].w > 0.5f && UserParams[3].w < 1.5f) {
        mark = max(Ring(r, 0.22f, hw, px), saturate((0.075f + 0.5f * px - r) / px));
        markHalo = exp(-abs(r - 0.22f) / 0.05f) * 0.35f;
    }
    if (UserParams[3].w > 1.5f) {
        bool reached = UserParams[2].w > 0.5f;
        float beacon = reached ? 1.0f : 0.55f + 0.45f * sin(Time.x * 2.2f);
        mark = max(Ring(r, 0.30f, hw, px) * beacon, Ring(r, 0.17f, hw, px));
        if (reached) mark = max(mark, saturate((0.12f + 0.5f * px - r) / px));
        markHalo = exp(-abs(r - 0.30f) / 0.06f) * 0.3f * beacon;
    }

    float boost = SolvedBoost(UserParams[2].y);
    float3 light = (max(beam, kBeamHot * saturate(mark)) + glow + kBeamHalo * markHalo) * boost;

    // Modo "solo lineas" (los planos del juego): se descarta lo que no es
    // cuadricula, luz ni marca, y queda todo flotando en el vacio. Los dos
    // planos se miran entre si: opacos, el de adelante tapaba la luz del otro.
    if (UserParams[0].z > 0.5f) {
        clip(max(max(wg.x, wg.y * 0.6f), max(edge, max(core, max(mark, halo * 1.5f)))) - 0.08f);
        return float4(lineColor * grid + light, 1.0f);
    }

    // La superficie se ilumina; la cuadricula NO: es el dibujo de la geometria,
    // no un color de la superficie.
    float3 albedo = AlbedoColor.rgb * AlbedoTex.Sample(AlbedoSampler, input.uv).rgb;
    float3 lit = StraceLighting(albedo, input);
    lit = lerp(lit, lineColor, saturate(grid * 2.0f));
    return float4(lit + light, AlbedoColor.a);
}
