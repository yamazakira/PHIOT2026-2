#include <Arduino.h>

const int TRIG_PIN = 32;
const int ECHO_PIN = 33;

const unsigned int DISTANCIA_MAXIMA_CM = 100;

const unsigned int DISTANCIA_MINIMA_SENSOR_CM = 2;
const unsigned int DISTANCIA_MAXIMA_SENSOR_CM = 400;

const float CM_POR_MICROSSEGUNDO = 0.0343 / 2.0;

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
}

unsigned long medirPulso(int pino, int estado, unsigned long timeout) {
  unsigned long inicioTimeout = micros();
    
    while (digitalRead(pino) == estado) {
        if (micros() - inicioTimeout > timeout) return 0;
    }
    
    while (digitalRead(pino) != estado) {
        if (micros() - inicioTimeout > timeout) return 0;
    }
    
    unsigned long horaInicioPulso = micros();
    
    while (digitalRead(pino) == estado) {
        if (micros() - inicioTimeout > timeout) return 0;
    }
    
    return micros() - horaInicioPulso;
}

long medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned int limiteDistanciaCm = min(DISTANCIA_MAXIMA_CM, DISTANCIA_MAXIMA_SENSOR_CM);

  unsigned long timeoutUs = (unsigned long)(limiteDistanciaCm / CM_POR_MICROSSEGUNDO) + 2000;

  unsigned long duracaoUs = medirPulso(ECHO_PIN, HIGH, timeoutUs);

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

void loop() {
  long distancia = medirDistanciaCm();

  Serial.print("Distância: ");
  Serial.print(distancia);
  Serial.println(" cm");

  delay(200);
}
