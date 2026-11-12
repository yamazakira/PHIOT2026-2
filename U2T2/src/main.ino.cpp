/*
 * IMD0904 - Plataforma de Hardware para IoT - Aula 15
 * Projeto IoT: ESP32 + DHT11 + HC-SR501 + MQTT (PubSubClient) + Node-RED
 *
 * O ESP32 apenas:
 *   1) lê os sensores e guarda os valores em variáveis temporárias;
 *   2) publica os valores via MQTT para o Node-RED;
 *   3) recebe do Node-RED o estado de cada alerta e acende/apaga o LED.
 * A LÓGICA DOS ALERTAS (presença, T < 20 °C, T > 25 °C, UR > 50 %) FICA NO NODE-RED.
 *
 * Bibliotecas (Arduino IDE > Gerenciador de Bibliotecas):
 *   - PubSubClient (Nick O'Leary)
 *   - DHT sensor library (Adafruit) + Adafruit Unified Sensor
 *
 * ---------------------------------------------------------------------------
 * LIGAÇÕES (iguais ao diagram.json – protoboard de 30 colunas)
 *   Trilhos:  + superior = 3V3   | - superior = GND
 *             + inferior = VIN(5V) | - inferior = GND
 *   DHT11  : VCC col3, DATA col4, NC col5, GND col6 (linha a)
 *            10k entre col4 e trilho 3V3 (pull-up) | DATA -> GPIO 4
 *   HC-SR501: VCC col11 (-> 5V), OUT col12 (-> GPIO 27), GND col13
 *   LEDs (anodo na coluna N, catodo na N+1, 220R do catodo ao GND):
 *     Presença     (amarelo)  col 16/17 -> GPIO 25
 *     Temp. alta   (vermelho) col 20/21 -> GPIO 26
 *     Temp. baixa  (azul)     col 24/25 -> GPIO 33
 *     Umidade      (verde)    col 28/29 -> GPIO 32
 * ---------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ======================= SIMULAÇÃO x CIRCUITO REAL ==========================
// 1 = Wokwi (o Wokwi só tem o DHT22 e usa a rede "Wokwi-GUEST")
// 0 = circuito real com DHT11  -> MUDE PARA 0 ANTES DE GRAVAR NA PLACA
#define SIMULACAO_WOKWI 1

// ============================== CONFIGURAÇÕES ===============================
#if SIMULACAO_WOKWI
const char* ssid     = "Wokwi-GUEST";       // Nome do roteador Wi-Fi
const char* password = "";                  // Senha do roteador Wi-Fi
#define DHT_TIPO DHT22
#else
const char* ssid     = "NOME_ROTEADOR";     // Nome do roteador Wi-Fi
const char* password = "SENHA_ROTEADOR";    // Senha do roteador Wi-Fi
#define DHT_TIPO DHT11
#endif

// Broker MQTT (Mosquitto online para testes; troque pelos dados do professor)
const char* mqtt_server = "broker.emqx.io";  // END_IP_BROKER
const int   mqtt_port   = 1883;                  // PORTA_BROKER
const char* un          = "";                    // USUARIO_BROKER (vazio = sem autenticação)
const char* pw          = "";                    // SENHA_BROKER
const char* id          = "ESP32_U2T2_AKIRA";    // ID_ESP32 (deve ser único no broker)

// Tópicos (prefixo próprio para não misturar com outros alunos no broker público)
#define BASE "/ESP32_U2T2_AKIRA"
// Publicação (ESP32 -> Node-RED)
const char* TOP_TEMP     = BASE "/temperatura";   // °C
const char* TOP_UMID     = BASE "/umidade";       // %
const char* TOP_PRESENCA = BASE "/presenca";      // "1" ou "0"
const char* TOP_STATUS   = BASE "/status";        // "online"
// Assinatura (Node-RED -> ESP32), payload "1" acende / "0" apaga
const char* TOP_LED_PRESENCA   = BASE "/LED/presenca";
const char* TOP_LED_TEMP_ALTA  = BASE "/LED/temp_alta";
const char* TOP_LED_TEMP_BAIXA = BASE "/LED/temp_baixa";
const char* TOP_LED_UMIDADE    = BASE "/LED/umidade";

// ================================== PINOS ===================================
const uint8_t PINO_DHT = 4;
const uint8_t PINO_PIR = 27;
const uint8_t LED_PRESENCA   = 25;
const uint8_t LED_TEMP_ALTA  = 26;
const uint8_t LED_TEMP_BAIXA = 33;
const uint8_t LED_UMIDADE    = 32;

// ========================= VARIÁVEIS TEMPORÁRIAS ============================
float temperatura = NAN;
float umidade     = NAN;
int   presenca    = LOW;
int   presencaPublicada = -1;            // força a 1ª publicação

const unsigned long INTERVALO_DHT = 2000;    // DHT11: no máx. 1 leitura/s
unsigned long ultimaLeituraDHT = 0;
unsigned long inicioTentativa  = 0;          // para reconectar a cada 5 s
bool mqttConectado = false;

WiFiClient espClient;                // Cria o cliente WiFi
PubSubClient client(espClient);      // Cliente MQTT usando o cliente WiFi
DHT dht(PINO_DHT, DHT_TIPO);

// ====================== CALLBACK: mensagens recebidas =======================
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensagem recebida [");
  Serial.print(topic);
  Serial.print("]: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (length == 0) return;
  uint8_t state = ((char)payload[0] == '1') ? HIGH : LOW;

  if      (strcmp(topic, TOP_LED_PRESENCA)   == 0) digitalWrite(LED_PRESENCA,   state);
  else if (strcmp(topic, TOP_LED_TEMP_ALTA)  == 0) digitalWrite(LED_TEMP_ALTA,  state);
  else if (strcmp(topic, TOP_LED_TEMP_BAIXA) == 0) digitalWrite(LED_TEMP_BAIXA, state);
  else if (strcmp(topic, TOP_LED_UMIDADE)    == 0) digitalWrite(LED_UMIDADE,    state);
}

// ================================ Wi-Fi =====================================
void conectaWiFi() {
  Serial.print("Conectando ao Wi-Fi ");
  Serial.print(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi conectado. IP: ");
  Serial.println(WiFi.localIP());
}

// ============================ MQTT (não bloqueante) =========================
void conectaMQTT() {
  if (client.connected()) return;
  if (inicioTentativa != 0 && millis() - inicioTentativa < 5000) return;

  Serial.print("Conectando ao broker MQTT... ");
  if (strlen(un) > 0) mqttConectado = client.connect(id, un, pw);
  else                mqttConectado = client.connect(id);

  if (mqttConectado) {
    Serial.println("conectado");
    client.subscribe(BASE "/LED/#");          // os 4 tópicos dos LEDs
    client.publish(TOP_STATUS, "online");
    presencaPublicada = -1;                   // republica estado atual
    inicioTentativa = 0;
  } else {
    Serial.print("falha, rc=");
    Serial.print(client.state());
    Serial.println(" tentando novamente em 5 segundos");
    inicioTentativa = millis();
  }
}

// ============================= LEITURA SENSORES =============================
void leSensores() {
  // PIR: leitura contínua (saída digital 3,3 V)
  presenca = digitalRead(PINO_PIR);

  // DHT: a cada INTERVALO_DHT
  if (millis() - ultimaLeituraDHT >= INTERVALO_DHT) {
    ultimaLeituraDHT = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (isnan(t) || isnan(h)) {
      Serial.println("Falha na leitura do DHT!");
      return;
    }
    temperatura = t;
    umidade = h;

    Serial.printf("T = %.1f C | UR = %.1f %% | Presenca = %d\n",
                  temperatura, umidade, presenca);

    if (client.connected()) {
      char buf[16];
      dtostrf(temperatura, 0, 1, buf);
      client.publish(TOP_TEMP, buf);
      dtostrf(umidade, 0, 1, buf);
      client.publish(TOP_UMID, buf);
    }
  }
}

void publicaPresenca() {
  // Publica imediatamente quando muda (e a cada reconexão)
  if (client.connected() && presenca != presencaPublicada) {
    client.publish(TOP_PRESENCA, presenca ? "1" : "0");
    presencaPublicada = presenca;
    Serial.printf("Presenca -> %d\n", presenca);
  }
}

// ================================ SETUP/LOOP ================================
void setup() {
  Serial.begin(115200);

  pinMode(PINO_PIR, INPUT);
  pinMode(LED_PRESENCA, OUTPUT);
  pinMode(LED_TEMP_ALTA, OUTPUT);
  pinMode(LED_TEMP_BAIXA, OUTPUT);
  pinMode(LED_UMIDADE, OUTPUT);
  digitalWrite(LED_PRESENCA, LOW);
  digitalWrite(LED_TEMP_ALTA, LOW);
  digitalWrite(LED_TEMP_BAIXA, LOW);
  digitalWrite(LED_UMIDADE, LOW);

  dht.begin();
  conectaWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) conectaWiFi();
  conectaMQTT();
  client.loop();          // processa mensagens recebidas (callback)

  leSensores();
  publicaPresenca();
}