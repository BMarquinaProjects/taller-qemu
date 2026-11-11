// Prueba de HX711 (v10): un HX711 con DOUT en GPIO16 y PD_SCK en GPIO17, con la librería HX711 de
// bogde (0.7.5). El script lo da de alta tras «HX711-LISTO», cambia la lectura tras «HX711-CAMBIO»
// y comprueba lo que se imprime. CPU0 está siempre ocupada: sin el cuanto corto mientras se mueve
// PD_SCK (M-157), su turno podría alargar un pulso de 1 µs más allá de 60 µs y apagar el chip.
#include "HX711.h"

HX711 balanza;

static void ocupar(void *) {
  volatile uint32_t x = 0;
  for (;;) x++;
}

void setup() {
  Serial.begin(115200);
  xTaskCreatePinnedToCore(ocupar, "ocupar", 2048, nullptr, 1, nullptr, 0);
  delay(200);
  Serial.println("HX711-LISTO");
  delay(500);
  balanza.begin(16, 17);  // DOUT, PD_SCK; A x128
  uint32_t t0 = millis();
  long a128 = balanza.read();  // 25 pulsos: la siguiente, A x128
  Serial.printf("ESPERA=%lu\n", (unsigned long)(millis() - t0));
  Serial.printf("A128=%ld\n", a128);
  balanza.set_gain(64);
  balanza.read();  // aún A x128; 27 pulsos piden A x64
  Serial.printf("A64=%ld\n", balanza.read());
  balanza.set_gain(32);
  balanza.read();  // aún A x64; 26 pulsos piden B x32
  Serial.printf("B32=%ld\n", balanza.read());
  balanza.set_gain(128);
  balanza.read();  // aún B x32; vuelve a A x128
  t0 = millis();
  balanza.tare(5);
  Serial.printf("TARA=%ld EN=%lu\n", balanza.get_offset(), (unsigned long)(millis() - t0));
  Serial.println("HX711-CAMBIO");
  delay(300);
  balanza.read();
  Serial.printf("NETO=%ld\n", (long)balanza.get_value(3));
  balanza.power_down();
  delay(5);
  balanza.power_up();
  t0 = millis();
  long tras = balanza.read();
  Serial.printf("DESPIERTA=%lu A128=%ld\n", (unsigned long)(millis() - t0), tras);
  Serial.println("TALLER-OK hx711");
}

void loop() {
  delay(1000);
}
