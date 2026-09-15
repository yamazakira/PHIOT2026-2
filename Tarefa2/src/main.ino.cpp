#include <Arduino.h>

const int TRIG_PIN = 32;
const int ECHO_PIN = 33;

const unsigned int DISTANCIA_MAXIMA_CM = 100;

// limites físicos do HC-SR04 segundo o datasheet 
const unsigned int DISTANCIA_MINIMA_SENSOR_CM = 2;
const unsigned int DISTANCIA_MAXIMA_SENSOR_CM = 400;

// Velocidade do som ~343 m/s = 0,0343 cm/us. Como o pulso do ECHO mede o tempo de ida E volta, dividimos por 2. 
const float CM_POR_MICROSSEGUNDO = 0.0343 / 2.0;

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
}

long medirDistanciaCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned int limiteDistanciaCm = min(DISTANCIA_MAXIMA_CM, DISTANCIA_MAXIMA_SENSOR_CM);

  // tempo limite de espera pra evitar lock esperando echo que nunca vai voltar
  unsigned long timeoutUs = (unsigned long)(limiteDistanciaCm / CM_POR_MICROSSEGUNDO) + 2000;

  unsigned long duracaoUs = pulseIn(ECHO_PIN, HIGH, timeoutUs);

  // pulseIn retorna 0 quando estoura o timeout, ou seja, quando nenhum echo voltou dentro do tempo esperado -> fora de alcance
  if (duracaoUs == 0) {
    return 0;
  }

  long distanciaCm = (long)(duracaoUs * CM_POR_MICROSSEGUNDO);

  // fora da faixa desejada (maior que o máximo configurado, ou fora do alcance físico real do sensor) também retorna 0
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

  // o HC-SR04 precisa de um tempo mínimo entre um disparo e outro para o eco anterior não interferir na próxima leitura
  delay(200);
}
