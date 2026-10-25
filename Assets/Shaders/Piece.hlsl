// LAS PIEZAS NUEVAS: toroide, prisma, piramide, las tres de recorrido (rampa,
// torsion, curva), la boca de gusano y los dos agujeros. La esfera y el cubo
// siguen en Figure.hlsl. Lo mismo que alli: superficie negra, cuadricula del
// mundo, contorno, y la LUZ dibujada sobre la propia superficie, medida con las
// mismas cuentas que LightTrace.h.
//
// Cada pieza trae su marco (Board.h: X adelante, Y arriba, Z = cross(X, Y)), y
// el shader mide en ese marco: asi una rampa girada no necesita otra formula.
//
// UserParams[0] = (progreso en metros, segundos desde resuelto o -1, tramos, tono del par de gusano)  -- SurfaceLight / PuzzleDirector
// UserParams[1] = (U: el Y local en el mundo, origen de las casillas)  -- PuzzleDirector
// UserParams[2] = (F: el X local en el mundo, forma)                    -- idem (forma = Shape de Board.h)
// UserParams[3] = (centro de la celda ancla xyz, 1 si es fija)           -- idem
// UserParams[4],[5] y [6],[7] = un tramo: (cara de entrada xyz, largo), (direccion xyz, s al entrar)
//   largo 0 = se freno al entrar; NEGATIVO = la boca de salida de un gusano, o
//   la vuelta al tubo de un toroide (su valor absoluto es el largo).
//   s al entrar NEGATIVO: al salir, la luz cae en un agujero negro (ver GravFall).

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

float Rim(PSInput input) {
    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    float3 N = normalize(input.worldNormal);
    return pow(1.0f - saturate(abs(dot(N, V))), 3.0f);
}

static const float kTau = 6.2831853f;
float Wrap(float v, float period) { return v - floor(v / period) * period; }

// Las formas (el orden de Shape en Board.h).
static const int kTorus = 4, kPrism = 5, kPyramid = 6, kRamp = 7, kTwist = 8, kBend = 9, kMouth = 10, kBlack = 11, kWhite = 12;

// Las constantes de LightTrace.h.
static const float kTorusMajor = 0.35f, kTorusMinor = 0.15f;
static const float kPyramidHalf = 1.6180340f; // 0,5 + raiz(1,25)
static const float kSlant = 1.1180340f;       // raiz(1,25): el lado inclinado de la piramide
static const float kPrismLoop = 3.4142136f;   // 2 + raiz(2)

// La caida en el agujero negro: el radio donde la luz se termina (el fondo del
// pozo; la malla se cierra en un punto y el 1/r de gen_pieces.py diverge ahi) y
// cuanto gira la espiral: kFallTwist * (rho0 / rho - 1) radianes, poco en el
// borde y cada vez mas cerca del centro, como una orbita que se cierra.
static const float kFallEnd = 0.03f;
static const float kFallTwist = 0.7f;

// La espiral del pozo del gusano: cuantas vueltas da la luz del borde del
// embudo a la garganta. Tres cuartos: se lee que gira sin volverse un ovillo.
static const float kMouthTurns = 0.75f;

// El perimetro del triangulo de la cuna, desde el centro del cateto -X yendo
// hacia la esquina recta (ver Transfer del prisma en LightTrace.h).
float PrismSigma(float2 q) {
    float dx = abs(q.x + 0.5f), dy = abs(q.y + 0.5f), dh = abs(q.x + q.y) * 0.7071068f;
    if (dx <= dy && dx <= dh) return q.y <= 0.0f ? -q.y : kPrismLoop - q.y;
    if (dy <= dh) return 1.0f + q.x;
    return 1.5f + (0.5f - q.x) * 1.4142136f;
}

float4 PSMain(PSInput input) : SV_TARGET {
    if (EmissiveSelected.a > 0.5f) return float4(StraceSelectionTint(input), 1.0f);

    int shape = (int)(UserParams[2].w + 0.5f);
    float3 U = UserParams[1].xyz, F = UserParams[2].xyz, S = cross(F, U);
    float3 P = input.worldPos;
    float3 p = P - UserParams[3].xyz;
    float3 lp = float3(dot(p, F), dot(p, U), dot(p, S));
    float px = GravPixel(P);
    float fade = GravDepthFade(P);
    bool fixedPiece = UserParams[3].w > 0.5f;
    bool hole = shape == kBlack || shape == kWhite || shape == kMouth;

    // La cuadricula de la pieza: las de caras planas (prisma, piramide) usan la
    // del mundo, que les cae en los bordes; las curvas, la de SU superficie
    // (ver Figure.hlsl: la del mundo casi no las cruza y se veian negras). El
    // toroide por su UV de primitiva (vueltas grande y chica), las cintas por la
    // suya (a lo largo y a lo ancho).
    float2 wg = WorldGrid(P, 1.1f);
    float2 own = float2(wg.x, wg.y);
    if (shape == kTorus) {
        own = float2(max(GridLine(input.uv.x * 4.0f, 1.2f), GridLine(input.uv.y * 2.0f, 1.2f)),
                     max(GridLine(input.uv.x * 20.0f, 1.0f), GridLine(input.uv.y * 8.0f, 1.0f)));
    } else if (shape == kRamp || shape == kTwist || shape == kBend) {
        own = float2(0.0f, max(GridLine(input.uv.x * 8.0f, 1.0f), GridLine(input.uv.y * 4.0f, 1.0f)));
    }
    float3 lit = kLine * max(own.x * kMajor, own.y * 0.22f) * fade;
    lit += kLine * Rim(input) * (hole ? 0.12f : 0.32f) * fade;

    bool ribbon = shape == kRamp || shape == kTwist || shape == kBend;
    float width = shape == kRamp ? 0.9f : 0.7f;
    if (ribbon) {
        // El borde de la cinta y su linea de centro (por donde va la luz):
        // es la que dice "esto es un camino" antes de que pase nada.
        float across = (input.uv.y - 0.5f) * width;
        float apx = fwidth(across) + 1e-5f;
        float border = saturate((0.6f * apx - (width * 0.5f - abs(across))) / apx + 0.5f);
        float center = saturate((0.5f * apx - abs(across)) / apx + 0.5f) * (0.5f + 0.5f * step(0.5f, frac(input.uv.x * 9.0f)));
        lit = max(lit, kLine * (border * 0.5f + center * 0.18f) * fade);
    }

    // Los pozos: el dibujo de la gravedad sobre una lamina, en SOLO LINEAS como
    // los planos (opacos se leian como bandejas negras que tapaban la
    // cuadricula de atras). Anillos de igual radio, que al bajar el embudo se
    // ven juntarse, radios que caen hacia el centro, y el horizonte del agujero
    // negro en el rojo del logo.
    float rho = length(lp.xz);
    float holeCover = 0.0f;
    if (hole) {
        float rings = GridLine(rho * 10.0f, 1.0f) * saturate(rho * 5.0f);
        float spokes = GridLine(atan2(lp.z, lp.x) / kTau * 12.0f, 1.0f) * saturate((rho - 0.05f) * 8.0f) * saturate((0.5f - rho) * 6.0f);
        float edge = 0.0f;
        {
            float2 e = (0.5f - abs(lp.xz)) / max(fwidth(lp.xz), 1e-5f);
            edge = saturate(1.6f - min(e.x, e.y));
        }
        lit = kLine * max(max(rings * 0.30f, spokes * 0.22f), max(edge * 0.5f, max(wg.x * kMajor, wg.y * kMinor))) * fade;
        holeCover = max(max(rings, spokes), max(edge, max(wg.x, wg.y * 0.6f)));
        if (shape == kBlack) {
            float horizon = saturate((0.8f * px - abs(rho - 0.11f)) / px + 0.5f);
            lit = max(lit, float3(0.80f, 0.10f, 0.10f) * horizon * 0.65f);
            holeCover = max(holeCover, horizon);
        }
        if (shape == kMouth) {
            // El tono del par: dos bocas del mismo color son la misma garganta.
            float hue = UserParams[0].w;
            float3 tint = hue < 0.5f ? float3(0.35f, 0.75f, 1.0f) : hue < 1.5f ? float3(0.70f, 0.45f, 1.0f) : float3(0.30f, 1.0f, 0.75f);
            float throat = saturate((0.9f * px - abs(rho - 0.105f)) / px + 0.5f);
            lit = max(lit, tint * 0.5f * throat);
            holeCover = max(holeCover, throat);
        }
    }

    if (fixedPiece && !hole) lit = lerp(lit, float3(0.62f, 0.07f, 0.07f), 0.30f);

    // --- La luz ---
    float progress = UserParams[0].x;
    float core = 0.0f, halo = 0.0f, swirl = 0.0f, arrived = 0.0f;
    float3 beam = 0.0f, glow = 0.0f;
    [unroll] for (int i = 0; i < 2; ++i) {
        if ((float)i >= UserParams[0].z) continue;
        float3 nIn = UserParams[4 + i * 2].xyz;
        float L = UserParams[4 + i * 2].w;
        float3 t = UserParams[5 + i * 2].xyz;
        bool falls = UserParams[5 + i * 2].w < 0.0f;
        float s0 = abs(UserParams[5 + i * 2].w);
        if (progress < s0) continue;
        float dist = 1000.0f, along = 0.0f;
        bool special = L < 0.0f;
        float len = abs(L);
        // Metros desde el contacto con el agujero negro (ver GravFall): al
        // final de este tramo si lo entrega, o dentro del pozo.
        float fallX = -1000.0f;

        if (len < 1e-4f && !hole) {
            // Se freno al entrar: un punto en el contacto.
            float3 contact = nIn * 0.5f;
            dist = max(0.0f, length(p - contact) - 0.05f);
            along = s0;
        } else if (shape == kTorus) {
            float3 A = U;
            if (!special) {
                // El ecuador de afuera, alrededor del eje.
                float h = dot(p, A);
                float radial = length(p - A * h);
                if (radial > kTorusMajor - 0.02f) {
                    float angle = Wrap(atan2(dot(p, t), dot(p, nIn)), kTau);
                    if (angle * 0.5f <= len + 1e-3f) {
                        dist = abs(h);
                        along = s0 + angle * 0.5f;
                    }
                }
            } else {
                // La vuelta al tubo, en el plano que contiene al eje y al contacto.
                float3 q = p - nIn * kTorusMajor;
                if (length(q) < kTorusMinor + 0.08f) {
                    dist = abs(dot(q, normalize(cross(A, nIn))));
                    float beta = Wrap(atan2(dot(q, t), dot(q, nIn)), kTau);
                    along = s0 + beta * kTorusMinor;
                }
            }
        } else if (shape == kPrism) {
            float2 ln = float2(dot(nIn, F), dot(nIn, U));
            float2 lt = float2(dot(t, F), dot(t, U));
            float sigma0 = ln.x < -0.5f ? 0.0f : 1.0f;
            float sgn = ln.x < -0.5f ? (lt.y < 0.0f ? 1.0f : -1.0f) : (lt.x > 0.0f ? 1.0f : -1.0f);
            if (special) {
                // SUBIO DESDE EL PLANO en el que se apoya (ClimbsOnto en
                // LightTrace.h): nIn es la cara apoyada, y la luz arranca en la
                // esquina de esa cara por la que llego - la de la pendiente
                // (sigma 1,5 o 1,5 + raiz 2) o la recta (0,5) - y da la vuelta
                // por arriba, hacia el otro lado.
                if (ln.y < -0.5f) {
                    sigma0 = lt.x < 0.0f ? 1.5f : 0.5f;
                    sgn = lt.x < 0.0f ? 1.0f : -1.0f;
                } else {
                    sigma0 = lt.y < 0.0f ? kPrismLoop - 0.5f : 0.5f;
                    sgn = lt.y < 0.0f ? -1.0f : 1.0f;
                }
            }
            float a = Wrap(sgn * (PrismSigma(lp.xy) - sigma0), kPrismLoop);
            if (a <= len + 1e-3f) {
                dist = abs(lp.z);
                along = s0 + a;
            }
        } else if (shape == kPyramid) {
            // EN LA PUNTA DOBLA (LightTrace.h): llega `t` = via, la direccion
            // con que salio de la punta, y la de entrada se rearma como
            // cross(via, entrada). Desde la base: base -> cara de `tIn` ->
            // punta (y si nada toca, baja por la cara de `via`). Desde la
            // punta: baja por la cara opuesta a `via` y cruza la base al centro.
            bool fromApex = dot(nIn, U) > 0.5f;
            float3 tIn = cross(t, nIn);
            float h = lp.y;
            bool onBase = h < -0.49f;
            if (special) {
                // SUBIO DESDE EL PLANO en el que apoya la base (ClimbsOnto en
                // LightTrace.h): arranca en la arista de enfrente, sube por la
                // cara de `-tIn` hasta la punta y, si sigue, baja por la de
                // `via`. Nunca toca la base.
                if (!onBase) {
                    float s1 = (h + 0.5f) * kSlant;
                    if (dot(p, -tIn) >= -0.02f && s1 <= len + 1e-3f) {
                        dist = abs(dot(p, normalize(cross(U, tIn))));
                        along = s0 + s1;
                    }
                    float s2 = kSlant + (0.5f - h) * kSlant;
                    float d2 = abs(dot(p, tIn));
                    if (dot(p, t) >= -0.02f && s2 <= len + 1e-3f && d2 < dist) {
                        dist = d2;
                        along = s0 + s2;
                    }
                }
            }
            float3 d1 = fromApex ? -t : tIn;
            float a1 = special ? -1.0f : dot(p, d1);
            if (a1 >= -0.02f) {
                float s1 = fromApex ? (onBase ? kSlant + (0.5f - a1) : (0.5f - h) * kSlant) : (onBase ? a1 : 0.5f + (h + 0.5f) * kSlant);
                if (s1 <= len + 1e-3f) {
                    dist = abs(dot(p, normalize(cross(U, d1))));
                    along = s0 + s1;
                }
            }
            // D3 (LightTrace.h): apoyada de PUNTA en un plano y sin nada sobre
            // la base, la luz cruza la base hasta la arista de `via` y baja por
            // esa cara hasta la punta, donde vuelve al plano. Largo: 2 x kPyramidHalf.
            if (fromApex && len > kPyramidHalf + 1e-3f) {
                float a2 = dot(p, t);
                float s2 = kPyramidHalf + (onBase ? a2 : 0.5f + (h + 0.5f) * kSlant);
                float d2 = abs(dot(p, tIn));
                if (a2 >= -0.02f && s2 <= len + 1e-3f && d2 < dist) {
                    dist = d2;
                    along = s0 + s2;
                }
            }
            if (!special && !fromApex && len > kPyramidHalf + 1e-3f) {
                float a2 = dot(p, t);
                float s2 = kPyramidHalf + (onBase ? kSlant + (0.5f - a2) : (0.5f - h) * kSlant);
                float d2 = abs(dot(p, tIn));
                if (a2 >= -0.02f && s2 <= len + 1e-3f && d2 < dist) {
                    dist = d2;
                    along = s0 + s2;
                }
            }
        } else if (ribbon) {
            // La UV va por largo de arco (Tools/Meshes/gen_pieces.py): la
            // entrada es la cara de abajo de la celda ancla (-U).
            bool forward = dot(nIn, -U) > 0.5f;
            float a = (forward ? input.uv.x : 1.0f - input.uv.x) * len;
            dist = abs(input.uv.y - 0.5f) * width;
            along = s0 + a;
        } else if (shape == kBlack) {
            // EL AGUJERO NEGRO: la luz CAE. Hasta el 22/10/2026 era un
            // remolino de brazos que giraban sobre todo el pozo, y el rayo que
            // llegaba se cortaba en seco en el borde: no se leia que la luz
            // entrara a ningun lado. Ahora es el mismo trazo, que sigue por la
            // lamina desde donde toco hasta el centro, en una espiral que se
            // cierra (la luz que trae velocidad de costado orbita al caer), y
            // se apaga con GravFall: pierde energia, se corre al rojo y se
            // afina, y del horizonte para adentro ya no queda nada.
            //
            // De donde cae: por un costado, el borde de la lamina EN ese
            // contacto (la lamina va a media altura de la celda: el punto
            // medio de ese lado, a 0,5 del centro, es el centro de la cara).
            // Por abajo toca la punta del embudo, que ya es el horizonte: no
            // hay espiral, solo lo que queda en el fondo. Por arriba no hay
            // nada sobre el contacto: entra desde atras de por donde venia,
            // mas cerca del centro.
            float2 eIn = float2(dot(nIn, F), dot(nIn, S));
            float2 tIn = float2(dot(t, F), dot(t, S));
            bool side = dot(eIn, eIn) > 0.25f;
            bool below = !side && dot(nIn, U) < 0.0f;
            float2 e = side ? normalize(eIn) : -normalize(tIn + float2(1e-4f, 0.0f));
            float rho0 = side ? 0.5f : below ? 0.06f : 0.3f;
            // Gira hacia donde la llevaba la direccion tangente; si llegaba sin
            // velocidad de costado (de frente, o vertical), para un lado fijo.
            float spin = dot(tIn, float2(-e.y, e.x)) < -0.3f ? -1.0f : 1.0f;
            float rhoC = max(rho, kFallEnd);
            float pitch = kFallTwist * rho0 / rhoC; // la vuelta por metro de caida, en el radio
            float pathAng = atan2(e.y, e.x) + spin * kFallTwist * (rho0 / rhoC - 1.0f);
            float dAng = Wrap(atan2(lp.z, lp.x) - pathAng + kTau * 0.5f, kTau) - kTau * 0.5f;
            if (rho <= rho0 + 0.01f) {
                float u = saturate((rho0 - rho) / (rho0 - kFallEnd));
                // A traves de la espiral (la cuenta en angulo ensancharia la
                // linea donde gira mas).
                dist = rho * abs(dAng) / sqrt(1.0f + pitch * pitch);
                // La punta ACELERA: recorre el pozo con el cuadrado del
                // progreso, despacio en el borde y de golpe al final. El
                // color, en cambio, va con el radio: el horizonte es siempre
                // el mismo rojo.
                along = s0 + len * sqrt(u);
                fallX = len * u;
            }
            arrived = max(arrived, saturate((progress - s0 - len * 0.85f) / (len * 0.15f)));
        } else if (shape == kMouth) {
            // EL POZO DEL GUSANO: la luz BAJA por el embudo en espiral, del
            // borde por donde llego (a 0,5 del centro, del lado de donde venia:
            // -t) hasta la garganta (radio 0,1); en el pozo de salida
            // (special) hace lo mismo al reves, de la garganta al borde por
            // donde se va (+t). Hasta el 23/10/2026 era un remolino sobre todo
            // el pozo y el tramo del plano llegaba hasta el CENTRO de la
            // casilla: un rayo flotando sobre el embudo que se cortaba en el
            // medio. Ahora el plano la deja en el borde (BeamPath.h) y el pozo
            // sigue el mismo trazo.
            //
            // El angulo crece lineal con lo que baja (kMouthTurns vueltas del
            // borde a la garganta); el giro va siempre para el mismo lado en
            // las dos bocas, asi la de salida se lee como la misma espiral
            // desenrollandose.
            float2 tIn = float2(dot(t, F), dot(t, S));
            float2 e = (special ? 1.0f : -1.0f) * normalize(tIn + float2(1e-4f, 0.0f));
            const float rim = 0.5f, throatR = 0.1f;
            float k = kMouthTurns * kTau / (rim - throatR); // radianes por metro de radio
            if (rho <= rim + 0.01f && rho >= throatR - 0.01f) {
                float u = saturate((rim - rho) / (rim - throatR)); // 0 en el borde, 1 en la garganta
                float pathAng = atan2(e.y, e.x) + k * (rim - rho);
                float dAng = Wrap(atan2(lp.z, lp.x) - pathAng + kTau * 0.5f, kTau) - kTau * 0.5f;
                dist = rho * abs(dAng) / sqrt(1.0f + (rho * k) * (rho * k));
                along = s0 + len * (special ? 1.0f - u : u);
            }
        } else if (hole) {
            // Agujero blanco: la luz no es un trazo sino un remolino
            // que entra al pozo (o sale de el), y crece con el tiempo desde
            // que llego.
            float since = progress - s0;
            float outward = shape == kWhite ? 1.0f : -1.0f;
            float ang = atan2(lp.z, lp.x) / kTau;
            float spiral = frac(rho * 5.0f + ang * 2.0f - outward * Time.x * 0.9f);
            float arms = saturate(1.0f - abs(spiral - 0.5f) * 6.0f);
            float grow = saturate(since / 0.5f);
            float near = saturate(1.0f - rho * 1.6f);
            swirl = max(swirl, arms * near * grow);
            if (shape == kWhite) swirl = max(swirl, exp(-rho / 0.06f) * grow);
        }

        if (falls && shape != kBlack) fallX = along - (s0 + len);
        if (along <= progress && dist < 999.0f) {
            float energy, thin;
            float3 hotC, haloC;
            GravFall(fallX, energy, hotC, haloC, thin);
            dist /= thin;
            float2 b = BeamProfile(dist, px);
            // En el pozo la corriente es mas marcada y va por `along`, que
            // acelera: son tirones que caen cada vez mas rapido.
            float flow = shape == kBlack ? 0.55f + 0.45f * sin((along - Time.x * 0.9f) * 14.0f) : BeamFlow(along);
            float tip = exp(-(progress - along) * 5.0f) * BeamProfile(dist * 0.6f, px).x;
            core = max(core, b.x * energy);
            halo = max(halo, b.y * energy);
            beam = max(beam, hotC * saturate(b.x * flow + tip * 0.8f) * energy);
            glow = max(glow, haloC * (b.y + tip * 0.3f) * energy);
        }
    }

    // La luz ya cayo: el horizonte la acusa con un resplandor rojo y quieto.
    // Es lo unico que queda de ella, y lo que dice que no va a salir.
    if (shape == kBlack && arrived > 0.0f) {
        float ember = exp(-abs(rho - 0.11f) / 0.035f) * arrived;
        lit += float3(0.55f, 0.05f, 0.04f) * ember * 0.45f;
        holeCover = max(holeCover, ember * 0.8f);
    }

    float boost = SolvedBoost(UserParams[0].y);
    lit += (beam + glow) * boost;
    lit += (kBeamHot * swirl * 0.9f + kBeamHalo * swirl * 0.4f) * boost;
    if (hole) clip(max(holeCover, max(swirl * 1.5f, max(core, halo * 1.5f))) - 0.06f);
    return float4(lit, AlbedoColor.a);
}
