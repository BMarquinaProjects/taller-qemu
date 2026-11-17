# FPU tras el reset en ESP32 y ESP32-S3 (M-160)

> Referencia de [taller-qemu](../README.md). Los demás documentos están en el [índice](../README.md#documentación).

Las capas `hw/xtensa/esp32.c` y `hw/xtensa/esp32s3.c` fijan **CPENABLE=0xff**
después de cada reset de CPU, tanto PRO como APP y en el arranque directo.
El auxiliar local `taller_cpu_reset` reproduce el valor del silicio que espera
Rust `no_std` con esp-hal **1.2.2** / xtensa-lx-rt **0.23.0**. Con cero, la
primera instrucción FPU provoca una excepción de coprocesador y el guardado
de registros FP del manejador termina en doble excepción. El cambio queda
limitado a estas dos máquinas, sin modificar `target/xtensa/cpu.c`.

En el S3, `esp32s3_io_read` devuelve **BBPLL_CAL_DONE (bit 24)** solo al leer
`I2C_MST_ANA_CONF0`, **0x6000E040**: permite terminar la espera de calibración
de `esp_hal::init`. Los demás registros sin modelo siguen devolviendo cero,
incluido el bit de ocupado que consultan los `rom_i2c_*`.

Los modos `fpu` (ESP32) y `s3_fpu` usan las imágenes ya compiladas
`pruebas/fpu_prueba.flash.bin` y `pruebas/s3_fpu_prueba.flash.bin`. Comparten
la fuente `pruebas/fpu_prueba/src/main.rs`; sus proyectos están en
`pruebas/fpu_prueba/` y `pruebas/s3_fpu_prueba/`. Exigen estas dos líneas
UART0 completas, únicas y ordenadas en un plazo total de **30 s**:

```text
TALLER fpu cpenable=0xff
TALLER-OK fpu 105 -21 3
```

```sh
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa fpu pruebas/fpu_prueba.flash.bin
python3 pruebas/qemu_prueba.py /ruta/qemu-system-xtensa s3_fpu pruebas/s3_fpu_prueba.flash.bin
```

`todas.sh` añade `fpu` tras `rmt` y `s3_fpu` tras `s3_spi`.
`pruebas/fpu_unitarias.py` comprueba el verificador y ambos modos sin QEMU:
CPENABLE, resultados exactos, líneas ausentes/duplicadas/invertidas,
fragmentación UART, errores y plazo compartido. La compilación de QEMU,
la ejecución integrada de `todas.sh` en Linux y la prueba del binario Windows
quedan para el revisor; estas unitarias no certifican el arranque real.
