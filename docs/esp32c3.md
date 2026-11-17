# ESP32-C3: GPIO, LEDC, ADC1, I2C0, RMT TX, SPI2 y puente (fase 6d)

> Referencia de [taller-qemu](../README.md). Los demás documentos están en el [índice](../README.md#documentación).

Usar **qemu-system-riscv32 -machine esp32c3**. El mismo chardev
`taller-puente` activa HOLA y las tramas GPIO, SPI, PWM, I2C_ESCRITURA, RMT y TIEMPO
del protocolo **v8**. Bit n significa GPIO n también en el C3. ENTRADA,
ENTRADA_ARMADA, ANALOGICO, I2C_DISPOSITIVO e I2C_REGISTROS llegan a los modelos
de la máquina seleccionada mediante un receptor propio; las rutas del ESP32
clásico se conservan. El C3 admite solamente el bus I2C **0**.

El modelo C3 tiene **22 GPIO (0..21)**. OUT (`0x04`), ENABLE (`0x20`),
IN (`0x3c`) y sus W1TS/W1TC usan solamente esos 22 bits. `GPIO_PINn_REG`
está en **`0x74 + 4·n`**: conserva lo escrito y su bit 2, PAD_DRIVER,
activa drenador abierto. IN devuelve el nivel propio en salida push-pull,
el externo en entrada y el AND de ambos en salida de drenador abierto.
`FUNCn_OUT_SEL_CFG` se conserva en **`0x554 + 4·n`**, con señal software
128 al reset. Los offsets y PAD_DRIVER se contrastaron con `gpio_reg.h`,
`gpio_ll.h` y `gpio_sig_map.h` del core Arduino ESP32 3.3.12.

Los trenes reutilizan `taller_armado.h`: ocho ranuras, hasta 200 pasos,
temporizadores virtuales, DISPARO y reglas de rearme/reset del clásico.
Ambos pines del programa deben pertenecer a `0..21`; los demás se ignoran
antes de modificar las ranuras. ENTRADA también ignora pines ajenos al C3.

Inventario de la máquina base `esp-develop-9.2.2-20260417` y trabajo pendiente:

| Pieza | Estado en C3 |
|---|---|
| GPIO | Salida, entrada externa, drenador abierto y trenes conectados al puente. Sin IRQ GPIO ni filtros. |
| IO_MUX / matriz GPIO | IO_MUX conserva registros y extrae FUN_PU/FUN_PD (bits 8/7); las entradas sueltas siguen esos pulls. PINn y selección de salida conservan valores; LEDC y RMT resuelven el GPIO de su señal de salida, sin inversión. La entrada externa empieza alta hasta que el núcleo imponga o suelte cada pin. |
| Matriz IRQ | El original ya guarda `irq_levels`, aplica OR por línea y recalcula líneas vieja/nueva al reasignar. Conserva prioridad, umbral y habilitación; no tiene el fallo corregido del clásico. La selección de IRQ por flanco no está emulada. No se sustituye este archivo. |
| UART y temporizadores | Dos UART con FIFO/timeout/IRQ; dos TIMG con T0 y watchdog; SYSTIMER con dos contadores y tres alarmas. Se conservan los modelos de Espressif. |
| LEDC | `hw/misc/esp32c3_ledc.c`: cuatro timers y seis canales LS (registros de `soc/ledc_reg.h` del C3). Frecuencia = reloj (APB 80 MHz, RC_FAST 17,5 MHz o XTAL 40 MHz según `LEDC_CONF`) / (CLK_DIV en punto fijo 10.8 × 2^resolución); ciclo = DUTY (4 bits de fracción) / 2^resolución. Cada cambio efectivo manda PWM (tipo 4) al GPIO cuya `FUNCn_OUT_SEL_CFG` tiene la señal 45 + canal; cambiar de pin apaga el anterior. Sin fades, IRQ ni pulsos GPIO. |
| ADC | `hw/misc/esp32c3_saradc.c`: ADC1 de una lectura (`ONETIME_SAMPLE`, `SAR1DATA_STATUS`, `ADC1_DONE` en INT_RAW/ST/CLR y su IRQ). ANALOGICO con GPIO 0..4 fija el valor crudo de 12 bits del canal 0..4; otros GPIO se ignoran. Sin ADC2, DMA ni calibración: `analogRead` devuelve el valor inyectado. |
| I2C | `hw/i2c/esp32c3_i2c.c`: I2C0 en `0x60013000`, FIFO APB de 32 bytes por DATA, ocho comandos C3 (WRITE=1, STOP=2, READ=3, END=4, RSTART=6), `conf_upgate`, `fsm_rst`, NACK e IRQ COMPLETE/END/FIFO. END conserva la transferencia; RSTART vuelve a seleccionar el esclavo. El mismo `taller_i2c` del clásico ofrece el espejo del puente en bus 0 y emite cada escritura al STOP/RSTART. Sin modo esclavo, DMA, temporización eléctrica ni I2C por software. |
| RMT | `hw/misc/esp32c3_rmt.c`: dos TX en `0x60016000`, RAM desde `+0x400`, 48 palabras por bloque TX (canal 0 puede tomar ambos). `conf_update`, inicio, fin, umbral e IRQ; recarga ping-pong con contrapresión como en el clásico. Cada tren emite RMT (tipo 7) con su inicio, símbolos crudos y GPIO de las señales 51/52. Tick = (1 + DIV_NUM + DIV_A/DIV_B) × divisor del canal / reloj, usando APB 80 MHz, RC_FAST 17,5 MHz o XTAL 40 MHz; divisor de canal 0 significa 256. Sin RX, DMA, portadora ni modo continuo. |
| SPI | SPI1 de flash conservado. Modelo independiente `hw/ssi/esp32c3_spi2.c`: SPI2/GPSPI2 maestro en `0x60024000`, W0–W15 desde `+0x98`, `CMD.update/usr` autolimpiables y `MS_DLEN` de 18 bits. CPU y GDMA TX/RX, MISO FF; cada fin vacía MOSI hacia `taller.spi`, **bus 2** del puente. `DMA_INT_RAW` R/W, ENA, ST=RAW&ENA y CLR W1C; IRQ `ETS_SPI2_INTR_SOURCE`, fin normal bit 12 y fin de segmento bit 13. |

Las capacidades y señales se contrastaron con `soc_caps.h`, `adc_channel.h`
y `gpio_sig_map.h` del mismo core. Los registros I2C y RMT se contrastaron
con `soc/{i2c,rmt}_{reg,struct}.h` y `hal/{i2c,rmt}_ll.h` del C3, además
del [driver `i2c_master` de IDF 5.5](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_driver_i2c/i2c_master.c). El bus del puente no necesita conocer
los GPIO SDA/SCL: el controlador resuelve directamente la dirección I2C.

SPI se contrastó con `soc/spi_{reg,struct}.h`, `soc/gdma_reg.h` y
`hal/{spi,gdma}_ll.h` instalados en Arduino **3.3.12**, y con el
[HAL SPI C3 de IDF 5.5](https://github.com/espressif/esp-idf/blob/v5.5/components/hal/esp32c3/include/hal/spi_ll.h)
y el [driver spi_master](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_driver_spi/src/gpspi/spi_master.c).
Las señales de matriz son **FSPICLK=63** y **FSPID/MOSI=65**. Taller asigna
el bus de la pieza según sus pines SCK/MOSI: para una ST7735 en C3 debe
resultar **bus 2**, también al usar el objeto Arduino `SPI` (FSPI).
No se envían pines dentro de la trama tipo 3: sigue siendo `u8 bus` + MOSI.
CS/DC continúan llegando por GPIO; `taller_puente_spi_fin(2)` preserva el
orden entre transacciones y cambios de GPIO. El puente conserva su troceo
de hasta 4096 bytes MOSI por trama; los bloques largos no pierden bytes.

El modelo usa los tres canales del **GDMA existente**, seleccionando
periférico 0/SPI2 **y** START de la dirección correspondiente (OUT bit 21,
IN bit 22). Los descriptores de 12 bytes contienen size/length/owner/EOF,
puntero y siguiente; TX consume length, RX size y escribe length/EOF.
CHECK_OWNER y OUT_AUTO_WRBACK se respetan. Solo accede a SRAM
`0x3fc80000..0x3fcdffff`, con direcciones absolutas: la `dma_as` de
Espressif tiene como raíz el alias DRAM, que está en `0x3FC80000`, y QEMU
conserva ese desplazamiento. Cadenas inválidas o cíclicas terminan con error GDMA;
si MS_DLEN corta un descriptor TX, no se devuelve OWNER ni se genera
OUT_DONE/EOF antes de consumir todos sus bytes.
Los enlaces consumidos no se reutilizan sin otro START. Se corrigen stride
de IRQ C3 (0x10 por canal), traducción de sus cinco eventos IN/cuatro OUT
y pulsos STOP/reset. IN/OUT se unen con OR antes de la fuente compartida.
No se modifica el motor genérico GDMA. La matriz IRQ ya recalcula el nivel
al reasignar (E-039); desde v0.12.1 usa `BIT_ULL()` (ver [«Matriz de interrupciones»](esp32.md#matriz-de-interrupciones)). RAW y ENA de SPI también recalculan el nivel,
incluido el fin que IDF escribe antes de arrancar su primera ISR.
Los eventos RAW de fin normal y fin de segmento se acumulan hasta CLR o
escritura explícita de RAW; iniciar otra operación no descarta una IRQ pendiente.

**Límites SPI2:** ejecución síncrona, datos de bytes completos por una línea,
sin temporización eléctrica, arbitraje entre canales, PSRAM, modo esclavo,
DMA circular/restart ni interpretación de buffers de configuración SCT.
Se conservan los registros de modo/reloj; no se simulan flancos de SCK,
inversión de matriz, IO_MUX ni dummy clocks en las tramas. El estado de fin
de segmento tiene RAW/ENA/ST/CLR propio; no implica soporte del protocolo SCT.

## Pruebas C3

- `c3_pwm` (`pruebas/c3_pwm_prueba/`): `ledcAttach(8, 50, 14)`, 5 % → 25 % →
  `ledcChangeFrequency(8, 100, 14)` → 0 %. El verificador exige esa secuencia
  en GPIO8, en orden y con un 0,5 % de margen, tramas PWM válidas para el C3 y
  la línea única `TALLER-OK c3_pwm`.
- `c3_analogico` (`pruebas/c3_analogico_prueba/`): `analogRead(0)` cada 20 ms;
  el verificador inyecta ANALOGICO(0, 2048) y ANALOGICO(0, 4095) y exige las
  lecturas 0 → 1900..2200 → 4000..4095 y `TALLER-OK c3_analogico`.
- `c3_i2c` (`pruebas/c3_i2c_prueba/`): Wire hardware, SDA4/SCL5 a 100 kHz.
  El lanzador registra 0x3C en bus 0 e inyecta 0xAB en el registro 0xD0;
  exige lectura con RSTART, escritura `00 AF`, ACK=0, NACK=2 en 0x3D
  ausente y la línea única `TALLER-OK c3_i2c`. También comprueba las dos
  escrituras del puente, `D0` y `00 AF`, en ese orden; los sondeos no emiten.
- `c3_rmt` (`pruebas/c3_rmt_prueba/`): `rmtWrite` de Arduino, GPIO8, 10 MHz,
  un bloque de 48 palabras. Envía 8 LED RGB=(17,34,51) y 30 LED
  RGB=(68,85,102), ambos en GRB con bits MSB primero. Los dos trenes
  fuerzan recarga por IRQ; el verificador exige colores y símbolos exactos,
  tick de 100 ns, fin cero, 193/721 símbolos y `TALLER-OK c3_rmt`.
- `c3_spi` (`pruebas/c3_spi_prueba/`): SCK4/MOSI6/MISO5, 20 MHz.
  `SPI.beginTransaction`/`transfer`/`writeBytes` emiten `abcd` por CPU.
  Después libera `SPI` y usa `spi_master` con `SPI_DMA_CH_AUTO` e ISR:
  patrón `00..FF` repetido 160 veces, **40960 bytes**, en dos transacciones
  de **32768+8192** (límite de MS_DLEN). Una lectura GDMA de cuatro bytes
  añade cuatro ceros MOSI y exige `C3-SPI-MISO=ffffffff` antes de la línea
  única `TALLER-OK c3_spi`. El verificador puro compara los **40968 bytes
  exactos en orden**, exige bus 2, HOLA v8 y tiempos no decrecientes.
  Arduino `writeBytes` usa bloques CPU de hasta 64 bytes incluso con un
  buffer grande; la llamada explícita a IDF garantiza que se ejercita GDMA.
- Las unitarias `c3_*_unitarias.py` prueban los verificadores y los lanzadores
  sin QEMU; `c3_i2c_modelo_unitarias.py` y `c3_rmt_modelo_unitarias.py`
  compilan los modelos reales mediante `c3_modelos.py`. I2C cubre NACK,
  RSTART, comandos mayores que FIFO, lotes END de 70 bytes, IRQ y reset;
  RMT cubre dos TX, relojes/divisores, recarga antes y después de limpiar
  el umbral, lectura diferida, parada y reset. El adaptador admite GCC/Clang
  o MSVC (`cl` desde una consola de desarrollo); sin compilador se omiten
  las pruebas C. LEDC y ADC1 reutilizan el mismo adaptador.
  `c3_spi_unitarias.py` ejecuta SPI2 real y emisor real del puente en bus 2:
  CPU de 64 bytes, dos bloques DMA, TX/RX en canales 0/1/2, cadenas,
  highpart, errores, ciclos, RAW/ENA/CLR, RX sin outlink y reset. También
  cubre descriptores y buffers fuera de SRAM o cruzando su límite, OWNER
  con y sin comprobación, canales ajenos/parados/en reset, RX/TX parciales,
  OUT_AUTO_WRBACK desactivado y fin de segmento pendiente entre operaciones.
  Prueba el lanzador por sockets y rechaza tiempos decrecientes con MOSI correcto.
  `c3_gdma_unitarias.py` compila la traducción
  real C3 y comprueba los nueve eventos y los registros de los tres canales.

Las imágenes se compilan como la de GPIO (abajo), cambiando el nombre del sketch.


`pruebas/c3_gpio_prueba/c3_gpio_prueba.ino` produce tres pulsos completos en
GPIO8, lee GPIO9 con INPUT_PULLUP, espera ENTRADA(9, 0), vuelve a leer y termina
con `TALLER-OK c3`. El verificador exige HOLA v8, niveles/salidas limitados a
22 bits, tres pulsos habilitados, tiempos no decrecientes y las líneas únicas
`C3-ANTES=1`, `C3-DESPUES=0`, `TALLER-OK c3`, en ese orden.

Imagen completa de **4 MiB**, bootloader en **0x0**, particiones en `0x8000`,
boot_app0 en `0xe000` y aplicación en `0x10000`. Se compiló con CLI 1.5.1 y
core 3.3.12, DIO/40 MHz y USB CDC desactivado para usar UART0:

```powershell
$cli = "$env:APPDATA\dev.taller.app\arduino-cli\1.5.1\arduino-cli.exe"
$config = "$env:APPDATA\dev.taller.app\arduino\arduino-cli.yaml"
& $cli --config-file $config board listall esp32c3
& $cli --config-file $config compile `
  --fqbn 'esp32:esp32:esp32c3:FlashMode=dio,FlashFreq=40,CDCOnBoot=default' `
  --build-path "$PWD\pruebas\c3_gpio_prueba\build" pruebas/c3_gpio_prueba
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación C3' }
Copy-Item pruebas/c3_gpio_prueba/build/c3_gpio_prueba.ino.merged.bin pruebas/c3_gpio_prueba.flash.bin
```

SHA-256 de la imagen incluida:
`71a31f166b7f8aff0027f8cf6080a7c72bae5ad6181bd21103ddca4df75150b2`.

Las nuevas imágenes fusionadas, también de **4 MiB**, se compilaron con
la misma CLI, core y FQBN:

| Imagen | SHA-256 |
|---|---|
| `c3_i2c_prueba.flash.bin` | `9770b2a1d1cc3c6c1e09771065b9fafde067160cab69d406360a4161426ea557` |
| `c3_rmt_prueba.flash.bin` | `249ca0475eb198017d8f8f6541b2d33dc334cd3c804431c6ec46dcf4553c9bfd` |
| `c3_spi_prueba.flash.bin` | `c985edee5b21f4f7f8777070ea820a3abfbf59e833481481047252242dcbacef` |

El lanzador usa una copia temporal de la flash. No carga la eFuse ECO3 del
clásico: el modelo C3 crea su eFuse en RAM con revisión **v0.3** y versión
de bloque **1.3** si no recibe fichero. Desactiva los watchdog de TIMG con
`timer.esp32c3.timg.wdt_disable=true` y conserva `-icount shift=2,align=off,sleep=on`.
La instalación debe incluir `share/esp32c3-rom.bin` junto al resto de ROM de QEMU.

```sh
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
python3 pruebas/qemu_prueba.py /ruta/qemu-system-riscv32 c3_gpio pruebas/c3_gpio_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-riscv32 c3_i2c pruebas/c3_i2c_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-riscv32 c3_rmt pruebas/c3_rmt_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-riscv32 c3_spi pruebas/c3_spi_prueba.flash.bin
bash pruebas/todas.sh /ruta/qemu-system-xtensa /ruta/qemu-system-riscv32
```

`todas.sh` ejecuta las pruebas clásicas, incluida `fpu`, los seis modos C3 (`c3_gpio`, `c3_pwm`,
`c3_analogico`, `c3_i2c`, `c3_rmt` y `c3_spi`) y los modos S3 (`s3_gpio`, `s3_pwm`,
`s3_analogico`, `s3_i2c`, `s3_rmt`, `s3_flash_qio`, `s3_spi` y `s3_fpu`).
Sin segundo argumento, busca `qemu-system-riscv32` junto al Xtensa; también
admite `QEMU_C3=/ruta/binario`. La omisión opcional de MicroPython sigue igual.
Las unitarias ejecutan el GPIO C3 real aislado, sus trenes/reset, el parser y
los receptores reales de ambas máquinas, además de comprobar el verificador.
En el paso I2C/RMT se compilaron los dos sketches y se ejecutaron **73 unitarias**:
sin fallos, ocho omitidas por requerir GCC/Clang. Los cuatro modelos C3
(LEDC, ADC1, I2C y RMT) se compilaron y ejecutaron aislados con MSVC C11;
también se compilaron y ejecutaron el GPIO y el parser/receptores reales,
incluido el despacho I2C del C3. `bash -n pruebas/todas.sh` pasó. WSL devolvió
`E_ACCESSDENIED` al intentar ejecutar las pruebas; se usó el compilador nativo.
**Pendientes compilación/enlace de QEMU y ejecución de `todas.sh` en WSL con
los binarios recompilados.** Las pruebas aisladas no certifican arranque,
registro QOM ni comportamiento temporal integrado; tampoco se ejecutó aquí
MicroPython. En el paso SPI2 se compilaron el sketch y su imagen de 4 MiB
con la CLI de Taller 1.5.1 y el FQBN DIO/40 MHz/CDC desactivado de arriba.
En la continuación del WIP se ejecutaron **101 unitarias en WSL, sin fallos
ni omisiones**, incluidos los modelos reales y las regresiones del SPI clásico.
Con MSVC pasan las mismas 101, con 11 omisiones de pruebas que requieren GCC/Clang.
WSL sí permitió esta vez compilar aisladamente `esp32c3_spi2.c`,
`esp32c3_gdma.c` y `esp32c3.c` con las cabeceras y opciones reales del árbol
base y `-Werror`; también pasó `bash -n pruebas/todas.sh`.
Se recompiló el sketch con el FQBN indicado: imagen idéntica a la incluida,
componentes/offsets y SHA-256 comprobados, checksum y hash de aplicación válidos
según esptool. La revisión independiente detectó dos fallos, corregidos con
regresiones que fallaron antes del cambio: OUT_DONE prematuro con TX parcial
y pérdida de SEG_DONE pendiente al iniciar una operación normal.
En QEMU real, la prueba `c3_spi` destapó un tercer fallo: el modelo restaba
`0x3FC80000` antes de leer la `dma_as`, pero sus direcciones son absolutas
(el alias DRAM está colocado en `0x3FC80000` y QEMU conserva ese
desplazamiento), así que todo descriptor salía ilegible. Corregido el modelo
y el doble de las unitarias; `todas.sh` pasa sus 21 modos. **v0.12.0**,
protocolo **v7**.
