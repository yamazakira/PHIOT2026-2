#include <Arduino.h>

const int TRIG_PIN = 32;
const int ECHO_PIN = 33;

const unsigned int DISTANCIA_MAXIMA_CM = 100;
const unsigned int DISTANCIA_MINIMA_SENSOR_CM = 2;
const unsigned int DISTANCIA_MAXIMA_SENSOR_CM = 400;

// Velocidade do som
const float CM_POR_MICROSSEGUNDO = 0.0343 / 2.0;

volatile int64_t tempoSubida = 0;
volatile int64_t tempoDescida = 0;
volatile bool pulsoCompleto = false;
volatile bool aguardandoDescida = false;

// ISR chamada em qualquer mudança de nível do ECHO
void IRAM_ATTR isrEcho() {
  int64_t agora = esp_timer_get_time();

  if (digitalRead(ECHO_PIN) == HIGH) {
    // Borda de subida: início do pulso
    tempoSubida = agora;
    aguardandoDescida = true;
  } else if (aguardandoDescida) {
    // Borda de descida: fim do pulso
    tempoDescida = agora;
    aguardandoDescida = false;
    pulsoCompleto = true;
  }
}

unsigned long medirPulso(unsigned long timeoutUs) {
  // Reinicia o estado antes de disparar
  pulsoCompleto = false;
  aguardandoDescida = false;

  // Pulso de trigger de 10 us
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long inicio = micros();

  // Aguarda a ISR sinalizar o fim do pulso
  while (!pulsoCompleto) {
    if (micros() - inicio > timeoutUs) {
      return 0;
    }
  }

  return (unsigned long)(tempoDescida - tempoSubida);
}

long medirDistanciaCm() {
  unsigned int limiteCm = min(DISTANCIA_MAXIMA_CM, DISTANCIA_MAXIMA_SENSOR_CM);
  unsigned long timeoutUs = (unsigned long)(limiteCm / CM_POR_MICROSSEGUNDO) + 2000;

  unsigned long duracaoUs = medirPulso(timeoutUs);

  if (duracaoUs == 0) {
    return 0;
  }

  long distanciaCm = (long)(duracaoUs * CM_POR_MICROSSEGUNDO);

  if (distanciaCm > (long)DISTANCIA_MAXIMA_CM ||
      distanciaCm < (long)DISTANCIA_MINIMA_SENSOR_CM) {
    return 0;
  }

  return distanciaCm;
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), isrEcho, CHANGE);
}

void loop() {
  long distancia = medirDistanciaCm();

  Serial.print("Distância: ");
  Serial.print(distancia);
  Serial.println(" cm");

  delay(200);
}