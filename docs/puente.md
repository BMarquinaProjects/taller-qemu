# Protocolo del puente (v10)

> Referencia de [taller-qemu](../README.md). Los demás documentos están en el [índice](../README.md#documentación).

Con `-chardev socket,id=taller-puente,host=127.0.0.1,port=<p>,server=on,wait=on`, QEMU envía tramas
little-endian con una cabecera de 12 bytes: `u8 tipo, u8 version=10, u16 longitud_carga, u64 t_ns`.

| tipo | carga |
|---|---|
| 1 HOLA | `u32 version_protocolo` (= 10) |
| 2 GPIO | `u64 niveles, u64 salidas` (bit n = GPIO n) |
| 3 SPI  | `u8 bus` (clásico: 2 = HSPI, 3 = VSPI; C3/S3: **2 = SPI2/FSPI**) + bytes MOSI de una transacción, troceados si superan 4096 bytes |
| 4 PWM | `u8 gpio, u32 frecuencia_mHz, u32 ciclo_ppm` (9 bytes, sin relleno) |
| 5 I2C_ESCRITURA | `u8 bus, u8 direccion, bytes…`: escritura completa, incluido el puntero; hasta 4094 bytes de datos por trama |
| 6 DISPARO | `u8 pin_disparo`; `t_ns` es el instante virtual del flanco que inicia el tren |
| 7 RMT | `u8 canal, u8 gpio, u32 tick_ps, u32 símbolos…`: símbolos crudos LE; `t_ns` es el inicio de TX; se envía al terminar |
| 8 TIEMPO | sin carga; `t_ns` es la hora virtual. Latido cada 10 ms virtuales desde HOLA (v7) |
| 9 PULL | `u64 arriba, u64 abajo` (16 bytes, bit n = GPIO n); pull efectivo, desde v8 |
| 16 ENTRADA (hacia QEMU) | `u8 pin, u8 nivel`: 0/1 imponen el nivel; 2 suelta el pin desde v8 |
| 17 ANALOGICO (hacia QEMU) | `u8 gpio, u16 valor` (3 bytes, sin relleno): muestra cruda ADC1, saturada a 4095 |
| 18 I2C_DISPOSITIVO (hacia QEMU) | `u8 bus, u8 direccion, u8 activo`: alta o baja de una dirección |
| 19 I2C_REGISTROS (hacia QEMU) | `u8 bus, u8 direccion, u8 registro_inicial, bytes…`: hasta 240 bytes para el espejo |
| 20 ENTRADA_ARMADA (hacia QEMU) | `u8 pin_disparo, u8 flanco, u32 minimo_ns, u8 pin_salida, u16 n`, seguidos de `n × (u32 espera_ns, u8 nivel)`; `9 + 5·n` bytes, sin relleno |
| 21 ENTRADA_TREN (hacia QEMU) | `u8 pin, u16 n`, seguidos de `n × (u32 espera_ns, u8 nivel)`; `3 + 5·n` bytes, sin relleno, `n ≤ 64`, nivel 0/1/2; tren inmediato desde la recepción (v8) |
| 22 UNHILO (hacia QEMU) | `u8 pin, u8 rom[8], i16 medida, u8 bits`: un DS18B20 en 1-Wire (v9, ver abajo) |
| 23 HX711 (hacia QEMU) | `u8 pin_sck, u8 pin_dout, i32 a128, i32 b32, i32 a64, u8 bits`: un HX711 (v10, ver abajo) |

Sin ese chardev, QEMU se comporta como el de Espressif.

La versión 8 permite contactos pasivos: `ENTRADA(pin, 2)` deja de imponer la
entrada. IO_MUX conserva los registros completos (reset a cero) y extrae
FUN_PU/FUN_PD, bits 8/7. En el clásico se usa el orden físico de
`GPIO_PIN_MUX_REG` de ESP-IDF 5.5.6, no el número de GPIO: por ejemplo GPIO15
está en `+0x3c` y GPIO16 en `+0x4c`. RTC_IO también conserva registros y aporta
los pulls de `rtc_io_desc`: el pull efectivo es el OR con IO_MUX, sin modelar
la selección RTC/digital. GPIO34–39 carecen de pulls, incluso al escribir
FUN_PU/FUN_PD. C3 y S3 usan IO_MUX en `+4+4·GPIO`; en S3 no cambia RTC_IO.

Un pin suelto lee 1 con pull-up y 0 con pull-down. Con ambos se elige 1:
en el silicio es indefinido y el pull-up interno del ESP32 es algo más fuerte.
Sin ninguno conserva su último nivel externo, como simplificación determinista
de la carga del pad, sin simular su descarga. Se recalcula al soltar y al cambiar
pulls; un nivel impuesto ignora las resistencias. ENTRADA 0/1 y los pasos
armados vuelven a imponer el pin. El reset conserva `sueltos` y su nivel,
pero borra los registros y las máscaras de pull. El reposo inicial sigue alto
por compatibilidad hasta recibir entradas externas.

PULL se envía una vez inmediatamente después de HOLA y después solo cuando
cambia alguna máscara, con tiempo virtual y bits altos a cero en C3. SPI
pendiente se vacía antes del aviso. Un receptor antiguo puede saltar el tipo 9;
un QEMU anterior interpreta ENTRADA 2 como 1, el comportamiento previo.

Pruebas M-239: `pull_unitarias.py` compila los tres GPIO y el emisor reales
aislados con GCC/Clang o MSVC C11. Comprueba tablas, registros, OR RTC/digital,
GPIO34 sin pull, cuatro estados sueltos, cambios con entrada suelta/impuesta,
reset, armados y PULL sin duplicados; los sockets prueban UART fragmentada
y la inyección de los tres modos. Las pruebas integradas `pull`, `c3_pull` y
`s3_pull`, incluidas en `todas.sh`, usan GPIO15/16/34, GPIO9 y GPIO15,
respectivamente. Sembrarán cero y enviarán ENTRADA 2 antes de autorizar las
lecturas por UART, con dos latidos virtuales entre inyección y autorización.
Comprueban `PULL-ARRIBA=1`, `PULL-ABAJO=0`, `SIN-PULL=0`, GPIO34 conservando
cero y `IMPUESTO=0` con pull-up activo, además de las máscaras PULL esperadas.

Los tres sketches e imágenes fusionadas DIO de 4 MiB se compilaron con el CLI
de Taller 1.5.1 y Arduino ESP32 3.3.12. Para reproducirlos:

```powershell
$cli = "$env:APPDATA\dev.taller.app\arduino-cli\1.5.1\arduino-cli.exe"
$config = "$env:APPDATA\dev.taller.app\arduino\arduino-cli.yaml"
$casos = @(
  @('pull_prueba', 'esp32:esp32:esp32:FlashMode=dio'),
  @('c3_pull_prueba', 'esp32:esp32:esp32c3:FlashMode=dio,FlashFreq=40,CDCOnBoot=default'),
  @('s3_pull_prueba', 'esp32:esp32:esp32s3:FlashMode=dio,FlashSize=4M,PSRAM=disabled,CDCOnBoot=default'),
  @('tren_prueba', 'esp32:esp32:esp32:FlashMode=dio,FlashFreq=40')
)
foreach ($caso in $casos) {
  $nombre, $fqbn = $caso
  $compilacion = Join-Path $PWD ".trabajo\$nombre"
  $env:ARDUINO_BUILD_CACHE_PATH = Join-Path $PWD ".trabajo\cache-$nombre"
  & $cli --config-file $config compile --fqbn $fqbn --build-path $compilacion "pruebas/$nombre"
  if ($LASTEXITCODE -ne 0) { throw "Falló $nombre" }
  Copy-Item "$compilacion\$nombre.ino.merged.bin" "pruebas/$nombre.flash.bin"
}
```

```sh
PYTHONIOENCODING=utf-8 python3 -m unittest discover -s pruebas -p '*_unitarias.py'
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa pull pruebas/pull_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-riscv32 c3_pull pruebas/c3_pull_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_pull pruebas/s3_pull_prueba.flash.bin
```

## ENTRADA_TREN (tipo 21, v8)

Secuencia **inmediata** de niveles en tiempo virtual, sin esperar un flanco del
firmware: el rebote de un contacto (abre y cierra varias veces en 1–5 ms) o el
hueco de un conmutador «break-before-make». `ENTRADA_ARMADA` no sirve para eso,
porque solo arranca con un flanco de salida.

- Carga `u8 pin, u16 n, n × (u32 espera_ns, u8 nivel)`, LE y sin relleno: `3 + 5·n`
  bytes, `n ≤ 64`, nivel 0, 1 o 2 (suelto, como en ENTRADA). `n = 0` solo cancela
  el tren pendiente del pin.
- La primera espera cuenta desde que llega la trama; las demás, desde el paso
  anterior (plazos acumulativos: una atención tardía no desplaza los siguientes).
  Cada paso aplica su nivel por la misma ruta que ENTRADA; una espera 0 se aplica
  al momento.
- Un tren nuevo en el mismo pin sustituye al pendiente. `ENTRADA` en ese pin
  también lo cancela; un paso de `ENTRADA_ARMADA` no (son independientes).
- Ocho ranuras por chip; un noveno pin sustituye al tren más antiguo. El reset
  del chip cancela los trenes pendientes.
- Una carga inválida (largo que no cuadra, `n > 64`, nivel > 2 o pin fuera del
  chip: clásico 0..39, C3 0..21, S3 0..21 y 26..48) se ignora entera y no cancela nada.

La lógica pura está en `capas/include/hw/misc/taller_tren.h` y se prueba aislada
con `pruebas/tren_logica.c` (`tren_unitarias.py`). La prueba integrada `tren`
(en `todas.sh`) pone GPIO16 con `INPUT_PULLUP`, manda `0 (0 µs), 2 (300 µs),
0 (300 µs), 2 (300 µs), 0 (300 µs)` y el sketch, que cuenta flancos leyendo en
un bucle cerrado durante 50 ms (el GPIO no genera IRQ), exige `FLANCOS=4` y
`NIVEL=0`. Después manda `2 (0 µs), 0 (20 ms)` seguido de `ENTRADA(16, 1)` y exige
`CANCELADO=1`: el paso de 20 ms no llega.

La versión 7 añade TIEMPO: un latido cada 10 ms virtuales con la hora del ESP32, para que el
receptor la sepa aunque el firmware no toque ningún periférico (el arranque o un programa que solo
usa el Serial). Taller lo usa como reloj de los escenarios. Un receptor que no conozca el tipo 8 lo salta.
La versión 6 añade RMT a la versión 5, conserva las cargas anteriores y acepta las
tramas entrantes anteriores. HOLA y todas las tramas salientes anuncian ahora 10.
La versión 5 añade ENTRADA_ARMADA y DISPARO a la versión 4, conservando sus cargas.
La versión 4 añade I2C a la versión 3, conservando las cargas anteriores. La versión 3
añade PWM y ANALOGICO a la versión 2. La frecuencia se expresa en milihercios
(50 Hz = 50000 mHz) y el ciclo en partes por millón (5 % = 50000 ppm;
0 apaga la salida, 1000000 representa el 100 %). SPI pendiente se envía antes
de GPIO o PWM para conservar el orden de los cambios.
La frecuencia se satura a `4294967295 mHz` si supera el rango del `u32`.

ENTRADA_ARMADA instala un tren persistente: `flanco=0` para bajada y `flanco=1`
para subida, después de permanecer al menos `minimo_ns` en el nivel contrario.
Se vigila por pin el nivel **conducido por el ESP32**, `enable ? out : 1`, y su
instante de último cambio en `QEMU_CLOCK_VIRTUAL`. En entrada cuenta como alto
por el pull-up del módulo: los pulsos externos del propio sensor no lo redisparan.
Todos los pines empiezan altos y su antigüedad inicial se cuenta desde `t=0`.
Cambiar OUT sin habilitar la salida no cambia ese nivel ni reinicia su antigüedad.

Hay ocho ranuras, una por `pin_disparo`; un armado con el mismo pin sustituye al
anterior. `n=0` desarma y `n<=200`; GPIO de disparo/salida `0..39` y niveles `0/1`.
Longitud, flanco, pines y **todos** los pasos se validan antes de cambiar el estado;
una trama mal formada se ignora entera. Una novena ranura se ignora. Una ranura
desarmada que aún suena no se puede reutilizar para otro pin hasta que termine.
El búfer entrante admite los 1021 bytes de una trama máxima; las tramas que lo
exceden se drenan completas para conservar la siguiente cabecera.

Al detectar el flanco se emite un único DISPARO y se programa el primer paso.
Cada paso espera `espera_ns` desde el anterior (el primero desde el flanco) y
fija el nivel externo de `pin_salida` por la misma ruta que ENTRADA. Las esperas
cero se aplican en el mismo instante. Los plazos son absolutos y acumulativos,
con temporizadores del reloj virtual: una ejecución tardía no desplaza los pasos
siguientes. Mientras suena se ignoran nuevos disparos de ese pin. Un rearme o
desarmado durante el tren afecta al siguiente disparo; el tren en curso termina
con su copia de los pasos y del pin originales. Al reset del chip se cancelan
los trenes en curso, se conservan los armados y se reinicia la antigüedad de los
niveles altos en ese instante, sin generar DISPARO. SPI pendiente se vacía antes
de DISPARO. El `t_ns` de ENTRADA_ARMADA se ignora, como en las demás entradas.

I2C usa buses 0 y 1 en el clásico y solo bus 0 en el C3, con hasta ocho direcciones activas de 7 bits por bus
(1..127; no se admite general call). Sin alta, la dirección responde NACK.
Cada alta nueva crea un espejo de 256 bytes a cero y puntero a cero; repetir el alta
conserva el estado, y la baja lo borra. Una novena alta y buses/direcciones no válidos
se ignoran. Con puente se omite el TMP105 de demostración de Espressif en 0x48.
En cada escritura, el primer byte fija el puntero y los siguientes actualizan el espejo
con autoincremento; cada lectura devuelve `espejo[puntero++]`. El puntero vuelve a cero
tras 255. I2C_REGISTROS actualiza el espejo sin mover ese puntero; una dirección sin alta
se ignora. El estado externo se conserva durante resets del controlador.
I2C_ESCRITURA se envía al STOP o al START siguiente; un sondeo sin bytes no genera trama.
El búfer de escritura tiene 4096 bytes y se trocea en cargas de hasta 4096 bytes
(bus + dirección + hasta 4094 datos). Por encima de 4096 bytes se drenan bloques durante
la transferencia, sin perder bytes ni interpretar otro puntero. Cada fragmento repite
bus/dirección; v4 no incluye identificador ni marca de último fragmento. SPI pendiente
se envía antes de I2C_ESCRITURA. Las tramas entrantes ignoran `t_ns`.

ANALOGICO ignora `t_ns` y conserva el valor hasta otra trama, incluso durante un reset.
Los ocho canales ADC1 empiezan en cero. La correspondencia GPIO → canal es
36→0, 37→1, 38→2, 39→3, 32→4, 33→5, 34→6 y 35→7; otros GPIO se ignoran.
SENS (`0x3ff48800`, alias APB `0x60008800`, 0x400 bytes) completa la conversión RTC
al flanco ascendente de `MEAS1_START_SAR`, con DONE y DATA de solo lectura.
Si se seleccionan varios canales, usa el bit más bajo; sin canal válido devuelve cero.
Los demás registros guardan lo escrito y se borran al reset del dominio RTC.
No se emulan ADC2, DMA, latencia, atenuación ni calibración: el valor inyectado ya es crudo de 12 bits.

LEDC conserva los ocho timers y los dieciséis canales HS/LS del ESP32 clásico.
Se usa APB de 80 MHz, o REF_TICK de 1 MHz cuando `TICK_SEL=0`; no se simulan fades
ni la fuente RC de 8 MHz. Los cambios LS se recalculan también antes de `PARA_UP`,
sin repetir muestras idénticas. Si una señal aparece en varios GPIO, se usa el primero;
al cambiar de pin se apaga el anterior.

## UNHILO (tipo 22, v9): sensores DS18B20 en 1-Wire

Las ranuras de 1-Wire duran 60 µs y el sensor tiene que contestar dentro de cada una: no da
tiempo a preguntar fuera. Por eso el DS18B20 entero vive en QEMU
(`capas/include/hw/misc/taller_unhilo.h`, lógica pura) y el puente solo trae su ROM y su
temperatura.

- Carga `u8 pin, u8 rom[8], i16 medida, u8 bits`, 12 bytes exactos: la medida en 1/16 °C
  (LE) y el bit 0 de `bits`, presente (0 = quitar el sensor). Hasta 4 buses (pines) por chip
  y 8 sensores por bus. Una trama con la misma ROM actualiza la medida.
- El GPIO le cuenta al bus cada cambio de lo que conduce el ESP32 (`enable ? out : 1`, como los
  trenes armados). Un nivel bajo de ≥ 400 µs es un reset: presencia de 120 µs, 30 µs después
  de soltar. En las ranuras de lectura, el sensor que manda un 0 sujeta la línea 30 µs desde la
  misma bajada del maestro. En las de escritura, menos de 15 µs a 0 es un 1. Los pulsos van por
  la ruta de `ENTRADA_TREN`.
- Comandos: Read ROM (33h), Match ROM (55h), Skip ROM (CCh), Search ROM (F0h), Alarm Search
  (ECh), Convert T (44h; 0 en las ranuras de estado mientras convierte, el 80 % del máximo de la
  hoja: 600 ms a 12 bits), Read Scratchpad (BEh, con CRC8), Write Scratchpad (4Eh), Copy
  Scratchpad (48h, 10 ms), Recall E² (B8h) y Read Power Supply (B4h: alimentación externa).
- Al darse de alta, el sensor arranca como al encender: 85 °C y TH, TL y configuración de la
  EEPROM (75, 70 y 12 bits). El reset del ESP32 no le quita la tensión: conserva su scratchpad.
- En el clásico y el S3, 1 ms tras cada cambio del bus se mantiene el cuanto corto de las dos CPU
  de los trenes armados (M-157): si no, el turno de CPU1 alarga las ranuras de CPU0.

## HX711 (tipo 23, v10): el conversor de las células de carga

La librería HX711 de bogde sube PD_SCK, espera 1 µs y lee DOUT, 24 veces seguidas: no da tiempo
a preguntar fuera, y los trenes armados tienen un solo tren por flanco y sin cuenta de pulsos. Por
eso el protocolo vive en QEMU (`capas/include/hw/misc/taller_hx711.h`, lógica pura) y el puente
solo trae lo que convierte cada canal.

- Carga `u8 pin_sck, u8 pin_dout, i32 a128, i32 b32, i32 a64, u8 bits`, 15 bytes exactos (LE):
  las cuentas de 24 bits en complemento a dos del canal A con ganancia 128, del B con 32 y del A
  con 64 (se saturan a ±2²³), el bit 0 de `bits`, presente (0 = quitar el chip, DOUT suelto), y
  el bit 1, RATE a VCC (80 lecturas por segundo en lugar de 10). Un chip se identifica por su
  DOUT; hasta 4 por chip. La misma trama con otros valores los cambia sin reiniciar nada.
- El GPIO le cuenta cada cambio de lo que conduce el ESP32 en PD_SCK (`enable ? out : 1`). Al
  darse de alta con PD_SCK alto (en entrada), el HX711 está apagado; al bajar PD_SCK se enciende en
  A ×128 y DOUT baja 400 ms después (50 ms a 80 Hz). Desde ahí hay un dato cada 100 ms (12,5).
- Con dato, cada subida de PD_SCK impone en DOUT el bit siguiente al momento (MSB primero), por la
  ruta de `ENTRADA_TREN` con espera 0. La 25.ª sube DOUT y programa su bajada en la siguiente
  conversión; los pulsos 26 y 27 cuentan para la ganancia de la lectura siguiente (25 = A ×128,
  26 = B ×32, 27 = A ×64). El dato se toma al empezar a leer, con los valores de ese momento.
- PD_SCK en alto más de 60 µs apaga el chip: al bajar se reinicia en A ×128 y vuelve a tardar
  400 ms. El reset del ESP32 deja PD_SCK en entrada (alto): lo mismo.
- Sin dato (DOUT alto), los pulsos no sacan nada. Mientras se mueve PD_SCK, en el clásico y el S3 se
  mantiene 1 ms el cuanto corto de M-157. En la prueba con CPU0 siempre ocupada, sin él también
  pasa (los 60 µs dejan más margen que las ranuras de 15 µs del 1-Wire): se deja por prudencia.

## Prueba de HX711

`pruebas/hx711_prueba/hx711_prueba.ino` usa la librería HX711 de bogde (0.7.5) con DOUT en GPIO16
y PD_SCK en GPIO17, con CPU0 siempre ocupada. Tras `HX711-LISTO`, el modo `hx711` da de alta un
HX711 (A ×128 = −123456, B ×32 = 54321, A ×64 = −61728) y exige: la primera lectura tras `begin()`
espera los 400 ms de asentamiento, cada ganancia da su valor (con `set_gain()`, que vale para la
lectura siguiente), `tare(5)` tarda unos 500 ms y guarda −123456; tras `HX711-CAMBIO` el script
pone A ×128 = 2000000 y `get_value(3)` da 2123456; y `power_down()`/`power_up()` vuelve a esperar
400 ms. La imagen de 4 MiB, `pruebas/hx711_prueba.flash.bin`, se genera como la de
`armado_prueba` (core 3.3.12, `esp32:esp32:esp32:FlashMode=dio`). `pruebas/hx711_unitarias.py`
compila la lógica C con un firmware simulado con los tiempos de bogde (`pruebas/hx711_logica.c`):
encendido y asentamiento, las tres ganancias, saturación, cambio de valores, apagado de 60 µs,
reset del ESP32, baja, 80 Hz y cargas inválidas.

## Prueba de 1-Wire

`pruebas/unhilo_prueba/unhilo_prueba.ino` usa OneWire 2.3.8 y DallasTemperature 4.0.6 en GPIO4,
con CPU1 siempre ocupada. Tras `UNHILO-LISTO`, el modo `unhilo` da de alta dos sensores (21,5 y
−10,125 °C) y exige que la búsqueda encuentre los dos, que Read Power Supply diga alimentación
externa y las temperaturas a 12 y a 9 bits (−10,5 °C: los bits sobrantes a 0). La imagen de
4 MiB, `pruebas/unhilo_prueba.flash.bin`, se genera como la de `armado_prueba` (core 3.3.12,
`esp32:esp32:esp32:FlashMode=dio`). `pruebas/unhilo_unitarias.py` compila la lógica C con un
maestro simulado (`pruebas/unhilo_logica.c`): CRC de la hoja, presencia, conversión a 12 y 9
bits, búsqueda y Match ROM con dos sensores y bajas.
