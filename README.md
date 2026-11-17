# taller-qemu

> **In English:** an ESP32, ESP32-C3 and ESP32-S3 emulator built on Espressif's QEMU. It runs unmodified firmware and reports every pin and bus event over a socket, so the firmware can talk to simulated components. The docs are in Spanish.

[![Compilación y pruebas](https://github.com/BMarquinaProjects/taller-qemu/actions/workflows/qemu.yml/badge.svg)](https://github.com/BMarquinaProjects/taller-qemu/actions/workflows/qemu.yml)
[![Última versión](https://img.shields.io/github/v/release/BMarquinaProjects/taller-qemu?label=versi%C3%B3n)](https://github.com/BMarquinaProjects/taller-qemu/releases/latest)

Emulador de los microcontroladores **ESP32, ESP32-C3 y ESP32-S3**. Ejecuta la misma imagen de firmware que se graba en la placa, sin modificarla y sin tener la placa.

Es el [QEMU de Espressif](https://github.com/espressif/qemu) con los periféricos que le faltaban para que un programa normal funcione: pines, PWM, entradas analógicas, I2C, SPI y los pulsos de las tiras de LED. Está probado con programas de Arduino, MicroPython y Rust.

## Para qué sirve

Para probar firmware sin tener los componentes en la mesa.

Cada vez que el programa mueve un pin o escribe en un bus, el emulador lo envía por un socket, con la hora del reloj simulado. Por el mismo socket recibe lo que contestaría el circuito: un botón pulsado, una tensión, la lectura de un sensor. Así, otro programa puede hacer de pantalla, de tira de LED o de sensor de temperatura.

Ese otro programa es **Taller**, un simulador de placas y componentes; este repositorio es su emulador. Si no se abre el socket, funciona como un QEMU normal.

Lo empecé para aprender cómo funcionan algunos componentes y módulos sin tenerlos físicamente. Con el tiempo se ha convertido en el entorno donde pruebo un diseño antes de montarlo.

## Qué emula

| | ESP32 | ESP32-C3 | ESP32-S3 |
|---|---|---|---|
| Procesador | Xtensa, dos núcleos | RISC-V, un núcleo | Xtensa, dos núcleos |
| Ejecutable | `qemu-system-xtensa` | `qemu-system-riscv32` | `qemu-system-xtensa` |
| Máquina (`-machine`) | `esp32` | `esp32c3` | `esp32s3` |
| Pines (GPIO) | 0 a 39 | 0 a 21 | 0 a 21 y 26 a 48 |
| PWM (LEDC) | 16 canales | 6 canales | 8 canales |
| Entradas analógicas (ADC1) | 8 | 5 | 10 |
| I2C | 2 buses | 1 bus | 1 bus |
| SPI | 2 buses, con DMA | 1 bus, con DMA | 1 bus, con DMA |
| Pulsos RMT (tiras de LED) | 8 canales | 2 canales | 4 canales |

Además, en los tres chips:

- **Resistencias de pull-up y pull-down**, y pines que se quedan sueltos.
- **Respuestas con tiempos de microsegundos**: trenes de pulsos que se disparan con un flanco del firmware, para sensores como el DHT22.
- **Dos sensores completos dentro del emulador**, porque contestan demasiado deprisa para preguntar fuera: el DS18B20 (1-Wire) y el HX711 (células de carga).

No simula la parte eléctrica: ni tensiones ni tiempos de flanco. Los límites de cada modelo están en su documento.

## Descargar

Cada versión publica dos paquetes ya compilados en [Releases](https://github.com/BMarquinaProjects/taller-qemu/releases/latest):

| Sistema | Archivo |
|---|---|
| Linux (x86-64) | `qemu-taller-<versión>-x86_64-linux-gnu.tar.xz` |
| Windows (x86-64) | `qemu-taller-<versión>-x86_64-w64-mingw32.tar.xz` |

Dentro hay una carpeta `qemu/`, con los ejecutables en `qemu/bin/` y las ROM de los chips en `qemu/share/`. El de Linux se compila en Ubuntu 24.04.

## Compilar

En Linux o WSL (Ubuntu 24.04):

```sh
sudo apt install build-essential ninja-build pkg-config python3-venv flex bison \
  libglib2.0-dev libpixman-1-dev libslirp-dev libgcrypt20-dev
./construir.sh
```

`construir.sh` descarga el código de Espressif y comprueba su SHA-256, copia encima los archivos de `capas/`, compila `qemu-system-xtensa` y `qemu-system-riscv32` y deja el paquete en `dist/`. La segunda vez solo recompila lo que ha cambiado. Los ejecutables quedan en `.trabajo/instalar-x86_64-linux-gnu/qemu/bin/`.

- **En WSL** conviene que la carpeta de trabajo esté dentro de Linux: `TRABAJO=~/qemu-trabajo ./construir.sh`. Por defecto es `.trabajo/`.
- **Con el código ya descargado:** `TARBALL=/ruta/src.tar.xz ./construir.sh`.
- **En Windows** se compila igual desde MSYS2 **UCRT64** (MINGW64 ya no trae `libslirp`), con los paquetes `mingw-w64-ucrt-x86_64-{gcc,glib2,pixman,libslirp,libgcrypt,ninja,meson,python,pkgconf}`. La lista completa está en [`.github/workflows/qemu.yml`](.github/workflows/qemu.yml).
- **Si la carpeta de trabajo viene de una versión que solo compilaba Xtensa**, se vuelve a configurar sola para añadir RISC-V.

## Usar

La forma más corta de verlo funcionar es una de las pruebas, que trae su programa de Arduino ya compilado:

```sh
python3 pruebas/qemu_prueba.py /ruta/qemu-system-riscv32 c3_gpio pruebas/c3_gpio_prueba.flash.bin
```

Arranca un ESP32-C3 cuyo programa da tres pulsos en GPIO8 y lee GPIO9. La prueba baja GPIO9 desde fuera y comprueba que el programa pasa a leer un 0.

Por debajo lanza QEMU así, con dos puertos libres:

```sh
qemu-system-riscv32 -machine esp32c3 -display none -monitor none \
  -drive file=firmware.flash.bin,if=mtd,format=raw \
  -global driver=timer.esp32c3.timg,property=wdt_disable,value=true \
  -chardev socket,id=taller-puente,host=127.0.0.1,port=5555,server=on,wait=on \
  -serial tcp:127.0.0.1:5556,server=on,wait=on \
  -icount shift=2,align=off,sleep=on
```

- `firmware.flash.bin` es la imagen completa de la flash. En Arduino es el archivo `.merged.bin`.
- QEMU espera a que un programa se conecte a los dos puertos: por el 5556 sale el puerto serie del firmware y por el 5555 van las tramas del puente.
- `wdt_disable` apaga el watchdog de los temporizadores, e `-icount` fija el reloj simulado con el que se han medido las pruebas.
- Para el ESP32-S3 cambian el ejecutable y la máquina: `qemu-system-xtensa -machine esp32s3` y `timer.esp32s3.timg`. El ESP32 clásico necesita además un archivo de eFuse: está en `lanzar()`, en [`pruebas/qemu_prueba.py`](pruebas/qemu_prueba.py).

El formato de las tramas está en [`docs/puente.md`](docs/puente.md).

## Probar

```sh
python3 -m unittest discover -s pruebas -p '*_unitarias.py'
bash pruebas/todas.sh /ruta/qemu-system-xtensa /ruta/qemu-system-riscv32
```

- **Las unitarias** no necesitan QEMU: compilan cada modelo en C por separado y lo ejercitan. Necesitan `cc`, `gcc`, `clang` o `cl`; sin compilador, esas se omiten.
- **`todas.sh`** arranca en QEMU más de treinta programas de prueba en los tres chips, y comprueba su salida y las tramas del puente.
- **La prueba con MicroPython** es opcional, porque su firmware no se distribuye aquí: [cómo prepararla](docs/esp32.md#prueba-micropython).

Cada cambio se compila y se prueba en Linux y en Windows con [GitHub Actions](.github/workflows/qemu.yml).

## Documentación

La referencia de registros, el protocolo y el detalle de cada prueba están en [`docs/`](docs/):

| Documento | Contenido |
|---|---|
| [`puente.md`](docs/puente.md) | El protocolo del puente: tramas, versiones, entradas temporizadas, 1-Wire y HX711 |
| [`esp32.md`](docs/esp32.md) | ESP32 clásico: SPI con DMA, matriz de interrupciones, drenador abierto, trenes armados y RMT |
| [`esp32c3.md`](docs/esp32c3.md) | ESP32-C3: registros, límites y pruebas de cada periférico |
| [`esp32s3.md`](docs/esp32s3.md) | ESP32-S3: registros, límites, pruebas, flash en QIO y reloj de la CPU |
| [`esp32s3-i2c-rmt.md`](docs/esp32s3-i2c-rmt.md) | ESP32-S3: contraste de I2C0 y RMT con las cabeceras de Arduino 3.3.12 |
| [`esp32s3-spi.md`](docs/esp32s3-spi.md) | ESP32-S3: SPI2 y GDMA |
| [`m157-armado.md`](docs/m157-armado.md) | Por qué fallaban a ratos los trenes de pulsos con dos núcleos, y el arreglo |
| [`m160-fpu.md`](docs/m160-fpu.md) | La FPU tras el reset en el ESP32 y el ESP32-S3 |
| [`m239-pull.md`](docs/m239-pull.md) | Pull-up, pull-down y contactos pasivos |

Los códigos como `M-157` o `E-054` son entradas de la bitácora de Taller.

## Origen y licencia

Deriva del [QEMU de Espressif](https://github.com/espressif/qemu) (etiqueta `esp-develop-9.2.2-20260417`), que a su vez deriva de [QEMU](https://www.qemu.org). Los archivos de `capas/` sustituyen a los del código base al compilar; [`AVISO.md`](AVISO.md) lista cada cambio.

Licencia: GNU GPL versión 2, la misma que QEMU. Ver [`LICENSE`](LICENSE).
