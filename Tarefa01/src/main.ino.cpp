#include <Arduino.h>

// Pinos
const int LED1_PIN = 16;
const int LED2_PIN = 17;
const int BTN_PIN  = 26;
const int POT_PIN  = 32;
const int LDR_PIN  = 34;

// PWM
const int PWM_CHANNEL    = 0;
const int PWM_FREQ_HZ    = 5000;
const int PWM_RESOLUTION = 10; // 10 bits = duty de 0 a 1023

// Tempo
const unsigned long DEBOUNCE_MS        = 50; //debounce do botão
const unsigned long LED2_MIN_PERIOD_MS = 100;
const unsigned long LED2_MAX_PERIOD_MS = 1000;

// Tipos
enum Modo {
  MODO_MANUAL = 0, // Potenciometro
  MODO_AUTOMATICO // LDR
};

// Estado estado
Modo modoAtual = MODO_MANUAL; // começa no manual

int ultimoNivelBotao = HIGH;
unsigned long ultimoEventoBotaoMs = 0;

bool led2Estado = false;
unsigned long ultimoToggleLed2Ms = 0;

void setup() {
  Serial.begin(9600); 
  pinMode(LED2_PIN, OUTPUT);
  pinMode(BTN_PIN, INPUT_PULLUP);

  analogSetAttenuation(ADC_0db);
  analogReadResolution(12);

  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION);
  ledcAttachPin(LED1_PIN, PWM_CHANNEL);

  Serial.println("Iniciando em modo MANUAL");
}

void loop() {
  unsigned long agoraMs = millis();

  int nivelBotao = digitalRead(BTN_PIN);
  if (nivelBotao != ultimoNivelBotao &&
      (agoraMs - ultimoEventoBotaoMs) > DEBOUNCE_MS) {

    ultimoEventoBotaoMs = agoraMs;
    ultimoNivelBotao = nivelBotao;

    if (nivelBotao == LOW) {
      modoAtual = (modoAtual == MODO_MANUAL) ? MODO_AUTOMATICO : MODO_MANUAL;
      Serial.print("Modo alterado para: ");
      Serial.println(modoAtual == MODO_MANUAL ? "MANUAL" : "AUTOMATICO");
    }
  }

  int leituraAdc = (modoAtual == MODO_MANUAL) ? analogRead(POT_PIN)
    : analogRead(LDR_PIN);

  // LED1: PWM com duty proporcional à leitura
  int duty = map(leituraAdc, 0, 4095, 0, 1023);
  ledcWrite(PWM_CHANNEL, duty);

  // LED2: pisca com período entre 100 ms e 1000 ms
  unsigned long periodoMs = map(leituraAdc, 0, 4095, LED2_MAX_PERIOD_MS, LED2_MIN_PERIOD_MS);
  unsigned long meioPeriodoMs = periodoMs / 2;

  if ((agoraMs - ultimoToggleLed2Ms) >= meioPeriodoMs) {
    ultimoToggleLed2Ms = agoraMs;
    led2Estado = !led2Estado;
    digitalWrite(LED2_PIN, led2Estado);
  }
}
