/*
 * HX711 (M-260 de Taller): el conversor de 24 bits de las células de carga contesta dentro de QEMU,
 * con el reloj virtual. Lógica pura, sin dependencias de QEMU ni E/S: el GPIO le cuenta cada cambio
 * de lo que conduce el ESP32 (enable ? out : 1) y aplica en DOUT los niveles que devuelve, por la
 * misma ruta que ENTRADA_TREN. El puente (trama 23 HX711) solo trae las tres lecturas ya convertidas
 * (canal A ×128, canal B ×32 y canal A ×64): la librería HX711 de bogde lee cada bit 1 µs después de
 * subir PD_SCK, y no hay tiempo para preguntar fuera.
 *
 * Hoja de Avia (HX711, 2009): DOUT baja cuando hay dato; con cada subida de PD_SCK sale un bit, MSB
 * primero (T2 ≤ 0,1 µs), en complemento a dos. 24 pulsos sacan el dato; el 25.º vuelve a subir DOUT
 * y 1, 2 o 3 pulsos más (25, 26 o 27 en total) eligen la siguiente conversión: A ×128, B ×32, A ×64.
 * PD_SCK en alto más de 60 µs apaga el chip; al bajar se reinicia en A ×128. Con RATE a GND (los
 * módulos verdes) da 10 lecturas por segundo y tarda 400 ms en asentarse; con RATE a VCC, 80 y 50 ms.
 *
 * Copyright (c) 2026 Taller. Licencia: GPL-2.0-or-later.
 */
#ifndef HW_MISC_TALLER_HX711_H
#define HW_MISC_TALLER_HX711_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TALLER_HX711_CHIPS 4
#define TALLER_HX711_CARGA 15
#define TALLER_HX711_APAGADO_NS INT64_C(60000)          /* PD_SCK alto más de 60 µs: se apaga */
#define TALLER_HX711_PERIODO_10_NS INT64_C(100000000)   /* RATE a GND: 10 Hz */
#define TALLER_HX711_PERIODO_80_NS INT64_C(12500000)    /* RATE a VCC: 80 Hz */
#define TALLER_HX711_ASENTAR_10_NS INT64_C(400000000)   /* asentamiento tras encender o reiniciar */
#define TALLER_HX711_ASENTAR_80_NS INT64_C(50000000)
/* Tras un cambio de PD_SCK, cuánto dura el cuanto corto de las dos CPU (M-157): un pulso de 1 µs no
 * puede alargarse a más de 60 µs (y apagar el chip a media lectura) porque la otra CPU tenga el turno. */
#define TALLER_HX711_COLA_NS INT64_C(1000000)
#define TALLER_HX711_MAXIMO INT32_C(0x7FFFFF)
#define TALLER_HX711_MINIMO (-INT32_C(0x800000))

typedef struct TallerHx711Chip {
    bool activo;
    uint8_t sck;
    uint8_t dt;
    bool rapido;               /* RATE a VCC: 80 Hz */
    int32_t valor[3];          /* por pulsos de más: 1 = A ×128, 2 = B ×32, 3 = A ×64 */
    bool sck_alto;             /* lo último que conduce el ESP32 en PD_SCK */
    int64_t subida_ns;         /* cuándo subió PD_SCK por última vez */
    int64_t ultimo_cambio_ns;
    int64_t base_ns;           /* primera conversión lista tras encender: las demás, cada periodo */
    int64_t listo_ns;          /* desde cuándo hay dato que leer */
    unsigned pulsos;           /* pulsos de la lectura en curso (0 = sin empezar; 25..27 = terminada) */
    uint8_t ganancia;          /* la de la conversión pendiente, 1..3 */
    uint32_t dato;             /* los 24 bits que está sacando */
} TallerHx711Chip;

typedef struct TallerHx711 {
    TallerHx711Chip chips[TALLER_HX711_CHIPS];
} TallerHx711;

/* Lo que hay que imponer en DOUT: `n` pasos de ENTRADA_TREN (espera relativa y nivel 0/1/2). */
typedef struct TallerHx711Salida {
    uint8_t pin;
    uint8_t n;
    uint32_t espera_ns[2];
    uint8_t nivel[2];
} TallerHx711Salida;

static inline void taller_hx711_iniciar(TallerHx711 *h)
{
    memset(h, 0, sizeof(*h));
}

static inline int64_t taller_hx711_periodo_ns(const TallerHx711Chip *c)
{
    return c->rapido ? TALLER_HX711_PERIODO_80_NS : TALLER_HX711_PERIODO_10_NS;
}

static inline int32_t taller_hx711_saturar(int32_t v)
{
    return v > TALLER_HX711_MAXIMO ? TALLER_HX711_MAXIMO : v < TALLER_HX711_MINIMO ? TALLER_HX711_MINIMO : v;
}

static inline int32_t taller_hx711_leer_i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

/* El primer instante de la rejilla de conversiones (base + k · periodo) estrictamente posterior a `t`. */
static inline int64_t taller_hx711_siguiente_ns(const TallerHx711Chip *c, int64_t t)
{
    if (t < c->base_ns) {
        return c->base_ns;
    }
    int64_t periodo = taller_hx711_periodo_ns(c);
    return c->base_ns + ((t - c->base_ns) / periodo + 1) * periodo;
}

/* Enciende (o reinicia tras apagarse): A ×128 y DOUT alto hasta que se asienta. */
static inline void taller_hx711_encender(TallerHx711Chip *c, int64_t ahora_ns, TallerHx711Salida *s)
{
    int64_t asentar = c->rapido ? TALLER_HX711_ASENTAR_80_NS : TALLER_HX711_ASENTAR_10_NS;
    c->base_ns = c->listo_ns = ahora_ns + asentar;
    c->pulsos = 0;
    c->ganancia = 1;
    s->pin = c->dt;
    s->n = 2;
    s->espera_ns[0] = 0;
    s->nivel[0] = 1;
    s->espera_ns[1] = (uint32_t)asentar;
    s->nivel[1] = 0;
}

/* DOUT a `nivel` ya y, si `bajar_ns` > 0, a 0 cuando haya dato. */
static inline void taller_hx711_dout(const TallerHx711Chip *c, uint8_t nivel, int64_t ahora_ns,
                                     TallerHx711Salida *s)
{
    s->pin = c->dt;
    s->n = 1;
    s->espera_ns[0] = 0;
    s->nivel[0] = nivel;
    if (c->listo_ns > ahora_ns && c->pulsos >= 25) {
        int64_t espera = c->listo_ns - ahora_ns;
        s->n = 2;
        s->espera_ns[1] = espera > UINT32_MAX ? UINT32_MAX : (uint32_t)espera;
        s->nivel[1] = 0;
    }
}

static inline TallerHx711Chip *taller_hx711_chip(TallerHx711 *h, uint8_t dt, bool crear)
{
    TallerHx711Chip *libre = NULL;
    for (unsigned i = 0; i < TALLER_HX711_CHIPS; i++) {
        TallerHx711Chip *c = &h->chips[i];
        if (c->activo && c->dt == dt) {
            return c;
        }
        if (!c->activo && !libre) {
            libre = c;
        }
    }
    return crear ? libre : NULL;
}

/*
 * Carga HX711 (v10) sin cabecera: u8 pin_sck, u8 pin_dout, i32 a128, i32 b32, i32 a64 (LE, en
 * cuentas de 24 bits; se saturan), u8 bits (bit 0 = presente, bit 1 = RATE a VCC). 15 bytes exactos.
 * Un chip se identifica por su DOUT: la misma trama actualiza las lecturas; con el bit 0 a 0 se
 * quita y DOUT queda suelto. Al darse de alta, encendido si PD_SCK ya está bajo, apagado si no.
 * Devuelve si la carga es válida; en `s->n` deja lo que haya que imponer en DOUT (0 = nada).
 */
static inline bool taller_hx711_recibir(TallerHx711 *h, const uint8_t *d, size_t largo, uint64_t mascara,
                                        uint64_t out, uint64_t enable, int64_t ahora_ns,
                                        TallerHx711Salida *s)
{
    s->n = 0;
    if (largo != TALLER_HX711_CARGA || d[0] >= 64 || d[1] >= 64 || d[0] == d[1] ||
        !(mascara & (1ull << d[0])) || !(mascara & (1ull << d[1]))) {
        return false;
    }
    bool presente = d[14] & 1;
    TallerHx711Chip *c = taller_hx711_chip(h, d[1], presente);
    if (!c) {
        return !presente;
    }
    if (!presente) {
        c->activo = false;
        s->pin = c->dt;
        s->n = 1;
        s->espera_ns[0] = 0;
        s->nivel[0] = 2;
        return true;
    }
    bool nuevo = !c->activo || c->sck != d[0] || c->rapido != ((d[14] & 2) != 0);
    for (unsigned i = 0; i < 3; i++) {
        c->valor[i] = taller_hx711_saturar(taller_hx711_leer_i32(d + 2 + 4 * i));
    }
    if (!nuevo) {
        return true;
    }
    c->activo = true;
    c->sck = d[0];
    c->dt = d[1];
    c->rapido = (d[14] & 2) != 0;
    uint64_t bit = 1ull << c->sck;
    c->sck_alto = !(enable & bit) || (out & bit);
    c->subida_ns = c->ultimo_cambio_ns = ahora_ns;
    if (c->sck_alto) {
        /* Apagado hasta que el firmware baje PD_SCK. */
        c->pulsos = 0;
        c->ganancia = 1;
        c->base_ns = c->listo_ns = INT64_MAX;
        s->pin = c->dt;
        s->n = 1;
        s->espera_ns[0] = 0;
        s->nivel[0] = 1;
    } else {
        taller_hx711_encender(c, ahora_ns, s);
    }
    return true;
}

/* Un cambio de PD_SCK. Devuelve si hay que imponer algo en DOUT. */
static inline bool taller_hx711_cambio(TallerHx711Chip *c, bool alto, int64_t ahora_ns, TallerHx711Salida *s)
{
    c->sck_alto = alto;
    c->ultimo_cambio_ns = ahora_ns;
    if (!alto) {
        if (c->listo_ns == INT64_MAX || ahora_ns - c->subida_ns > TALLER_HX711_APAGADO_NS) {
            taller_hx711_encender(c, ahora_ns, s);
            return true;
        }
        return false;
    }
    c->subida_ns = ahora_ns;
    if (c->listo_ns == INT64_MAX) {
        return false; /* apagado */
    }
    if (c->pulsos == 0 || c->pulsos >= 25) {
        if (ahora_ns < c->listo_ns) {
            if (c->pulsos >= 25 && c->pulsos < 27) {
                c->pulsos++; /* pulso de ganancia */
            }
            return false; /* sin dato: DOUT sigue alto */
        }
        if (c->pulsos >= 25) {
            c->ganancia = (uint8_t)(c->pulsos - 24);
        }
        c->dato = (uint32_t)c->valor[c->ganancia - 1] & 0xFFFFFFu;
        c->pulsos = 0;
    }
    c->pulsos++;
    if (c->pulsos <= 24) {
        taller_hx711_dout(c, (uint8_t)((c->dato >> (24 - c->pulsos)) & 1), ahora_ns, s);
        return true;
    }
    /* 25.º: DOUT vuelve a subir y baja con la siguiente conversión. */
    c->listo_ns = taller_hx711_siguiente_ns(c, ahora_ns);
    taller_hx711_dout(c, 1, ahora_ns, s);
    return true;
}

/* El GPIO cuenta lo que conduce el ESP32; devuelve cuántas salidas ha dejado en `s`. */
static inline unsigned taller_hx711_observar(TallerHx711 *h, uint64_t out, uint64_t enable, int64_t ahora_ns,
                                             TallerHx711Salida s[TALLER_HX711_CHIPS])
{
    unsigned n = 0;
    for (unsigned i = 0; i < TALLER_HX711_CHIPS; i++) {
        TallerHx711Chip *c = &h->chips[i];
        if (!c->activo) {
            continue;
        }
        uint64_t bit = 1ull << c->sck;
        bool alto = !(enable & bit) || (out & bit);
        if (alto != c->sck_alto && taller_hx711_cambio(c, alto, ahora_ns, &s[n])) {
            n++;
        }
    }
    return n;
}

/* Reset del ESP32: el HX711 sigue alimentado, pero PD_SCK queda en entrada (alto): se apagará. */
static inline void taller_hx711_reiniciar(TallerHx711 *h, int64_t ahora_ns)
{
    for (unsigned i = 0; i < TALLER_HX711_CHIPS; i++) {
        TallerHx711Chip *c = &h->chips[i];
        if (c->activo && !c->sck_alto) {
            c->sck_alto = true;
            c->subida_ns = c->ultimo_cambio_ns = ahora_ns;
        }
    }
}

static inline int64_t taller_hx711_cola_ns(const TallerHx711 *h)
{
    int64_t fin = INT64_MIN;
    for (unsigned i = 0; i < TALLER_HX711_CHIPS; i++) {
        const TallerHx711Chip *c = &h->chips[i];
        if (c->activo && c->ultimo_cambio_ns && c->ultimo_cambio_ns + TALLER_HX711_COLA_NS > fin) {
            fin = c->ultimo_cambio_ns + TALLER_HX711_COLA_NS;
        }
    }
    return fin;
}

/* Carga de ENTRADA_TREN para una salida: u8 pin, u16 n, n × (u32 espera_ns, u8 nivel). */
static inline size_t taller_hx711_tren(const TallerHx711Salida *s, uint8_t carga[13])
{
    carga[0] = s->pin;
    carga[1] = s->n;
    carga[2] = 0;
    for (unsigned i = 0; i < s->n; i++) {
        uint8_t *q = carga + 3 + 5 * i;
        q[0] = (uint8_t)s->espera_ns[i];
        q[1] = (uint8_t)(s->espera_ns[i] >> 8);
        q[2] = (uint8_t)(s->espera_ns[i] >> 16);
        q[3] = (uint8_t)(s->espera_ns[i] >> 24);
        q[4] = s->nivel[i];
    }
    return 3 + 5 * (size_t)s->n;
}

#endif
