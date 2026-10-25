// EL FANTASMA: donde caeria la pieza con el proximo clic. Translucido (el alfa
// del material < 1 lo manda a la cola de transparentes): tiene que verse sobre
// el vacio negro y no taparle nada al jugador.
//
// Un relleno casi nulo y un CONTORNO en el color de la luz: con las piezas de
// varias celdas (una rampa, una esfera de 3) un relleno parejo era una mancha
// sin forma; el contorno dice que pieza es y para donde mira. Si la postura no
// toca nada util el director no la muestra, asi que aqui no hay estado de error.
//
// UserParams[0].x = 1 si NO se puede poner ahi (la celda esta ocupada): rojo.
float4 PSMain(PSInput input) : SV_TARGET {
    float3 V = normalize(CameraWorldPosPad.xyz - input.worldPos);
    float3 N = normalize(input.worldNormal);
    float rim = pow(1.0f - saturate(abs(dot(N, V))), 2.0f);
    float pulse = 0.85f + 0.15f * sin(Time.x * 4.0f);
    float3 color = UserParams[0].x > 0.5f ? float3(0.85f, 0.12f, 0.12f) : AlbedoColor.rgb;
    return float4(color, (AlbedoColor.a * 0.35f + rim * 0.55f) * pulse);
}
