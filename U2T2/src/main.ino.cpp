#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Modo de execução: 1 para simulação no Wokwi, 0 para o circuito real
#define SIMULACAO_WOKWI 1

// Credenciais Wi-Fi e modelo do sensor conforme o modo de execução
#if SIMULACAO_WOKWI
const char* ssid     = "Wokwi-GUEST";
const char* password = "";
#define DHT_MODELO 22
#else
const char* ssid     = "NOME_ROTEADOR";
const char* password = "SENHA_ROTEADOR";
#define DHT_MODELO 11
#endif

// Configurações do broker MQTT
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;
const char* un          = "";
const char* pw          = "";
const char* id          = "ESP32_U2T2_AKIRA";

// Tópicos MQTT de publicação (sensores) e assinatura (LEDs)
#define BASE "/ESP32_U2T2_AKIRA"
const char* TOP_TEMP     = BASE "/temperatura";
const char* TOP_UMID     = BASE "/umidade";
const char* TOP_PRESENCA = BASE "/presenca";
const char* TOP_STATUS   = BASE "/status";
const char* TOP_LED_PRESENCA   = BASE "/LED/presenca";
const char* TOP_LED_TEMP_ALTA  = BASE "/LED/temp_alta";
const char* TOP_LED_TEMP_BAIXA = BASE "/LED/temp_baixa";
const char* TOP_LED_UMIDADE    = BASE "/LED/umidade";

// Pinos
const uint8_t PINO_DHT = 4;
const uint8_t PINO_PIR = 27;
const uint8_t LED_PRESENCA   = 25;
const uint8_t LED_TEMP_ALTA  = 26;
const uint8_t LED_TEMP_BAIXA = 33;
const uint8_t LED_UMIDADE    = 32;

// Variáveis temporárias dos sensores e controle de tempo
float temperatura = NAN;
float umidade     = NAN;
int   presenca    = LOW;
int   presencaPublicada = -1;

const unsigned long INTERVALO_DHT = 2000;
unsigned long ultimaLeituraDHT = 0;
unsigned long inicioTentativa  = 0;
bool mqttConectado = false;

// Clientes Wi-Fi e MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// Constantes e tipos do driver do DHT
enum DhtStatus { DHT_OK, DHT_ERRO_SEM_RESPOSTA, DHT_ERRO_TIMEOUT, DHT_ERRO_CHECKSUM };

#if DHT_MODELO == 11
const uint32_t DHT_INICIO_MS = 20;
#else
const uint32_t DHT_INICIO_MS = 2;
#endif
const uint32_t DHT_TIMEOUT_US = 200;

static portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// Mede por quanto tempo (us) a linha do DHT permanece no nível informado, com timeout
static uint32_t dhtTempoNoNivel(uint8_t nivel) {
  uint32_t inicio = micros();
  while (digitalRead(PINO_DHT) == nivel) {
    if (micros() - inicio > DHT_TIMEOUT_US) return UINT32_MAX;
  }
  return micros() - inicio;
}

// Envia o sinal de início, recebe os 40 bits do DHT e valida o checksum
static DhtStatus dhtLerBytes(uint8_t dados[5]) {
  for (int i = 0; i < 5; i++) dados[i] = 0;

  pinMode(PINO_DHT, OUTPUT);
  digitalWrite(PINO_DHT, LOW);
  delay(DHT_INICIO_MS);
  pinMode(PINO_DHT, INPUT);

  DhtStatus status = DHT_OK;
  portENTER_CRITICAL(&dhtMux);

  if (dhtTempoNoNivel(HIGH) == UINT32_MAX ||
      dhtTempoNoNivel(LOW)  == UINT32_MAX ||
      dhtTempoNoNivel(HIGH) == UINT32_MAX) {
    status = DHT_ERRO_SEM_RESPOSTA;
  } else {
    for (int i = 0; i < 40; i++) {
      uint32_t tBaixo = dhtTempoNoNivel(LOW);
      uint32_t tAlto  = dhtTempoNoNivel(HIGH);
      if (tBaixo == UINT32_MAX || tAlto == UINT32_MAX) {
        status = DHT_ERRO_TIMEOUT;
        break;
      }
      dados[i / 8] <<= 1;
      if (tAlto > tBaixo) dados[i / 8] |= 1;
    }
  }

  portEXIT_CRITICAL(&dhtMux);
  if (status != DHT_OK) return status;

  uint8_t soma = dados[0] + dados[1] + dados[2] + dados[3];
  if (soma != dados[4]) return DHT_ERRO_CHECKSUM;
  return DHT_OK;
}

// Converte os bytes recebidos em temperatura (°C) e umidade (%)
DhtStatus dhtLer(float &t, float &h) {
  uint8_t d[5];
  DhtStatus status = dhtLerBytes(d);
  if (status != DHT_OK) return status;

#if DHT_MODELO == 11
  h = d[0] + d[1] * 0.1f;
  t = d[2] + (d[3] & 0x7F) * 0.1f;
  if (d[3] & 0x80) t = -t;
#else
  h = ((d[0] << 8) | d[1]) * 0.1f;
  t = (((d[2] & 0x7F) << 8) | d[3]) * 0.1f;
  if (d[2] & 0x80) t = -t;
#endif
  return DHT_OK;
}

// Retorna a descrição do erro de leitura do DHT
const char* dhtErroTexto(DhtStatus s) {
  switch (s) {
    case DHT_ERRO_SEM_RESPOSTA: return "sensor nao respondeu (ligacao/pull-up?)";
    case DHT_ERRO_TIMEOUT:      return "timeout no meio dos bits";
    case DHT_ERRO_CHECKSUM:     return "checksum invalido";
    default:                    return "ok";
  }
}

// Recebe as mensagens MQTT e acende ou apaga o LED do tópico correspondente
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

// Conecta ao Wi-Fi e aguarda até a conexão ser estabelecida
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

// Conecta ao broker MQTT e assina os tópicos dos LEDs, tentando de novo a cada 5 s
void conectaMQTT() {
  if (client.connected()) return;
  if (inicioTentativa != 0 && millis() - inicioTentativa < 5000) return;

  Serial.print("Conectando ao broker MQTT... ");
  if (strlen(un) > 0) mqttConectado = client.connect(id, un, pw);
  else                mqttConectado = client.connect(id);

  if (mqttConectado) {
    Serial.println("conectado");
    client.subscribe(BASE "/LED/#");
    client.publish(TOP_STATUS, "online");
    presencaPublicada = -1;
    inicioTentativa = 0;
  } else {
    Serial.print("falha, rc=");
    Serial.print(client.state());
    Serial.println(" tentando novamente em 5 segundos");
    inicioTentativa = millis();
  }
}

// Lê o PIR continuamente e o DHT a cada 2 s, publicando temperatura e umidade
void leSensores() {
  presenca = digitalRead(PINO_PIR);

  if (millis() - ultimaLeituraDHT >= INTERVALO_DHT) {
    ultimaLeituraDHT = millis();
    float t, h;
    DhtStatus s = dhtLer(t, h);
    if (s != DHT_OK) {
      Serial.print("Falha na leitura do DHT: ");
      Serial.println(dhtErroTexto(s));
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

// Publica o estado da presença sempre que ele muda
void publicaPresenca() {
  if (client.connected() && presenca != presencaPublicada) {
    client.publish(TOP_PRESENCA, presenca ? "1" : "0");
    presencaPublicada = presenca;
    Serial.printf("Presenca -> %d\n", presenca);
  }
}

// Configura os pinos, conecta ao Wi-Fi e prepara o cliente MQTT
void setup() {
  Serial.begin(115200);

  pinMode(PINO_DHT, INPUT);
  pinMode(PINO_PIR, INPUT);
  pinMode(LED_PRESENCA, OUTPUT);
  pinMode(LED_TEMP_ALTA, OUTPUT);
  pinMode(LED_TEMP_BAIXA, OUTPUT);
  pinMode(LED_UMIDADE, OUTPUT);
  digitalWrite(LED_PRESENCA, LOW);
  digitalWrite(LED_TEMP_ALTA, LOW);
  digitalWrite(LED_TEMP_BAIXA, LOW);
  digitalWrite(LED_UMIDADE, LOW);

  conectaWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// Mantém as conexões ativas, processa mensagens e lê os sensores
void loop() {
  if (WiFi.status() != WL_CONNECTED) conectaWiFi();
  conectaMQTT();
  client.loop();

  leSensores();
  publicaPresenca();
}
