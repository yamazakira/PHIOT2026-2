#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// 1 para simulação no Wokwi, 0 para o circuito real
#define SIMULACAO_WOKWI 1

// wifi
#if SIMULACAO_WOKWI
const char* ssid     = "Wokwi-GUEST";
const char* password = "";
#define DHT_MODELO 22
#else
const char* ssid     = "NPITI-IoT";
const char* password = "NPITI-IoT";
#define DHT_MODELO 11
#endif

// Configurações do broker MQTT
const char* mqtt_server = "broker.emqx.io";
const int   mqtt_port   = 1883;
const char* un          = "";
const char* pw          = "";
const char* id          = "ESP32_U2T2_AKIRA";

// Tópicos
#define BASE "/ESP32_U2T2_AKIRA"
const char* TOP_TEMP     = BASE "/temperatura";
const char* TOP_UMID     = BASE "/umidade";
const char* TOP_PRESENCA = BASE "/presenca";
const char* TOP_STATUS   = BASE "/status";
const char* TOP_LED_PRESENCA   = BASE "/LED/presenca";
const char* TOP_LED_TEMP_ALTA  = BASE "/LED/temp_alta";
const char* TOP_LED_TEMP_BAIXA = BASE "/LED/temp_baixa";
const char* TOP_LED_UMIDADE    = BASE "/LED/umidade";

// pinos
const uint8_t PINO_DHT = 4;
const uint8_t PINO_PIR = 27;
const uint8_t LED_PRESENCA   = 25;
const uint8_t LED_TEMP_ALTA  = 26;
const uint8_t LED_TEMP_BAIXA = 33;
const uint8_t LED_UMIDADE    = 32;

// variáveis temporárias dos sensores e controle de tempo
float temperatura = NAN;
float umidade     = NAN;
int   presenca    = LOW;
int   presencaPublicada = -1;

const unsigned long INTERVALO_DHT = 2000;
unsigned long ultimaLeituraDHT = 0;
unsigned long inicioTentativa  = 0;
bool mqttConectado = false;

// clientes
WiFiClient espClient;
PubSubClient client(espClient);

// constantes e tipos do driver do DHT
enum DhtStatus { DHT_OK, DHT_ERRO_SEM_RESPOSTA, DHT_ERRO_TIMEOUT, DHT_ERRO_CHECKSUM };

#if DHT_MODELO == 11
const uint32_t DHT_INICIO_MS = 20;
#else
const uint32_t DHT_INICIO_MS = 2;
#endif
const uint32_t DHT_TIMEOUT_US = 200;

static portMUX_TYPE dhtMux = portMUX_INITIALIZER_UNLOCKED;

// mede por quanto tempo (us) a linha do DHT permanece no nível informado, com timeout
static uint32_t dhtTempoNoNivel(uint8_t nivel) {
  uint32_t inicio = micros();
  while (digitalRead(PINO_DHT) == nivel) {
    if (micros() - inicio > DHT_TIMEOUT_US) return UINT32_MAX;
  }
  return micros() - inicio;
}

// envia o sinal de início, recebe os 40 bits do DHT e valida o checksum
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

// converte os bytes recebidos em temperatura (°C) e umidade (%)
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

// retorna a descrição do erro de leitura do DHT
const char* dhtErroTexto(DhtStatus s) {
  switch (s) {
    case DHT_ERRO_SEM_RESPOSTA: return "sensor nao respondeu (ligacao/pull-up?)";
    case DHT_ERRO_TIMEOUT:      return "timeout no meio dos bits";
    case DHT_ERRO_CHECKSUM:     return "checksum invalido";
    default:                    return "ok";
  }
}

// recebe as mensagens MQTT e acende ou apaga o LED do tópico correspondente
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

// converte o código de WiFi.status() em texto
const char* wifiStatusTexto(wl_status_t s) {
  switch (s) {
    case WL_IDLE_STATUS:     return "ocioso, iniciando";
    case WL_NO_SSID_AVAIL:   return "rede nao encontrada";
    case WL_SCAN_COMPLETED:  return "busca de redes concluida";
    case WL_CONNECTED:       return "conectado";
    case WL_CONNECT_FAILED:  return "falha na conexao";
    case WL_CONNECTION_LOST: return "conexao perdida";
    case WL_DISCONNECTED:    return "desconectado, tentando conectar";
    default:                 return "desconhecido";
  }
}

// converte o código de motivo da desconexão do ESP32 em texto
const char* wifiMotivoTexto(uint8_t motivo) {
  switch (motivo) {
    case 2:   return "autenticacao expirou (senha errada?)";
    case 8:   return "desconexao solicitada";
    case 15:  return "senha incorreta (handshake 4-way expirou)";
    case 201: return "rede nao encontrada (nome errado, fora de alcance ou rede so 5 GHz)";
    case 202: return "falha de autenticacao (senha incorreta)";
    case 203: return "falha de associacao com o roteador";
    case 204: return "senha incorreta (handshake expirou)";
    case 210:
    case 211:
    case 212: return "rede encontrada, mas incompativel ou sinal fraco";
    default:  return "outro motivo (ver lista wifi_err_reason_t do ESP-IDF)";
  }
}

// mostra o motivo informado pelo ESP32 sempre que a conexão Wi-Fi cai ou falha
void eventoWiFi(WiFiEvent_t evento, WiFiEventInfo_t info) {
  if (evento == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    uint8_t motivo = info.wifi_sta_disconnected.reason;
    Serial.printf("\nWi-Fi desconectado, motivo %u: %s\n", motivo, wifiMotivoTexto(motivo));
  }
}

// conecta ao Wi-Fi mostrando cada mudança de status e reinicia a tentativa após 20 s
void conectaWiFi() {
  Serial.printf("Conectando ao Wi-Fi %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  wl_status_t anterior = WL_NO_SHIELD;
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    wl_status_t atual = WiFi.status();
    if (atual != anterior) {
      Serial.printf("\n[%lu s] status %d: %s ", (millis() - inicio) / 1000, atual, wifiStatusTexto(atual));
      anterior = atual;
    }
    Serial.print(".");

    if (millis() - inicio > 20000) {
      Serial.println("\nSem conexao apos 20 s, reiniciando a tentativa");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
      inicio = millis();
      anterior = WL_NO_SHIELD;
    }
    delay(500);
  }
  Serial.println();
  Serial.print("Wi-Fi conectado. IP: ");
  Serial.print(WiFi.localIP());
  Serial.printf(" | sinal (RSSI): %d dBm\n", WiFi.RSSI());
}

// conecta ao broker MQTT e assina os tópicos dos LEDs, tentando de novo a cada 5 s
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

// lê o PIR continuamente e o DHT a cada 2 s, publicando temperatura e umidade
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

  WiFi.onEvent(eventoWiFi);
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
