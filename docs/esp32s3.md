# ESP32-S3: GPIO, LEDC, ADC1, I2C0, RMT TX, SPI2 y puente (fase 6d)

> Referencia de [taller-qemu](../README.md). Los demás documentos están en el [índice](../README.md#documentación).

Usar **qemu-system-xtensa -machine esp32s3**. La máquina de las capas
(`hw/xtensa/esp32s3.c`) sustituye el GPIO mínimo de Espressif y conecta el
mismo chardev `taller-puente`: HOLA, GPIO, SPI, PWM, DISPARO, TIEMPO y PULL **v8**.
ENTRADA, ENTRADA_ARMADA y ANALOGICO llegan a su receptor propio, sin cambiar
el protocolo: las máscaras GPIO ya son de 64 bits. Se conservan los demás
dispositivos de la máquina base `esp-develop-9.2.2-20260417`.

## Modelos y límites S3

El GPIO tiene **49 índices (0..48), de los que 45 son pines físicos**:
**GPIO0..21 y GPIO26..48**. GPIO22..25 y los bits superiores se ignoran,
también en entradas externas, tablas de registros y ambos pines de un tren.
Los registros, relativos a `DR_REG_GPIO_BASE = 0x60004000`, se contrastaron
con `soc/gpio_reg.h` del core Arduino ESP32 **3.3.12**:

| Registro | Banco bajo | Banco alto (bit 0 = GPIO32) |
|---|---|---|
| OUT / W1TS / W1TC | `0x04 / 0x08 / 0x0c` | `0x10 / 0x14 / 0x18` |
| ENABLE / W1TS / W1TC | `0x20 / 0x24 / 0x28` | `0x2c / 0x30 / 0x34` |
| IN, solo lectura | `0x3c` | `0x40` |

`GPIO_PINn_REG`, en `0x74 + 4·n`, conserva el valor escrito; PAD_DRIVER
(bit 2) activa drenador abierto. IN devuelve OUT en salida push-pull, el
nivel externo en entrada y su AND en salida de drenador abierto. Las
entradas externas empiezan altas; al soltarlas siguen los pulls de IO_MUX.
STRAP (`0x38`) conserva la propiedad `strap_mode`, por
defecto **4**, arranque flash. IRQ GPIO, filtros e inversión quedan
pendientes.

`FUNCn_OUT_SEL_CFG`, en `0x554 + 4·n`, conserva el registro completo y usa
**9 bits** de selección. Al reset selecciona software (**256**). Los
periféricos resuelven su primera ruta mediante `esp32s3_gpio_pin_de_senal`.
Los trenes reutilizan `taller_armado.h`, ahora con capacidad hasta GPIO48:
ocho ranuras, 200 pasos, reloj virtual, DISPARO, rearme persistente y
cancelación de la reproducción al reset, como en C3. Cada modelo mantiene
su límite propio (clásico 40, C3 22, S3 con su hueco). El constructor Python
`trama_entrada_armada(..., pines=49)` permite pines altos del S3; por defecto
sigue validando 40 índices para las pruebas del clásico.

| Pieza | Estado en S3 |
|---|---|
| LEDC | Reutiliza `hw/misc/esp32c3_ledc.c`, parametrizado a **8 canales LS y 4 timers**, con señales **73..80** (`LEDC_LS_SIG_OUT0..7` de `gpio_sig_map.h`). Base `0x60019000`; registros de canal, timer y reloj coinciden con C3. Frecuencia y ciclo usan el mismo cálculo, relojes y tramas PWM; cambiar la ruta apaga el pin anterior. El C3 conserva 6 canales y señales 45..50. Sin fades, IRQ ni pulsos GPIO individuales. |
| ADC1 | Modelo propio `hw/misc/esp32s3_saradc.c`: **10 canales, GPIO1..10**, según `adc_channel.h`. `adc_ll.h` usa **SENS**, base **`0x60008800`**, no `APB_SARADC.ONETIME_SAMPLE` del C3. `SAR_MEAS1_CTRL2` (`0x0c`) selecciona un pad en bits 30..19; START (17) completa la conversión y publica DONE (16) y dato (15..0), ambos de solo lectura. Bajar START limpia DONE; mantenerlo alto no repite conversión. `SARADC_MEAS_STATUS` permanece libre. ANALOGICO inyecta el crudo, limitado a 4095; las entradas sobreviven al reset. `SAR_MEAS2_CTRL2` (`0x30`) termina con cero para permitir la autocalibración de arranque de ADC2. Sin canales ADC2 externos, DMA, ULP, latencia, IRQ ni calibración analógica. |
| UART, TIMG, SYSTIMER, matriz IRQ y flash | Modelos de Espressif conservados. El revisor confirmó previamente arranque Arduino por UART0 con flash DIO desde 0x0 y watchdog TIMG desactivado. |
| I2C0 | Reutiliza `hw/i2c/esp32c3_i2c.c` sin parámetros: mapa, bits y comandos coinciden con S3. Base `0x60013000`, fuente IRQ `ETS_I2C_EXT0_INTR_SOURCE`=42. FIFO APB de 32 bytes, ocho comandos, END sin STOP, RSTART, NACK y reset; espejo `taller_i2c` del puente en bus 0, preparado antes de HOLA. El receptor acepta I2C_DISPOSITIVO(18)/I2C_REGISTROS(19); las escrituras salen como I2C_ESCRITURA(5). Sin I2C1, esclavo, DMA, ventanas FIFO de RAM ni temporización eléctrica. |
| RMT TX | Reutiliza `hw/misc/esp32c3_rmt.c` con propiedad `s3=true`: **TX0..3**, registros RX4..7 sin recepción, **48 palabras por canal**, RAM en **`+0x800`** (`0x60016800`, distinta del C3), señales GPIO **81..84** y IRQ `ETS_RMT_INTR_SOURCE`=40. Base `0x60016000`. Mapa y STATUS propios del S3, préstamo limitado a bloques TX, wrap, umbral con contrapresión y recarga ping-pong, STOP/reset y fin. RMT(7) transmite GPIO, tick y todos los símbolos, conservando inicio al trocear. Mismos relojes y cálculo del tick C3. Sin RX, DMA, portadora, sincronización ni modo continuo. |
| SPI2/FSPI de usuario | Motor `hw/ssi/esp32c3_spi2.c` compartido mediante tipo `esp32s3.spi2`: base **0x60024000**, W0–W15, UPDATE/USR, MS_DLEN de 18 bits, CPU y GDMA TX/RX. Cada transacción vacía MOSI exacto por **bus 2**; MISO FF. IRQ `ETS_SPI2_INTR_SOURCE`, fin bit 12, RAW/ENA/ST/CLR e **INT_SET +0x44** W1S para primera ISR del HAL S3. Señales FSPICLK=101 y FSPID=103. SPI1 conserva flash/PSRAM. |
| GDMA | Cinco canales del modelo S3 de Espressif, IRQ IN/OUT separadas y mapa nativo (canal a stride 0xc0, OUT +0x60, LINK +0x20). Adaptador de las capas limpia pulsos STOP/reset y PARK al START. SPI2 exige periférico 0 y START; descriptores de 12 bytes en SRAM **0x3FC88000..0x3FD00000**, direcciones **absolutas** en dma_as (E-046), OWNER/EOF, errores y cadenas acotadas. |

Contraste registro a registro y diferencias C3/S3 en
[`docs/esp32s3-i2c-rmt.md`](esp32s3-i2c-rmt.md). Se compararon las cabeceras
instaladas de Arduino **3.3.12 / IDF 5.5**. La matriz Xtensa S3 usa índices por
fuente, sin máscaras `long`; las fuentes 40/42 no sufren E-045 en Windows.

SPI2 se contrasta con Arduino **3.3.12 / IDF 5.5** en
[`docs/esp32s3-spi.md`](esp32s3-spi.md): mapa compartido, INT_SET y
SPI_DATE propios del S3, diferencias CTRL/octal/DTR y mapa GDMA. Sin tiempo
eléctrico, orden de bits en hilo, dual/quad/octal, esclavo, PSRAM DMA,
RESTART, circular ni buffers SCT. No genera pulsos de reloj/CS por GPIO.

## Pruebas S3

- `s3_gpio`: tres pulsos completos en **GPIO2**, lectura **GPIO4** con
  INPUT_PULLUP y ENTRADA(4, 0). Exige `S3-ANTES=1`, `S3-DESPUES=0` y
  `TALLER-OK s3_gpio`, líneas únicas y ordenadas, HOLA v8 y máscaras válidas.
- `s3_pwm`: **GPIO4**, 50 Hz al 5 % → 25 % → 100 Hz → apagado. Exige las
  tramas PWM en orden, margen del 0,5 % y `TALLER-OK s3_pwm` único.
- `s3_analogico`: `analogRead(1)`; inyecta ANALOGICO(1, 2048) y luego 4095.
  Exige lecturas 0 → 1900..2200 → 4000..4095 y `TALLER-OK s3_analogico`.
- `s3_i2c`: Wire hardware, **SDA8/SCL9**, 100 kHz. Inyecta alta de 0x3C
  y registro D0=AB; exige lectura con RSTART, escritura exacta `00 AF`,
  ACK=0, NACK=2 de 0x3D y línea única `TALLER-OK s3_i2c`.
- `s3_rmt`: `rmtWrite`, **GPIO10**, 10 MHz y un bloque de 48 palabras.
  Envía 8 LED RGB(17,34,51) y 30 RGB(68,85,102) en orden GRB. Exige ambos
  trenes completos, tick de 100 ns, fin explícito, 193/721 símbolos y
  `TALLER-OK s3_rmt`. Ambos tamaños fuerzan recarga por ISR.

- `s3_spi`: **SCK12/MOSI11/MISO13**, `SPI` Arduino emite `abcd` por CPU.
  Después `spi_master` con DMA automático e ISR envía **32768+8192 bytes**
  de patrón `uint8_t(i)` y lee cuatro bytes por DMA. Exige **40968 bytes
  MOSI exactos por bus 2**, HOLA v8, `S3-SPI-MISO=ffffffff` y
  `TALLER-OK s3_spi`, completos y ordenados. Verificador puro, sockets y
  modelos reales SPI2/GDMA/emisor aislados; incluye canales 0..4, SRAM alta,
  INT_SET, STOP/reset, OWNER/EOF, cadenas parciales/cíclicas y regresión C3.

Los verificadores puros y lanzadores se comparten con C3, parametrizados por
chip, GPIO y máscara válida. Cada ejecución usa una **copia privada** de la
flash de **4 MiB**, sin eFuse ECO3 del clásico, UART0, `-icount
shift=2,align=off,sleep=on` y
`-global driver=timer.esp32s3.timg,property=wdt_disable,value=true`.
La instalación de QEMU debe incluir la ROM `esp32s3_rev0_rom.bin`.

Los seis sketches e imágenes están en `pruebas/s3_*_prueba/` y
`pruebas/s3_*_prueba.flash.bin`. Compilados con CLI de Taller **1.5.1** y core
**3.3.12**; bootloader en **0x0**, particiones en **0x8000**, boot_app0 en
**0xe000** y aplicación en **0x10000**:

```powershell
$cli = "$env:APPDATA\dev.taller.app\arduino-cli\1.5.1\arduino-cli.exe"
$config = "$env:APPDATA\dev.taller.app\arduino\arduino-cli.yaml"
$fqbn = 'esp32:esp32:esp32s3:FlashMode=dio,FlashSize=4M,PSRAM=disabled,CDCOnBoot=default'
foreach ($nombre in 's3_gpio_prueba', 's3_pwm_prueba', 's3_analogico_prueba', 's3_i2c_prueba', 's3_rmt_prueba', 's3_spi_prueba') {
  $compilacion = Join-Path $PWD ".trabajo\$nombre"
  & $cli --config-file $config compile --fqbn $fqbn `
    --build-path $compilacion "pruebas/$nombre"
  if ($LASTEXITCODE -ne 0) { throw "Falló $nombre" }
  Copy-Item "$compilacion\$nombre.ino.merged.bin" "pruebas/$nombre.flash.bin"
}
```

SHA-256 de las imágenes incluidas:

| Imagen | SHA-256 |
|---|---|
| `s3_gpio_prueba.flash.bin` | `cf1c71264997a19975b0810ad1b51c367e4be777d9b400786b2306061b1f5407` |
| `s3_pwm_prueba.flash.bin` | `8351ca35f7e3fe6bdab0d9878a6ae352795dc6d90e70bcfdf63bf4ae8107a1db` |
| `s3_analogico_prueba.flash.bin` | `ec1337eca9dc628588fe472069f00900fc3779d0eaba87ab5e0a5e9eb3b14cd8` |
| `s3_i2c_prueba.flash.bin` | `c7e824ba8c74f95308d4d1c70247509989c7db5ba470514d36258ba0f0f15246` |
| `s3_rmt_prueba.flash.bin` | `a508468145db10c0233864a3a5580ac034db0040647b9734065474dfbafad55e` |
| `s3_spi_prueba.flash.bin` | `cea4c292ce30fca58fe927583059636d2e678f63fc619d6c4edcf697d500aa07` |

```sh
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_gpio pruebas/s3_gpio_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_pwm pruebas/s3_pwm_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_analogico pruebas/s3_analogico_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_i2c pruebas/s3_i2c_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_rmt pruebas/s3_rmt_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_spi pruebas/s3_spi_prueba.flash.bin
bash pruebas/todas.sh /ruta/qemu-system-xtensa /ruta/qemu-system-riscv32
```

El bloqueo de `s3_analogico` sin ninguna línea `S3-A=` se explica por el
constructor [`adc_hw_calibration()` de IDF 5.5.5](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_adc/adc_common.c):
calibra **ambas unidades y sus cuatro atenuaciones antes de `setup()`**.
Sin eFuses de calibración, `read_cal_channel()` pulsa START sin pads y espera
DONE también en ADC2 (`SENS+0x30`), que antes solo almacenaba escrituras.
Ahora completa con cero (GND interno); ADC1 conserva las entradas del puente.
En una lectura normal, `adc_oneshot_ll_start()` espera `MEAS_STATUS=0` en
`SENS+0x40` y `adc_oneshot_hal_convert()` espera DONE en `SENS+0x0c`.
El arbitraje APB afecta a ADC2; no hace falta simular ADC digital ni modificar
RTC_CNTL para resolver esta espera. La regresión C reproduce calibraciones
repetidas de ambas unidades, protección DATA/DONE, independencia y reset.

Tras recompilar QEMU con las capas actualizadas, esta traza opcional vuelca
todos los accesos SENS a stderr (L/E, dirección absoluta, tamaño y valor):

```sh
TALLER_ADC_TRAZA=1 python3 pruebas/qemu_prueba.py "$HOME/qemu-trabajo/instalar-x86_64-linux-gnu/qemu/bin/qemu-system-xtensa" s3_analogico pruebas/s3_analogico_prueba.flash.bin 2> /tmp/s3-adc.traza
```

Buscar `60008830`: tras START (bit 17), la lectura debe tener DONE (bit 16)
y dato cero; después deben verse conversiones ADC1 en `6000880c`.
El verificador exige lecturas UART `S3-A=0`, `S3-A=2048`, `S3-A=4095` y
`TALLER-OK s3_analogico`; si pasan, muestra `OK s3_analogico: GPIO1 0 → 2048 → 4095`.
Sin la variable, o con valor distinto de `1`, la traza permanece apagada.
El lanzador hereda stderr cuando está activa para evitar llenar su tubería
interna y bloquear QEMU durante la calibración.

Las unitarias compilan y ejecutan los modelos C reales aislados de QOM:
ambos bancos, GPIO48, huecos, selección de 9 bits, drenador abierto, trenes,
ocho canales LEDC/cuatro timers y diez canales ADC1, además de reset.
Prueban el receptor real S3 con el parser fragmentado y los tres lanzadores
mediante sockets, incluida fragmentación UART, inyecciones y flash privada.
También pasan las unitarias previas. En la fase anterior se compilaron aisladamente ocho unidades
C con cabeceras reales de QEMU (máquinas S3/C3, GPIO S3/C3/clásico, LEDC
compartido, ADC1 S3 y puente). **Pendientes enlace de QEMU y ejecución real
de `todas.sh` con los binarios recompilados**; las pruebas aisladas no
certifican registro QOM, arranque ni temporización integrada.
La investigación opcional de MicroPython S3 no se realizó en esta fase.

Para I2C0/RMT S3, la suite reúne **121 pruebas: 110 pasan y 11 se omiten**
porque requieren GCC/Clang, ausentes en el entorno nativo. Los modelos reales
compartidos pasan compilación y ejecución aisladas con **MSVC C11**: ambos
mapas RMT, cuatro TX S3, RAM S3 en **+0x800**, recargas de 8/30 LED, GPIO48,
relojes, fin y reset. También pasa el parser/receptor real con altas y registros
I2C fragmentados en bus 0. Se contrastaron 40 offsets de las estructuras IDF
reales por chip y los 314 campos numéricos I2C comunes. `todas.sh` pasa revisión
de sintaxis. Revisión independiente sin hallazgos pendientes.

Las dos imágenes nuevas se compilaron con el CLI de Taller; se verificaron
componentes, DIO, identificador S3, FQBN y tamaño de 4 MiB. Los sketches usan
Wire y `rmtWrite`, como sus equivalentes C3. En la revisión (v0.13.0) se
compiló QEMU completo y `todas.sh` pasa sus 23 modos en Linux, `s3_i2c` y
`s3_rmt` incluidos; 121 unitarias. La matriz de interrupciones Xtensa del S3
usa índices en su mapa, sin el problema de `BIT()` del C3 en Windows; la
conservación de niveles y el OR al reasignar se corrigen en el paso SPI2.

En este paso SPI2 S3, la revisión en QEMU detectó un bloqueo antes del
primer GDMA: IDF habilita la interrupción reasignando la fuente con
`trans_done` ya alto. La capa `esp32s3_intc.c` y su cabecera conservan
niveles por fuente/CPU y recalculan el OR en las líneas vieja y nueva.
Sus seis regresiones fallan con el original del tarball y pasan con el arreglo.
**137 unitarias pasan en WSL, sin omisiones**; matriz, SPI2, GDMA S3 y máquina
S3 generan objetos aislados con GCC, cabeceras reales y `-Werror`.
RAW/ENA/SET/CLR de SPI2 y RAW/ENA/CLR de GDMA ya recalculan sus IRQ.
Imagen S3 DIO de 4 MiB compilada y verificada. En la revisión (v0.14.0) se compiló QEMU completo: `todas.sh` pasa sus 23 modos en Linux,
`s3_spi` incluido (40 968 bytes MOSI exactos por el bus 2 y MISO `ffffffff`), y 137 unitarias.
Detalle en [`docs/esp32s3-spi.md`](esp32s3-spi.md).

## Flash del S3 en QIO y MicroPython (v0.15.0)

QEMU elige el chip de flash del S3 por el tamaño de la imagen: 4 MiB `gd25q32`, 8 MiB `gd25q64` y
16 MiB `is25lp128`. Solo el ISSI acepta activar QIO (los GigaDevice responden «qio_mode: Failed to
set QIE bit» y IDF sigue en DIO). En QIO, IDF lee con `0xEB` y 6 ciclos dummy (bits de modo incluidos),
que con 4 bits por ciclo son 3 bytes, justo lo que espera `m25p80` para el ISSI. La copia de
Espressif de `esp32s3_spi.c` los convertía como si fueran de una línea (1 byte), y cada lectura
salía desplazada 2 bytes. Ahora `esp32s3_spi_get_dummy` multiplica por las líneas del modo
(`FREAD_QIO` ×4, `FREAD_DIO` ×2).

Con eso, el MicroPython oficial `ESP32_GENERIC_S3` v1.29.0 (compilado en QIO) arranca y llega al
REPL con la imagen rellena con `0xFF` hasta **16 MiB**: «Performing initial setup» formatea su
sistema de archivos. Con 4 u 8 MiB no: el chip GigaDevice no acepta QIO, y la cabecera de 4 MB
impide registrar la partición `vfs` en una flash de 8 MiB. La prueba `s3_flash_qio` (sketch
Arduino en QIO; el arnés rellena la imagen hasta 16 MiB) comprueba que borrar, escribir y leer
no tenga desfase.

## Reloj de la CPU del S3 (v0.18.0, E-096 de Taller)

El modelo de reloj del S3 de Espressif ignora las escrituras de `SYSTEM_CPU_PER_CONF` (0x600C0010)
y `SYSTEM_SYSCLK_CONF` (0x600C0060), y la CPU se quedaba con los 40 MHz del núcleo: CCOUNT corría 4
veces más despacio de lo que cree ESP-IDF (160 MHz) y `esp_rom_delay_us(10000)` duraba 40 ms. La
máquina intercepta ahora esos dos registros (`taller_reloj_s3_*`) y pone el reloj de las dos CPU en
lo que elige el firmware: con `SOC_CLK_SEL` = PLL, 80, 160 o 240 MHz según `CPUPERIOD_SEL` y
`PLL_FREQ_SEL`; con XTAL, 40 MHz. Hasta la primera escritura, 40 MHz. Igual que hace el clásico
con `DPORT_CPU_PER_CONF`. Las **lecturas** siguen dando lo mismo que el modelo de Espressif (PLL de
480 MHz, 80 MHz y cristal de 40 MHz): si devolvían lo escrito, ESP-IDF abortaba al arrancar. En el
cambio, CCOUNT se rebasa (QEMU lo calcula desde `time_base` con el reloj actual y saltaría) y se
reprograman los CCOMPARE pendientes.
