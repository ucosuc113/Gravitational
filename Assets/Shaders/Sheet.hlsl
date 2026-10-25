// LA LAMINA: un punto, una linea o un plano, segun dos numeros. Es el
// lenguaje del menu (cada cara de la caja es una) y de la transicion entre
// niveles (el punto y la linea que escribe el nombre del nivel).
//
// Lo que cambia es cuantas dimensiones muestra: `stretch` la estira de punto a linea, `open` la abre de linea a
// plano. Asi el descenso dimensional no es un efecto puesto encima del menu:
// es el menu.
//
// Las coordenadas son de la LAMINA (en celdas, origen en su centro, u a la
// derecha y v arriba), medidas con los ejes que manda el script - no con la UV
// de la primitiva. Al plegarse la lamina gira, y el texto (que el script pone
// en pantalla con la misma cuenta) tiene que seguir cayendo en su celda.
//
// UserParams[0] = (origen xyz, tamano de celda en metros)
// UserParams[1] = (eje u xyz, stretch 0..1)
// UserParams[2] = (eje v xyz, open 0..1)
// UserParams[3] = (celdas u, celdas v, alfa de la cuadricula, brillo del punto)
// UserParams[4] = (celda elegida u, v (centro, en celdas), alfa de la eleccion, tramos 0..3)
// UserParams[5..7] = tramos de luz (u0, v0, u1, v1), ya recortados a lo revelado

float BLine(float d, float hw, float px) { return 1.0f - smoothstep(hw, hw + px, d); }

float4 PSMain(PSInput input) : SV_TARGET {
    float cellSize = max(UserParams[0].w, 1e-3f);
    float3 rel = input.worldPos - UserParams[0].xyz;
    float2 s = float2(dot(rel, UserParams[1].xyz), dot(rel, UserParams[2].xyz)) / cellSize;
    float stretch = UserParams[1].w;
    float open = UserParams[2].w;
    float cu = UserParams[3].x, cv = UserParams[3].y;
    float px = length(fwidth(s)) + 1e-5f;

    float halfU = cu * 0.5f * stretch;
    float halfV = cv * 0.5f * open;
    float3 beamColor = float3(0.55f, 0.85f, 1.0f);
    float3 col = 0.0f;
    float cover = 0.0f;

    // --- 0D: el punto. Late; es lo unico que hay en la pantalla. ---
    float r = length(s);
    float dotK = UserParams[3].w;
    float beat = 0.8f + 0.2f * sin(Time.x * 2.4f);
    float dotCore = BLine(r, 0.10f, px) * dotK;
    float dotGlow = 0.6f * exp(-r * 2.2f) * dotK * beat;
    col += beamColor * (dotCore * 1.4f + dotGlow);
    cover = max(cover, max(dotCore, dotGlow));

    // --- 1D: la linea, mientras el plano no se abrio. ---
    float lineK = saturate(stretch * 4.0f) * (1.0f - saturate(open * 3.0f));
    float inLine = 1.0f - smoothstep(halfU, halfU + px * 2.0f, abs(s.x));
    float lineCore = BLine(abs(s.y), 0.035f, px) * inLine * lineK;
    float lineGlow = 0.35f * exp(-abs(s.y) * 9.0f) * inLine * lineK;
    col += float3(1.0f, 1.0f, 1.0f) * lineCore * 0.6f + beamColor * lineGlow;
    cover = max(cover, max(lineCore, lineGlow));

    // --- 2D: la cuadricula, dentro de lo abierto ---
    float inside = (1.0f - smoothstep(halfU, halfU + px, abs(s.x))) * (1.0f - smoothstep(halfV, halfV + px, abs(s.y)));
    float2 g = s + float2(cu, cv) * 0.5f;
    float2 cell = frac(g);
    float2 dd = min(cell, 1.0f - cell);
    float2 w = fwidth(g);
    float2 hw = w * 0.75f;
    float2 l2 = 1.0f - smoothstep(hw, hw + w, dd);
    float grid = max(l2.x, l2.y) * inside * UserParams[3].z * saturate(open * 1.5f);
    // El borde de la lamina, un poco mas firme que la cuadricula.
    float edgeU = BLine(abs(abs(s.x) - halfU), 0.02f, px) * (abs(s.y) <= halfV + 0.02f ? 1.0f : 0.0f);
    float edgeV = BLine(abs(abs(s.y) - halfV), 0.02f, px) * (abs(s.x) <= halfU + 0.02f ? 1.0f : 0.0f);
    float edge = max(edgeU, edgeV) * saturate(open * 3.0f) * UserParams[3].z;
    // La cuadricula tenue: es el soporte del texto, no lo que se lee. El
    // blanco frio y los pesos son los de EL TRAZO de los shaders del juego, y
    // el borde queda por DEBAJO del umbral del bloom (0,6): con el en 0,77
    // cada arista de la caja del menu sangraba como si fuera luz.
    col += float3(0.60f, 0.67f, 0.78f) * max(grid * 0.45f, edge * 0.74f);
    cover = max(cover, max(grid, edge));

    // --- La celda elegida: se llena, y su marco respira ---
    float selA = UserParams[4].z * inside;
    if (selA > 0.001f) {
        float2 d = abs(s - UserParams[4].xy);
        float box = (d.x < 0.5f && d.y < 0.5f) ? 1.0f : 0.0f;
        float frame = max(BLine(abs(d.x - 0.5f), 0.025f, px) * (d.y < 0.52f ? 1.0f : 0.0f),
                          BLine(abs(d.y - 0.5f), 0.025f, px) * (d.x < 0.52f ? 1.0f : 0.0f));
        float k = 0.85f + 0.15f * sin(Time.x * 6.0f);
        col += beamColor * (box * 0.22f + frame * k) * selA;
        cover = max(cover, max(box * 0.4f, frame) * selA);
    }

    // --- La luz: hasta tres tramos, ya recortados por el script ---
    float core = 0.0f, glow = 0.0f;
    [unroll] for (int i = 0; i < 3; ++i) {
        if ((float)i < UserParams[4].w) {
            float4 seg = UserParams[5 + i];
            float2 a = seg.xy, b = seg.zw;
            float2 ab = b - a;
            float t = saturate(dot(s - a, ab) / max(dot(ab, ab), 1e-6f));
            float dist = length(s - (a + ab * t));
            core = max(core, BLine(dist, 0.05f, px));
            glow = max(glow, 0.5f * exp(-dist * 7.0f));
        }
    }
    col += beamColor * (core * 1.3f + glow) * inside;
    cover = max(cover, max(core, glow) * inside);

    // Umbral bajo: los halos se apagan solos antes de llegar a el (un umbral
    // alto les dibujaba un borde duro alrededor).
    clip(cover - 0.003f);
    return float4(col, 1.0f);
}
