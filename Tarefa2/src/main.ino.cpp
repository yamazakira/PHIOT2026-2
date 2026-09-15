/**
 * IMD0904 - Plataforma de Hardware para Internet das Coisas
 * Atividade prática 2 - Medição de distância com HC-SR04
 *
 * Requisitos atendidos:
 *  - Mede a distância de um objeto à frente do sensor.
 *  - Permite ajustar a distância máxima de detecção (constante abaixo).
 *  - Acima da distância máxima, ou fora do alcance físico do sensor
 *    (2 a 400 cm, segundo o datasheet), retorna 0.
 *  - Imprime o resultado na serial no formato "Distância: 30 cm".
 *
 * Pinagem (conforme diagram.json):
 *  GPIO32 -> TRIG do HC-SR04 (saída, pulso de disparo)
 *  GPIO33 -> ECHO do HC-SR04, através do divisor de tensão 1,1k/2k
 *            (o ECHO sai em 5V; o divisor traz isso para ~3,2V,
 *             seguro para a entrada do ESP32)
 */

#include <Arduino.h>

const int TRIG_PIN = 32;
const int ECHO_PIN = 33;

/* Distância máxima de detecção, em cm. Ajuste esse valor conforme
 * a necessidade da aplicação (o sensor em si só enxerga até 400 cm). */
const unsigned int DISTANCIA_MAXIMA_CM = 100;

/* Limites físicos do HC-SR04, segundo o datasheet */
const unsigned int DISTANCIA_MINIMA_SENSOR_CM = 2;
const unsigned int DISTANCIA_MAXIMA_SENSOR_CM = 400;

/* Velocidade do som ~343 m/s = 0,0343 cm/us. Como o pulso do ECHO
 * mede o tempo de ida E volta, dividimos por 2. */
const float CM_POR_MICROSSEGUNDO = 0.0343 / 2.0;

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
}

/**
 * Dispara o pulso de trigger, mede o tempo de retorno do echo
 * e converte para centímetros. Retorna 0 se o objeto estiver
 * além da distância máxima configurada (ou fora do alcance do sensor).
 */
long medirDistanciaCm() {
  /* Pulso de disparo: LOW -> HIGH por 10us -> LOW.
   * É esse pulso que "pede" ao sensor para emitir o ultrassom. */
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  /* O menor valor entre a distância máxima configurada e o alcance
   * físico do sensor define até onde vale a pena esperar pelo echo. */
  unsigned int limiteDistanciaCm = min(DISTANCIA_MAXIMA_CM, DISTANCIA_MAXIMA_SENSOR_CM);

  /* Calcula um tempo limite de espera (timeout) coerente com essa
   * distância, para não travar o programa esperando um echo que
   * nunca vai voltar (objeto fora de alcance). Soma uma margem de
   * segurança de 2000us. */
  unsigned long timeoutUs = (unsigned long)(limiteDistanciaCm / CM_POR_MICROSSEGUNDO) + 2000;

  unsigned long duracaoUs = pulseIn(ECHO_PIN, HIGH, timeoutUs);

  /* pulseIn retorna 0 quando estoura o timeout, ou seja, quando
   * nenhum echo voltou dentro do tempo esperado -> fora de alcance. */
  if (duracaoUs == 0) {
    return 0;
  }

  long distanciaCm = (long)(duracaoUs * CM_POR_MICROSSEGUNDO);

  /* Fora da faixa desejada (maior que o máximo configurado, ou fora
   * do alcance físico real do sensor) também retorna 0. */
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

  /* Intervalo entre leituras: o HC-SR04 precisa de um tempo mínimo
   * (na prática, uns 60ms) entre um disparo e outro para o eco
   * anterior não interferir na próxima leitura. */
  delay(200);
}
