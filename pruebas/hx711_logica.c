/* Lógica pura del HX711 (trama 23, v10): un firmware simulado con los tiempos de la librería
 * HX711 de bogde (shiftInSlow: PD_SCK alto, 1 µs, leer DOUT, PD_SCK bajo, 1 µs). */
#include <assert.h>
#include <stdio.h>
#include "hw/misc/taller_hx711.h"

#define SCK 5
#define DT 4
#define US INT64_C(1000)
#define MS INT64_C(1000000)

static TallerHx711 h;
static int64_t t;
static uint64_t out, enable;
/* DOUT: nivel actual (0/1/2 = suelto) y el tren pendiente, como la ruta de ENTRADA_TREN. */
static uint8_t dout = 2;
static int64_t paso_ns[2];
static uint8_t paso_nivel[2];
static unsigned pasos;

static void aplicar(const TallerHx711Salida *s)
{
    assert(s->pin == DT && s->n >= 1 && s->n <= 2);
    /* Comprobar también la carga de ENTRADA_TREN. */
    uint8_t carga[13];
    size_t largo = taller_hx711_tren(s, carga);
    assert(largo == 3 + 5u * s->n && carga[0] == DT && carga[1] == s->n && carga[2] == 0);
    int64_t base = t;
    pasos = 0;
    for (unsigned i = 0; i < s->n; i++) {
        base += s->espera_ns[i];
        paso_ns[pasos] = base;
        paso_nivel[pasos++] = s->nivel[i];
    }
    /* Las esperas cero se aplican al momento. */
    while (pasos && paso_ns[0] <= t) {
        dout = paso_nivel[0];
        paso_ns[0] = paso_ns[1];
        paso_nivel[0] = paso_nivel[1];
        pasos--;
    }
}

static uint8_t leer_dout(void)
{
    while (pasos && paso_ns[0] <= t) {
        dout = paso_nivel[0];
        paso_ns[0] = paso_ns[1];
        paso_nivel[0] = paso_nivel[1];
        pasos--;
    }
    return dout == 2 ? 1 : dout; /* suelto: INPUT_PULLUP */
}

static void sck(bool alto)
{
    TallerHx711Salida s[TALLER_HX711_CHIPS];
    enable |= 1ull << SCK;
    if (alto) {
        out |= 1ull << SCK;
    } else {
        out &= ~(1ull << SCK);
    }
    unsigned n = taller_hx711_observar(&h, out, enable, t, s);
    for (unsigned i = 0; i < n; i++) {
        aplicar(&s[i]);
    }
}

static bool alta(int32_t a128, int32_t b32, int32_t a64, uint8_t bits)
{
    uint8_t d[TALLER_HX711_CARGA] = {SCK, DT};
    int32_t v[3] = {a128, b32, a64};
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < 4; j++) {
            d[2 + 4 * i + j] = (uint8_t)((uint32_t)v[i] >> (8 * j));
        }
    }
    d[14] = bits;
    TallerHx711Salida s;
    bool ok = taller_hx711_recibir(&h, d, sizeof d, 0xffffffffffull, out, enable, t, &s);
    if (ok && s.n) {
        aplicar(&s);
    }
    return ok;
}

/* read() de bogde: 24 bits MSB primero y `ganancia` pulsos más (1 = A128, 2 = B32, 3 = A64). */
static int32_t leer(unsigned ganancia)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < 24; i++) {
        sck(true);
        t += 1 * US + 300; /* delayMicroseconds(1) y lo que tarda digitalRead */
        v = (v << 1) | leer_dout();
        sck(false);
        t += 1 * US + 200;
    }
    for (unsigned i = 0; i < ganancia; i++) {
        sck(true);
        t += 1 * US;
        sck(false);
        t += 1 * US;
    }
    if (v & 0x800000u) {
        v |= 0xFF000000u;
    }
    return (int32_t)v;
}

/* wait_ready(): sondea DOUT cada 100 µs hasta que baja; devuelve cuánto ha esperado. */
static int64_t esperar_dato(void)
{
    int64_t desde = t;
    while (leer_dout()) {
        t += 100 * US;
        assert(t - desde < 2000 * MS);
    }
    return t - desde;
}

int main(void)
{
    taller_hx711_iniciar(&h);

    /* Arranque: PD_SCK en entrada (alto) → apagado; DOUT alto. */
    t = 1 * MS;
    assert(alta(-123456, 7777, -61728, 1));
    assert(leer_dout() == 1);
    /* Pulsos con el chip apagado: no hacen nada. */
    t = 2 * MS;
    sck(true);
    sck(false); /* bajar tras estar apagado lo enciende */
    int64_t encendido = t;
    assert(leer_dout() == 1);
    t += 399 * MS;
    assert(leer_dout() == 1);
    /* Sin dato, los pulsos no sacan nada. */
    sck(true);
    t += 1 * US;
    sck(false);
    assert(leer_dout() == 1);
    t = encendido + 400 * MS;
    assert(leer_dout() == 0);

    /* Primera lectura: A ×128; 27 pulsos piden A ×64 para la siguiente. */
    assert(leer(3) == -123456);
    assert(leer_dout() == 1);
    int64_t espera = esperar_dato();
    assert(t >= encendido + 500 * MS && t < encendido + 500 * MS + 100 * US);
    (void)espera;
    assert(leer(2) == -61728);    /* A ×64; 26 pulsos piden B ×32 */
    esperar_dato();
    assert(leer(1) == 7777);      /* B ×32; 25 pulsos vuelven a A ×128 */
    esperar_dato();
    assert(leer(1) == -123456);

    /* Las lecturas cambian sin reiniciar el chip (la misma trama, otro valor). */
    t += 10 * MS;
    assert(alta(2147483, 0, 1073741, 1));
    esperar_dato();
    assert(leer(1) == 2147483);
    /* Se satura a 24 bits. */
    esperar_dato();
    assert(alta(0x900000, -0x900000, 5, 1));
    assert(leer(2) == TALLER_HX711_MAXIMO);
    esperar_dato();
    assert(leer(1) == TALLER_HX711_MINIMO);

    /* El dato se queda listo si no se lee; sin pulsos, DOUT sigue bajo. */
    esperar_dato();
    t += 350 * MS;
    assert(leer_dout() == 0);
    assert(leer(1) == TALLER_HX711_MAXIMO);

    /* power_down(): PD_SCK alto 100 µs → apagado; power_up(): al bajar, otros 400 ms. */
    esperar_dato();
    sck(true);
    t += 100 * US;
    sck(false);
    int64_t despierto = t;
    assert(leer_dout() == 1);
    t += 300 * MS;
    assert(leer_dout() == 1);
    assert(esperar_dato() <= 100 * MS + 100 * US && t >= despierto + 400 * MS);
    assert(leer(1) == TALLER_HX711_MAXIMO); /* tras reiniciar, A ×128 */

    /* Un pulso de 50 µs no lo apaga (el límite es 60). */
    esperar_dato();
    sck(true);
    t += 50 * US;
    sck(false);
    assert(leer_dout() == 0);

    /* Cuanto corto: 1 ms tras el último cambio de PD_SCK. */
    assert(taller_hx711_cola_ns(&h) == t + TALLER_HX711_COLA_NS);

    /* Reset del ESP32: PD_SCK vuelve a entrada; pinMode(OUTPUT) lo baja y se enciende otra vez. */
    t += 5 * MS;
    taller_hx711_reiniciar(&h, t);
    enable &= ~(1ull << SCK);
    out &= ~(1ull << SCK);
    t += 200 * MS;
    sck(false);
    int64_t tras_reset = t;
    assert(leer_dout() == 1);
    esperar_dato();
    assert(t >= tras_reset + 400 * MS);

    /* Baja: DOUT suelto. */
    assert(alta(0, 0, 0, 0));
    assert(dout == 2);
    assert(taller_hx711_cola_ns(&h) == INT64_MIN);
    /* Otra baja del mismo chip: válida, sin nada que hacer. */
    assert(alta(0, 0, 0, 0));

    /* Alta con PD_SCK ya bajo y RATE a VCC: encendido al momento, 50 ms y cada 12,5 ms. */
    t = 3000 * MS;
    assert(alta(42, 0, 0, 3));
    int64_t alta80 = t;
    esperar_dato();
    assert(t >= alta80 + 50 * MS && t < alta80 + 50 * MS + 100 * US);
    assert(leer(1) == 42);
    esperar_dato();
    assert(t >= alta80 + 62500 * US && t < alta80 + 62500 * US + 100 * US);

    /* Cargas inválidas: largo, pines iguales, pin fuera del chip. */
    {
        uint8_t d[TALLER_HX711_CARGA] = {SCK, DT};
        TallerHx711Salida s;
        d[14] = 1;
        assert(!taller_hx711_recibir(&h, d, sizeof d - 1, 0xffffffffffull, out, enable, t, &s));
        d[1] = SCK;
        assert(!taller_hx711_recibir(&h, d, sizeof d, 0xffffffffffull, out, enable, t, &s));
        d[1] = 45;
        assert(!taller_hx711_recibir(&h, d, sizeof d, 0xffffffffffull, out, enable, t, &s));
        /* Cuatro chips como mucho. */
        d[0] = 20;
        for (uint8_t dt = 30; dt < 33; dt++) {
            d[1] = dt;
            assert(taller_hx711_recibir(&h, d, sizeof d, 0xffffffffffull, out, enable, t, &s));
        }
        d[1] = 33;
        assert(!taller_hx711_recibir(&h, d, sizeof d, 0xffffffffffull, out, enable, t, &s));
    }

    printf("HX711-LOGICA-OK\n");
    return 0;
}
