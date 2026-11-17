# ESP32 clásico: SPI con DMA, interrupciones, trenes armados y RMT TX

> Referencia de [taller-qemu](../README.md). Los demás documentos están en el [índice](../README.md#documentación).

Modelos y pruebas del ESP32 clásico (`qemu-system-xtensa -machine esp32`). Las tramas del puente
y las reglas de I2C, ADC1 y LEDC del clásico están en [`puente.md`](puente.md).

## SPI con DMA (v0.7.0)

SPI2 (HSPI) y SPI3 (VSPI) leen MOSI y escriben MISO mediante cadenas de
descriptores `lldesc_t` en SRAM interna (`0x3ffae000..0x3fffffff`). Esto permite
usar `machine.SPI` de MicroPython con el driver `spi_master` de ESP-IDF 5.x,
que inicializa el bus con `SPI_DMA_CH_AUTO`. MicroPython `SPI(2)` corresponde
a **VSPI/SPI3**; `SPI(1)` corresponde a HSPI/SPI2.

Registros añadidos al modelo (desplazamientos desde cada controlador):

| Registros | Semántica |
|---|---|
| `DMA_CONF` `0x100` | Conserva configuración; `in_rst`/`out_rst` reinician su motor y estado de descriptores. Los resets comunes FIFO/AHB no desmontan enlaces ya armados. Los bits de reset conservan el valor escrito hasta que el driver los baja. |
| `DMA_OUT_LINK` `0x104`, `DMA_IN_LINK` `0x108` | Dirección inicial de 20 bits, reconstruida como `0x3ff00000 \| addr`; `start`/`restart` arman el motor y se autolimpian; `stop` lo detiene. |
| `DMA_STATUS` `0x10c` | `rx_en`/`tx_en` indican motores armados. |
| `DMA_INT_ENA/RAW/ST/CLR` `0x110..0x11c` | RAW retenido, ST = RAW & ENA; escribir uno en CLR limpia el bit correspondiente. |
| Descriptores/EOF `0x120..0x144` | Descriptor actual, siguiente, búfer y último EOF procesados. |
| `DMA_RSTATUS/TSTATUS` `0x148/0x14c` | Estado de reposo, cero; no se simula la evolución de las FIFO. |
| `SLAVE` `0x38` | Estado real R/W de `trans_done` (bit 4) y habilitación `trans_inten` (bit 9). También permite que IDF provoque la IRQ escribiendo ambos bits. |

`DPORT_SPI_DMA_CHAN_SEL_REG` (`0x3ff005a8`) conserva seis bits: SPI1 en [1:0],
SPI2 en [3:2] y SPI3 en [5:4]. Para SPI2/3, 0 desactiva DMA, 1/2 seleccionan
canal y 3 es reservado. La IRQ SPI conserva las fuentes 30/31; las IRQ DMA
son independientes y llegan a las fuentes 53/54.

Orden del driver contrastado con `spi_ll.h` del core 3.3.12 y
[ESP-IDF 5.5](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_driver_spi/src/gpspi/spi_master.c):

1. Al inicializar el bus, selecciona canal DMA en DPORT y configura SPI/IRQ.
2. Al preparar cada transacción, limpia `SLAVE.trans_done`, comprueba `CMD=0`
   y configura `MOSI_DLEN`, `MISO_DLEN`, comando, dirección y temporización.
3. Para RX, prepara descriptores, pulsa `in_rst` y los resets de FIFO/AHB,
   escribe la dirección de `DMA_IN_LINK` y pone `start`.
4. Para TX, prepara descriptores, pulsa `out_rst` y los resets de FIFO/AHB,
   escribe la dirección de `DMA_OUT_LINK` y pone `start`. En TX sin RX, IDF
   puede arrancar antes un inlink NULL como workaround del silicio; sin fase
   MISO, el modelo no accede a ese descriptor.
5. Habilita MOSI/MISO después de preparar DMA y escribe `CMD.usr`.
6. El modelo completa la transferencia y fija `trans_done`; el driver lee
   ese bit por polling o recibe la IRQ SPI. El driver también lo escribe a
   uno al inicializar, para poder despertar la ISR cuando llega trabajo.

Cada descriptor contiene tres palabras little-endian: control, dirección de
búfer y dirección completa del siguiente descriptor. TX usa `length[23:12]`,
sin superar MOSI_DLEN ni continuar después de EOF. RX usa `size[11:0]`, escribe
la longitud recibida y pone EOF en el último descriptor utilizado. Ambos
devuelven `owner` a CPU. Los campos que `lldesc_t` llama `offset`/`sosf`
son reservados para SPI en el [TRM, sección 2.3.2](https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf)
y se conservan sin interpretarlos. TX marca `out_done`, `out_eof` y
`out_total_eof`; RX marca `in_done` e `in_suc_eof`. Una cadena inválida, un
owner CPU, una dirección ajena a SRAM o un fallo de memoria marca error de
descriptor; RX sin capacidad suficiente marca `inlink_dscr_empty`.

MOSI sigue pasando por SSI, `taller_puente_spi_byte` y `taller_puente_spi_fin`.
El núcleo recibe las mismas cargas v6; no hay mensaje DMA nuevo. Un framebuffer
de 40 960 bytes llega completo, troceado por el puente en bloques de hasta
4096 bytes. MicroPython además divide su escritura en transacciones de hasta
4092 bytes. El verificador concatena todas y exige cada byte en orden.
MISO procede de SSI; sin esclavo que responda devuelve `0xff`.
Las transacciones sin DMA siguen usando W0..W15, incluido Arduino/ST7735.
Se corrige además el índice del bucle PIO: la comparación de límites usa la
posición, permitiendo recibir todos los bytes incluso con MOSI `0xff`.

### Prueba MicroPython

El firmware externo no se incorpora a Git. Descarga
[ESP32_GENERIC-20260824-v1.29.0.bin](https://micropython.org/resources/firmware/ESP32_GENERIC-20260824-v1.29.0.bin),
SHA-256 `e67ad6015a0a504c1fec9aa9bbf589d0432ed28e62546f4f8dd8a147f8bd95f6`.
`preparar_micropython.py` comprueba ese hash y coloca el firmware en **0x1000**
de una flash de **4 MiB rellena de 0xff**, no en el offset cero:

```sh
python3 pruebas/preparar_micropython.py /ruta/ESP32_GENERIC-20260824-v1.29.0.bin
# Sin argumento, el preparador descarga la misma URL y verifica el hash.
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa spi_dma pruebas/micropython.flash.bin
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
bash pruebas/todas.sh /ruta/qemu-system-xtensa
```

`spi_dma_micropython.py` crea `SPI(2, baudrate=20_000_000, sck=Pin(18), mosi=Pin(23))`,
escribe `b"abcd"`, escribe `bytes(range(256)) * 160`, lee cuatro bytes y exige
`ffffffff`; termina con `TALLER-OK spi_dma`. El modo `spi_dma` introduce ese
programa por REPL raw paste, con control de ventana y plazo de espera. Exige
HOLA v8 y **40968 bytes exactos de VSPI**: los cuatro iniciales, el framebuffer
y los cuatro ceros MOSI que genera `read(4)`. Rechaza pérdidas, sobrantes,
cambios de orden y excepciones aunque aparezca la marca de éxito.
Cada ejecución usa una copia temporal de la flash para proteger la imagen
original de las escrituras VFS de MicroPython.

`todas.sh` añade la prueba cuando existe `pruebas/micropython.flash.bin`.
También admite `MICROPYTHON_FLASH=/ruta/flash.bin`; si se indica esa variable
y falta la imagen, falla. Sin imagen ni variable, anuncia la omisión y sigue
ejecutando todos los modos anteriores.

Las unitarias compilan en WSL la cabecera pura, el modelo SPI real aislado
de los servicios QEMU y el emisor SPI real del puente. Cubren una única
transacción de 40 960 bytes con 11 descriptores, TX/RX en HSPI/VSPI, SSI,
IRQ separadas, start/stop/reset/canal, cadenas inválidas, límites de longitud
y recepción sin sobreescribir centinelas. También prueban el verificador y
el protocolo raw paste por sockets.

**Límites:** ejecución síncrona, sin duración de reloj SPI/DMA, contención
simultánea de canales, modo continuo/circular, DMA SPI0/1 ni PSRAM. Los modos
burst se conservan como configuración sin efecto temporal. `OUT_AUTO_WRBACK`
no condiciona la devolución de owner en este modelo. Los errores detienen
el recorrido y completan la transacción con sus banderas; no se reproducen
bloqueos de silicio. La lógica acota cada recorrido a 32768 descriptores y
solo accede a SRAM interna; un descriptor guest no puede ejecutar MMIO.
Las pruebas aisladas no sustituyen ejecutar `spi_dma` y los modos anteriores
con el QEMU recompilado.

## Matriz de interrupciones

`hw/xtensa/esp32_intc.c` es una copia modificada del de Espressif. La matriz guarda el nivel de
cada fuente, y la línea de cada interrupción de CPU es el OR de todas las fuentes asignadas a ella;
se recalcula cuando cambia un nivel y cuando se reasigna una fuente. El original ponía la línea al
nivel de la última fuente que cambiaba y no miraba los niveles al reasignar. Pero ESP-IDF activa y
desactiva las interrupciones cambiando su asignación (`esp_intr_enable`/`disable`), y el driver
SPI deja `trans_done` alto antes de activarla para que su ISR entre en cuanto se active: con el
original, `machine.SPI` de MicroPython se quedaba esperando para siempre.

En el **C3** (`hw/riscv/esp32c3_intmatrix.c`, v0.12.1) el modelo de Espressif guarda los
niveles de sus 62 fuentes en un `uint64_t`, pero los consulta con `BIT(n)`, que es
`1UL << n`. En Windows `long` tiene 32 bits: para n ≥ 32 el desplazamiento es indefinido
y x86 lo cuenta módulo 32, así que la fuente n pisaba a la n − 32. Con el I2C0 (29)
activo, la matriz daba también por activa CACHE_CORE0_ACS (61), que IDF enruta a la
interrupción 25 de error de caché: `Guru Meditation Error … (Cache error)` y reinicio en
bucle, solo en Windows. Ahora usa `BIT_ULL()`; `pruebas/c3_intmatrix_unitarias.py`
compila la función real con un `BIT()` que imita a Windows y exige que no se pisen.

## GPIO de drenador abierto

`GPIO_PINn_REG` (0x88 + 4·n) se guarda entero y su bit 2 (`PAD_DRIVER`) marca el pin como
drenador abierto. En un pin de salida así, `GPIO_IN` da el AND cableado de lo que conduce el
ESP32 y lo que llega de fuera: un 1 suelta la línea y manda el exterior. El módulo `dht` de
MicroPython deja el pin del DHT22 en drenador abierto y lee así la respuesta del sensor; sin
esto leía siempre su propio 1. Los trenes armados vigilan `enable ? out : 1`, que en drenador
abierto sigue siendo correcto (un 1 es una línea suelta).

## Prueba PWM

`pruebas/pwm_prueba/pwm_prueba.ino` usa Arduino ESP32 core 3.x. Su imagen completa
de flash está en `pruebas/pwm_prueba.flash.bin` (generada con el core 3.3.12 y
`esp32:esp32:esp32:FlashMode=dio,FlashFreq=40`; QEMU necesita arranque DIO).
El modo `pwm` admite HOLA v3/v4/v5/v6, exige la marca UART y una muestra en GPIO13 de
50 Hz ±1 % con ciclo del 5 % ±0,3 %:

```sh
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa pwm pruebas/pwm_prueba.flash.bin
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
```

## Prueba analógica

`pruebas/analogico_prueba/analogico_prueba.ino` lee GPIO34 cada 20 ms con resolución de 12 bits.
`pruebas/analogico_prueba.flash.bin` contiene la flash completa, compilada con Arduino ESP32
core 3.3.12 y `esp32:esp32:esp32:FlashMode=dio,FlashFreq=40`.
El modo `analogico` admite HOLA v3/v4/v5/v6 y exige líneas UART completas: `A=0` inicial,
1900..2200 tras ANALOGICO(34, 2048) y 4000..4095 tras ANALOGICO(34, 4095).
En este core, `analogRead` devuelve el resultado crudo; con 12 bits no cambia la resolución
ni aplica la calibración de `analogReadMilliVolts`. Se conservan los márgenes solicitados.
El HAL de ADC1 RTC consulta también `SENS_SAR_SLAVE_ADDR1.MEAS_STATUS` (cero tras reset),
pero no consulta APB_SARADC/SYSCON para completar esta conversión; esas rutas corresponden
al controlador digital o a ADC2.

```sh
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa analogico pruebas/analogico_prueba.flash.bin
```

## Prueba I2C

`pruebas/i2c_prueba/i2c_prueba.ino` usa `Wire` del core Arduino ESP32 3.x,
bus 0, SDA 21 y SCL 22. Espera un segundo antes de escanear 0x08..0x77.
El modo `i2c` espera HOLA v4/v5/v6, registra 0x3C y precarga 0xD0/0xD1 con 0x60/0x61.
Exige únicamente `ENCONTRADO 0x3C`, `LEIDO=60,61`, `TALLER-OK i2c` y
una trama I2C_ESCRITURA con bus 0, dirección 0x3C y bytes `00 AF`.
La imagen completa `pruebas/i2c_prueba.flash.bin` se genera con core 3.3.12 y
`esp32:esp32:esp32:FlashMode=dio`:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:FlashMode=dio --build-path pruebas/i2c_prueba/build pruebas/i2c_prueba
cp pruebas/i2c_prueba/build/i2c_prueba.ino.merged.bin pruebas/i2c_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa i2c pruebas/i2c_prueba.flash.bin
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
```

## Prueba de trenes armados

Diagnóstico confirmado de los fallos intermitentes (E-054, M-157) y pruebas:
[`docs/m157-armado.md`](m157-armado.md#conclusión). Los plazos siguen siendo
acumulativos. Con `-icount`, las dos CPU del clásico y del S3 ejecutan por turnos:
la CPU0 puede retrasar el sondeo de la CPU1 aunque el flanco llegue puntual.
Su GPIO mantiene un temporizador virtual de 1 µs en tres fases: preparación
acotada, tren y cola. La preparación empieza cuando un pin armado y sin tren
queda al nivel contrario al flanco de disparo; dura menos de `minimo_ns + 20 µs`
desde ese cambio. Evita que un turno largo de CPU0 estire un pulso corto de
CPU1 y lo convierta en disparo válido. Pasado ese margen, el umbral ya se cumple
y se apaga: un pin fijo, incluso una entrada con pull-up, no lo mantiene siempre.
Durante el tren acota el retraso del sondeo de sus flancos. La cola de 20 µs tras
el último paso permite observar el flanco que cierra una medida, como la bajada
de ECHO que mide `pulseIn`, aunque CPU0 esté ocupada.
Un tren nuevo dentro de la cola la prolonga al terminar; el reset la anula.
El C3 tiene una CPU y conserva su comportamiento. La tolerancia sigue siendo ±3 µs.

`pruebas/armado_prueba/armado_prueba.ino` configura TRIG bajo, anuncia
`ARMADO-LISTO` y espera un segundo al inicio para que el script arme ambos trenes
tras HOLA v5/v6. Esperar esa marca evita contar como disparo la propia habilitación
inicial de GPIO5: pasa de entrada (conducido alto) a salida baja.

- A, GPIO4: subida tras al menos 800 µs en bajo. Pasos relativos: 30 µs→0,
  80 µs→1, 80 µs→0, 50 µs→1, 70 µs→0, 50 µs→1. El sketch mantiene el pin bajo
  1100 µs y lo suelta con `INPUT_PULLUP`, dos veces. El script exige `DHT=80,80,50,70`
  y `DHT2=80,80,50,70`, cada ancho con tolerancia ±3 µs. Un bajo de 300 µs debe
  dejar la línea alta durante los siguientes 500 µs: `CORTO=SIN_RESPUESTA`.
- B, TRIG=GPIO5 y ECHO=GPIO18: bajada tras al menos 8 µs en alto (un `delayMicroseconds(10)` dura ~9,9 µs). Pasos:
  450 µs→1 y 1166 µs→0. Se inicia ECHO bajo por ENTRADA. Un pulso TRIG de 10 µs
  se mide con `pulseIn(18, HIGH, 30000)` y debe dar `ECO=1163..1169`; uno de 5 µs
  debe dar `ECO_CORTO=0`.

El modo `armado` exige además `TALLER-OK armado`, exactamente dos DISPARO del pin 4
y uno del pin 5, sin otros DISPARO ni tiempos decrecientes. `pruebas/todas.sh`
ejecuta este modo con ambas imágenes, sin reintentos; todos los modos previos
admiten HOLA 6.

`pruebas/armado_ocupado/armado_ocupado.ino` repite la misma prueba y añade una
tarea fijada a CPU0 que nunca cede su turno. Reproduce de forma determinista
el fallo del reparto de `icount`: antes del arreglo daba `DHT=80,65,60,60`
y `ECO=1083`, frente a `80,80,50,70` y `1163..1169`. Usa el mismo modo `armado`
y sus comprobaciones, para que la regresión no dependa de coincidir con el tick
ocasional de FreeRTOS. Su imagen ya compilada es `pruebas/armado_ocupado.flash.bin`.

Para A se mide con `esp_cpu_get_cycle_count()` a 240 MHz (comprobado con
`static_assert`), lectura directa del registro GPIO y las interrupciones locales
suspendidas durante la captura. Un ciclo equivale a 4,17 ns; ambos flancos usan
el mismo bucle y se redondea la diferencia a µs. Así se reduce el coste por sondeo
y se evita la cuantización de `micros()`. En B se conserva `pulseIn`, que en el
core 3.3.12 también usa el contador de ciclos. La tolerancia de ±3 µs se comprueba
con el QEMU compilado y `-icount`; las pruebas unitarias no certifican esa medida.

La imagen **completa de flash**, de 4 MiB, es `pruebas/armado_prueba.flash.bin`:
salida `armado_prueba.ino.merged.bin` del core 3.3.12, FQBN
`esp32:esp32:esp32:FlashMode=dio`. Para regenerarla desde PowerShell:

```powershell
$cli = "$env:APPDATA\dev.taller.app\arduino-cli\1.5.1\arduino-cli.exe"
$config = "$env:APPDATA\dev.taller.app\arduino\arduino-cli.yaml"
$compilacion = Join-Path (Get-Location) '.trabajo\armado-compilacion'
& $cli --config-file $config compile --fqbn esp32:esp32:esp32:FlashMode=dio `
  --build-path $compilacion pruebas/armado_prueba
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de armado_prueba' }
Copy-Item "$compilacion\armado_prueba.ino.merged.bin" pruebas/armado_prueba.flash.bin
Remove-Item -LiteralPath $compilacion -Recurse -Force
```

Para regenerar la imagen completa de `armado_ocupado`, también de 4 MiB,
con la misma CLI, core y FQBN:

```powershell
$cli = "$env:APPDATA\dev.taller.app\arduino-cli\1.5.1\arduino-cli.exe"
$config = "$env:APPDATA\dev.taller.app\arduino\arduino-cli.yaml"
$compilacion = Join-Path (Get-Location) '.trabajo\armado-ocupado-compilacion'
& $cli --config-file $config compile --fqbn esp32:esp32:esp32:FlashMode=dio `
  --build-path $compilacion pruebas/armado_ocupado
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de armado_ocupado' }
Copy-Item "$compilacion\armado_ocupado.ino.merged.bin" pruebas/armado_ocupado.flash.bin
Remove-Item -LiteralPath $compilacion -Recurse -Force
```

```sh
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa armado pruebas/armado_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa armado pruebas/armado_ocupado.flash.bin
bash pruebas/todas.sh /ruta/qemu-system-xtensa
```

`armado_unitarias.py` compila y ejecuta la lógica C pura de `taller_armado.h`,
incluyendo rearme/desarmado durante un tren, mínimo inclusivo, 200 pasos, esperas
cero, GPIO39, ocupación de ranuras, reset y consulta de trenes sonando. Comprueba
preparación de ambas polaridades, límite exacto de `minimo_ns + cola`, pulso
corto sin disparo, enlace con el tren y margen mayor que 32 bits. Ejecuta
también los GPIO reales clásico y S3 con dobles de reloj y temporizadores:
plazos acumulados, cuanto a +1 µs desde preparación o disparo, parada de
preparación al vencer o rechazar un pulso de 5 µs, continuidad hasta el tren,
cola de 20 µs, ambos órdenes de callbacks
coincidentes y último paso entre ticks, dos trenes solapados, prolongación de
la cola con otro tren, parada, reset y liberación. Compila el receptor real
aislado de QOM/chardev para comprobar el descarte de tramas excesivas y
fragmentadas. Requiere `cc`, `gcc`, `clang`
o `cl` (en Windows, desde consola de desarrollo de Visual Studio); sin
compilador omite las pruebas C. Las del verificador usan solo Python.

## RMT TX y prueba de NeoPixel

`esp32.rmt` sustituye el hueco del SoC en `0x3ff56000` (alias APB `0x60016000`,
0x1000 bytes). Emula ocho canales TX y 512 palabras compartidas, en bloques de
64 por canal. RAM directa en `+0x800`, con `APB_CONF.fifo_mask=1`; con cero,
`CHnDATA` permite acceso FIFO. `CHnCONF0/1` conservan divisor, tamaño, propietario,
modo continuo y nivel/habilitación de reposo. `MEM_RD_RST` reinicia el lector y
el contador de umbral; `APB_MEM_RST`, los punteros FIFO. Son pulsos de control,
igual que `TX_START` y `REF_CNT_RST`. `CHnSTATUS` refleja lector, activo y memoria
agotada; no emula estados RX. Con tamaño cero o RAM propiedad de RX, TX no arranca
y marca el bit de error del canal. Los tamaños que exceden la RAM se acotan al
final de las 512 palabras. Un reset cancela timers, descarta trenes incompletos,
borra RAM y restaura registros e IRQ.

Cada palabra contiene `duration0:15, level0:1, duration1:15, level1:1`.
Una duración cero termina TX; si es la segunda, se consume primero la duración
inicial. Se conservan las palabras crudas, incluido el marcador de fin leído.
Con `mem_tx_wrap_en`, el lector vuelve al principio de los bloques del canal.
Sin wrap, agotar los bloques termina el tren. `tx_conti_mode` repite desde el
principio al encontrar fin o agotar memoria sin wrap, sin `tx_end` entre vueltas;
cada vuelta completa se publica como un tren. Un bucle vacío termina para evitar
un temporizador infinito sin avance de tiempo.

`tx_thr_event` aparece cada `tx_lim` símbolos consumidos (`tx_lim=0` lo desactiva),
independientemente de su máscara de interrupción. El contador se reinicia en cada
aviso, aunque RAW siga pendiente; un marcador de duración inicial cero no cuenta.
`tx_end` aparece al finalizar el último intervalo. RAW queda retenido;
ST = RAW & ENA, CLR limpia los bits escritos a uno. La IRQ de nivel llega a
`ETS_RMT_INTR_SOURCE` (47). APB se fija a 80 MHz y REF a 1 MHz, según
`ref_always_on`; divisor cero representa 256. El tick se fija al comenzar TX.
Los plazos usan `QEMU_CLOCK_VIRTUAL`, acumulando picosegundos y redondeando el
plazo total hacia arriba a ns. Así se conservan los ticks APB de 12,5 ns.
Solo se consulta la siguiente palabra cuando empieza su intervalo: los símbolos
ya enviados quedan copiados, aunque el firmware sobrescriba la RAM en la ISR.

El HAL Arduino del core 3.3.12 llama al driver RMT TX de ESP-IDF 5.5 con un
codificador de copia. `Adafruit NeoPixel` 1.15.5 pide **un bloque**, resolución
**10 MHz** (APB / 8) y usa símbolos alto/bajo de 4/8 ticks para cero y 8/4 para uno.
El driver configura wrap y umbral de **32 palabras**, llena inicialmente 64 y
alterna la recarga de ambas mitades en la ISR. Al completar la codificación
introduce un símbolo de duración cero y deshabilita la IRQ de umbral. El modelo
separa cargar y completar un símbolo para respetar ese orden y sus tiempos.

`MEM_OWNER` (bit 5 de `CHnCONF1`) **no** bloquea la transmisión: el driver deja el
valor de reinicio (1) en los canales de TX y el chip real transmite igual. Ese bit
solo decide quién escribe la RAM al recibir. Para diagnosticar el driver,
`TALLER_RMT_TRAZA=1` vuelca a la salida de error cada acceso a los registros del
RMT (no a su RAM) y cada cambio de la IRQ.

**Contrapresión en el umbral (v0.6.1).** Con la IRQ `tx_thr_event` activada, el canal
se para al marcarla y sigue cuando la ISR la limpia en `INT_CLR` (o la desactiva en
`INT_ENA`). En el chip, la ISR entra en pocos µs y le sobran los 40 µs de la otra
mitad. Pero en QEMU, con el firmware en `waiti` y `-icount sleep=on`, el reloj virtual
corre con el real, y la ISR podía llegar tarde: el modelo leía la mitad sin rellenar,
con colores distintos en cada ejecución. Los símbolos no cambian; el tren dura unos
µs más (8 LED: 282-290 µs, hasta ~330 µs con el equipo cargado).

La trama RMT llega **al terminar**, con `t_ns` del **inicio** del tren y el primer
GPIO conectado a `RMT_SIG_OUT0_IDX + canal` (87 + canal), capturado al iniciar;
si no hay ninguno, 255. SPI pendiente se vacía antes. Por ello, el `t_ns` de RMT
puede ser anterior al de otras tramas emitidas durante TX: no hay monotonía
global entre tipos. RMT no emite pulsos GPIO individuales; el nivel de reposo
se conserva en los registros y el consumidor recibe los niveles en los símbolos.
Sin puente ni pin conectado, timers, umbral, fin e IRQ funcionan igualmente.

Cada carga admite **16382 símbolos** (6 + 4·16382 = 65534 bytes). Un tren mayor
se trocea en tramas consecutivas con idénticos canal, GPIO, tick y `t_ns`, sin
partir palabras ni intercalar otras tramas. No hay índice ni marca de último
fragmento: el receptor debe reunir esos fragmentos antes de tratar el final de
la transmisión como reset WS2812. Las transmisiones incompletas se guardan en
memoria del anfitrión hasta terminar; un firmware con wrap infinito y sin fin
puede hacer crecer ese búfer. RX, DMA, portadora y cambios de reloj durante TX
quedan fuera del modelo.

`pruebas/rmt_prueba/rmt_prueba.ino` envía en GPIO13 los ocho colores
rojo, verde, azul, blanco, apagado, (1,2,3), (128,64,32), (255,255,0), y después
30 LED con RGB `(7·i, 255−5·i, 3·i)`, `i=0..29`. El verificador usa `tick_ps`,
umbral alto >0,55 µs, orden GRB y MSB primero; exige ambos trenes completos,
GPIO13, HOLA 6 y `TALLER-OK rmt`.

`T_SHOW` mide `micros()` alrededor del primer `show()` y debe estar entre
**200 y 2000 µs**, inclusivos. Se prepara el mismo canal con `rmtInit` antes de
medir para excluir su `delay(1)`. El tren nominal de esta versión de NeoPixel
dura **230,4 µs** (192 × 1,2 µs); ISR, codificación y despertar de la tarea añaden
coste. La medida real y la integración se comprueban después de compilar QEMU.
El modo `humo` permite comprobar también el firmware sin puente.

La imagen completa de 4 MiB `pruebas/rmt_prueba.flash.bin` está compilada con
CLI 1.5.1, core 3.3.12, NeoPixel 1.15.5 y `esp32:esp32:esp32:FlashMode=dio`:

```powershell
$cli = "$env:APPDATA\dev.taller.app\arduino-cli\1.5.1\arduino-cli.exe"
$config = "$env:APPDATA\dev.taller.app\arduino\arduino-cli.yaml"
$compilacion = Join-Path (Get-Location) '.trabajo\rmt-compilacion'
& $cli --config-file $config lib install 'Adafruit NeoPixel@1.15.5'
& $cli --config-file $config compile --fqbn esp32:esp32:esp32:FlashMode=dio `
  --build-path $compilacion pruebas/rmt_prueba
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación de rmt_prueba' }
Copy-Item "$compilacion\rmt_prueba.ino.merged.bin" pruebas/rmt_prueba.flash.bin
Remove-Item -LiteralPath $compilacion -Recurse -Force
```

```sh
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa rmt pruebas/rmt_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa humo pruebas/rmt_prueba.flash.bin 'TALLER-OK rmt'
bash pruebas/todas.sh /ruta/qemu-system-xtensa
```

`rmt_logica.c` prueba la misma cabecera que usa el dispositivo: ping-pong de
192/720 símbolos, varios bloques, ocho canales, umbral periódico y no divisor
del bloque, wrap, duración cero en ambas mitades, bucle, límites, reset, relojes,
memoria directa/FIFO e interrupciones. `rmt_unitarias.py` compila ese C en WSL,
prueba colores y fragmentos del verificador y compila el emisor real aislado
de chardev para verificar LE y troceado de 20000 símbolos. Estas pruebas no
sustituyen la compilación de QEMU ni certifican la latencia real de su IRQ.
