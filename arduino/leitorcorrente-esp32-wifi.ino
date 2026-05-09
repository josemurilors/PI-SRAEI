/*
 *  ███████╗██████╗  █████╗ ███████╗██╗
 *  ██╔════╝██╔══██╗██╔══██╗██╔════╝██║
 *  ███████╗██████╔╝███████║█████╗  ██║
 *  ╚════██║██╔══██╗██╔══██║██╔══╝  ██║
 *  ███████║██║  ██║██║  ██║███████╗███████╗
 *  ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝╚══════╝
 *
 *  Sistema Residencial de Análise Energética Inteligente
 *  UNIVESP - Grupo 14 - DRP04-PI em Computação V
 *  Placa: ESP32 | Sensor: SCT-013-000 (100A/1V)
 *
 *  Mede corrente RMS via ADC do ESP32 e envia dados
 *  via HTTP POST para servidor Flask.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// =====================================================================
// CONFIGURAÇÕES DO SENSOR SCT-013-000
// =====================================================================
// Circuito externo:
//   - Resistor burden: 33Ω  (converte corrente do secundário em tensão)
//   - Divisor de tensão: 2x 10kΩ (bias de 1.65V para o ADC)
//   - Capacitor: 10µF  (filtro passa-baixa)
// =====================================================================

const int   PINO_SENSOR_CORRENTE = 34;     // GPIO34 (ADC1_CH6)
const float TENSAO_REFERENCIA    = 3.3;    // Tensão de operação do ESP32
const int   RESOLUCAO_ADC        = 4095;   // ADC de 12 bits
const float TENSAO_REDE          = 127.0;  // 127V (BR) — altere para 220V se necessário
const float RESISTOR_BURDEN      = 33.0;   // Burden de 33Ω
const float RELACAO_ESPIRAS      = 2000.0; // 100A / 0.05A (relação do SCT-013-000)

// =====================================================================
// MODO SIMULAÇÃO (útil para testes sem sensor físico)
// =====================================================================
#define MODO_SIMULACAO 0  // 1 = gera corrente senoidal fictícia; 0 = lê ADC real

// =====================================================================
// CONFIGURAÇÕES DE REDE
// =====================================================================
const char* SSID     = "SEU_WIFI";      // Nome da rede WiFi
const char* PASSWORD = "SUA_SENHA";     // Senha da rede WiFi

// =====================================================================
// ENDPOINT DO SERVIDOR FLASK
// =====================================================================
const char* SERVER_URL = "http://192.168.1.XXX:5000/api/esp32";

// =====================================================================
// PARÂMETROS DE MEDIÇÃO
// =====================================================================
const float LIMIAR_RUIDO_CORRENTE = 0.5;  // Leituras abaixo deste valor são descartadas (ruído eletromagnético)
const int   NUM_AMOSTRAS_RMS      = 1000; // Quantidade de amostras por leitura RMS

// =====================================================================
// CONTROLE DE TEMPO
// =====================================================================
const int INTERVALO_LEITURA_MS = 1000; // Envia dados a cada 1 segundo

// =====================================================================
// VARIÁVEIS GLOBAIS
// =====================================================================
float corrente_rms   = 0.0;
float potencia_watts = 0.0;
float energia_kwh    = 0.0;

unsigned long tempo_anterior = 0;
unsigned long tempo_inicio   = 0;

// =====================================================================
// Função: lerCorrenteRMS
// Lê N amostras do ADC, calcula offset CC e retorna o valor RMS
// da corrente no primário do transformador.
// =====================================================================
float lerCorrenteRMS(int pino, int amostras = NUM_AMOSTRAS_RMS) {
  #if MODO_SIMULACAO
  // --- Modo simulação: gera senoide entre ~2A e ~14A ---
  float t = millis() / 1000.0;
  return max(0.0, 8.0 + 6.0 * sin(2.0 * PI * 0.05 * t));

  #else
  // --- Modo real: leitura do ADC com cálculo de offset ---

  // Descartam as primeiras 100 leituras para estabilizar o circuito
  for (int i = 0; i < 100; i++) {
    analogRead(pino);
    delayMicroseconds(100);
  }

  // Coleta as amostras e calcula a média (offset CC do ADC)
  float soma_valores = 0.0;
  float leituras[amostras];

  for (int i = 0; i < amostras; i++) {
    leituras[i] = analogRead(pino);
    soma_valores += leituras[i];
    delayMicroseconds(200);
  }

  // Offset em volts: nível CC sobre o qual a senoide está montada
  float media        = soma_valores / amostras;
  float offset_tensao = (media / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;

  // Calcula o RMS: remove o offset, eleva ao quadrado, tira a média, extrai a raiz
  float soma_quadrados = 0.0;
  for (int i = 0; i < amostras; i++) {
    float tensao_adc     = (leituras[i] / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;
    float tensao_sensor  = tensao_adc - offset_tensao;           // Remove nível CC
    float corrente_sec   = tensao_sensor / RESISTOR_BURDEN;      // I = V / R (secundário)
    float corrente_prim  = corrente_sec * RELACAO_ESPIRAS;       // Corrente real no primário
    soma_quadrados      += corrente_prim * corrente_prim;
  }

  if (amostras > 0) {
    return max(0.0, sqrt(soma_quadrados / amostras));
  }
  return 0.0;
  #endif
}

// =====================================================================
// SETUP — executado uma vez ao ligar o ESP32
// =====================================================================
void setup() {
  Serial.begin(115200);

  // Configura ADC do ESP32 para 12 bits e atenuação de 11dB (0‒3.3V)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  tempo_inicio   = millis();
  tempo_anterior = millis();

  // --- Banner inicial no serial ---
  Serial.println("============================================");
  Serial.println(" Sistema de Monitoramento Energético IoT   ");
  Serial.println(" UNIVESP - Grupo 14                        ");
  Serial.println(" Placa: ESP32                              ");
  Serial.println(" Sensor: SCT-013-000 (100A)                ");
  Serial.println("============================================");

  // --- Conexão WiFi ---
  Serial.print("Conectando ao WiFi ");
  Serial.println(SSID);
  WiFi.begin(SSID, PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado!");
  Serial.print("IP do ESP32: ");
  Serial.println(WiFi.localIP());
  Serial.println("\nIniciando monitoramento...\n");
}

// =====================================================================
// LOOP — executado infinitamente
// A cada INTERVALO_LEITURA_MS:
//   1. Lê a corrente RMS
//   2. Calcula potência e energia acumulada
//   3. Exibe no serial
//   4. Envia via HTTP POST para o servidor Flask
// =====================================================================
void loop() {
  unsigned long tempo_atual = millis();

  if (tempo_atual - tempo_anterior < INTERVALO_LEITURA_MS) {
    return;  // Ainda não é hora da próxima leitura
  }
  tempo_anterior = tempo_atual;

  // --- 1. Leitura da corrente RMS ---
  corrente_rms = lerCorrenteRMS(PINO_SENSOR_CORRENTE);

  // Descarta leituras abaixo do limiar (ruído com carga desligada)
  if (corrente_rms < LIMIAR_RUIDO_CORRENTE) {
    corrente_rms = 0.0;
  }

  // --- 2. Cálculo de potência e energia ---
  potencia_watts = corrente_rms * TENSAO_REDE;

  float dt_horas  = INTERVALO_LEITURA_MS / 3600000.0;        // ms → horas
  energia_kwh    += (potencia_watts * dt_horas) / 1000.0;    // W·h → kWh

  unsigned long tempo_decorrido_seg = (tempo_atual - tempo_inicio) / 1000;

  // --- 3. Saída no monitor serial ---
  Serial.print("Tempo(s): ");
  Serial.print(tempo_decorrido_seg);
  Serial.print(" | Corrente: ");
  Serial.print(corrente_rms, 3);
  Serial.print(" A | Potencia: ");
  Serial.print(potencia_watts, 2);
  Serial.print(" W | Energia: ");
  Serial.print(energia_kwh, 6);
  Serial.println(" kWh");

  // --- 4. Envio para o servidor Flask ---
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado!");
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  // Monta JSON: {"corrente": X, "potencia": Y, "energia": Z}
  String jsonData = "{";
  jsonData += "\"corrente\":" + String(corrente_rms, 3) + ",";
  jsonData += "\"potencia\":" + String(potencia_watts, 2) + ",";
  jsonData += "\"energia\":"  + String(energia_kwh, 6);
  jsonData += "}";

  int httpResponseCode = http.POST(jsonData);

  if (httpResponseCode > 0) {
    http.getString();  // Descarta body da resposta
    Serial.println("Dados enviados com sucesso!");
  } else {
    Serial.print("Erro ao enviar: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}
