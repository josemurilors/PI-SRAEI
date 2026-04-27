#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// --- Configurações do Sensor SCT-013-000 (100A) no ESP32 ---
// Nova configuração com divisor de tensão de 10kΩ e resistor de proteção de 100Ω
const int   PINO_SENSOR_CORRENTE = 34;      // GPIO34 (ADC1_CH6 - somente leitura)
const float TENSAO_REFERENCIA    = 3.3;     // ESP32 opera em 3.3V
const int   RESOLUCAO_ADC        = 4095;    // ESP32 tem ADC de 12 bits
const float TENSAO_REDE        = 127.0;   // Ajuste para 220.0 se necessário

// Novos componentes do circuito:
// - Divisor de tensão: 2x 10kΩ resistores para bias de 1.65V
// - Capacitor de 10µF para filtragem
// - Resistor burden: 300Ω (3x 100Ω em serie)
const float RESISTOR_BURDEN    = 300.0;   // Resistor burden adaptado (3x 100Ohm em serie)
const float RESISTOR_DIVISOR   = 10000.0;  // 2x 10kΩ em série para bias
const float RESISTOR_PROTECAO   = 100.0;   // Resistor de proteção (100Ω)
const float CAPACITOR_FILTRO    = 0.00001;  // 10µF = 0.00001F
const float RELACAO_ESPIRAS     = 2000.0;  // 100A / 0.05A
const float OFFSET_ADC         = (TENSAO_REFERENCIA / 2.0); // Bias = 1.65V

// --- Modo simulação (1 = sem sensor, 0 = sensor real) ---
#define MODO_SIMULACAO 0

// --- Configurações WiFi ---
const char* SSID     = "SEU_WIFI";      // Substitua pelo nome da sua rede
const char* PASSWORD = "SUA_SENHA";     // Substitua pela senha

// --- Configurações do Servidor ---
const char* SERVER_URL = "http://192.168.1.XXX:5000/api/esp32";  // IP do computador rodando Flask

// --- Variáveis de medição ---
float corrente_rms   = 0.0;
float potencia_watts = 0.0;
float energia_kwh    = 0.0;

// --- Controle de tempo ---
unsigned long tempo_anterior    = 0;
unsigned long intervalo_leitura = 1000; // Leitura a cada 1 segundo
unsigned long tempo_inicio      = 0;

// ============================================================
// Função: lerCorrenteRMS
// Lê N amostras do SCT-013-000 e calcula o valor RMS da corrente
// ============================================================
float lerCorrenteRMS(int pino, int amostras = 1000) {
  #if MODO_SIMULACAO
  // Simula corrente residencial variando entre ~2A e ~14A
  float t = millis() / 1000.0;
  float corrente = 8.0 + 6.0 * sin(2.0 * PI * 0.05 * t);
  if (corrente < 0) corrente = 0;
  return corrente;
  #else
  // --- Passo 1: calcula o offset dinamico (media real do divisor de tensao) ---
  // Isso elimina erros causados por variacoes nos resistores ou ruido no bias
  float soma_offset = 0.0;
  for (int i = 0; i < amostras; i++) {
    soma_offset += analogRead(pino);
    delayMicroseconds(200);
  }
  float offset_adc_counts = soma_offset / amostras;
  float offset_dinamico   = (offset_adc_counts / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;

  // Log do offset medido para diagnostico via Serial
  Serial.print("[DEBUG] Offset dinamico medido: ");
  Serial.print(offset_dinamico, 4);
  Serial.println(" V  (esperado ~1.6500 V)");

  // --- Passo 2: calcula o RMS usando o offset dinamico ---
  float soma_quadrados = 0.0;
  for (int i = 0; i < amostras; i++) {
    int leitura_adc = analogRead(pino);

    // Converte ADC para tensao (ESP32: 0-4095 -> 0-3.3V)
    float tensao_adc = (leitura_adc / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;

    // Remove o offset dinamico medido (substitui o 1.65V fixo)
    float tensao_sensor = tensao_adc - offset_dinamico;

    // Corrente no secundario: I = V / R_burden
    float corrente_sec = tensao_sensor / RESISTOR_BURDEN;

    // Corrente real (primario): I_prim = I_sec * relacao de espiras
    float corrente_prim = corrente_sec * RELACAO_ESPIRAS;

    // Acumula o quadrado da corrente primaria
    soma_quadrados += corrente_prim * corrente_prim;
    delayMicroseconds(200);
  }

  if (amostras > 0) {
    float corrente_rms = sqrt(soma_quadrados / amostras);
    return corrente_rms;
  }
  return 0.0;
  #endif
}

// ============================================================
void setup() {
  Serial.begin(115200);  // ESP32 usa padrão 115200

  // Configura ADC do ESP32 para máxima precisão
  analogReadResolution(12);        // 12 bits (0-4095)
  analogSetAttenuation(ADC_11db);  // Permite leitura na faixa 0-3.3V

  tempo_inicio   = millis();
  tempo_anterior = millis();

  Serial.println("============================================");
  Serial.println(" Sistema de Monitoramento Energético IoT   ");
  Serial.println(" UNIVESP - Grupo 14                        ");
  Serial.println(" Placa: ESP32                              ");
  Serial.println(" Sensor: SCT-013-000 (100A)                ");
  Serial.println(" Componentes:                                ");
  Serial.println("  - Divisor de tensao: 2x 10kOhm             ");
  Serial.println("  - Capacitor: 10uF                         ");
  Serial.println("  - Resistor burden: 100 Ohm (adaptado)    ");
  Serial.println("============================================");

  // Conecta ao WiFi
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
  Serial.println();

  Serial.println("Iniciando monitoramento...");
  Serial.println();
}

// ============================================================
void loop() {
  unsigned long tempo_atual = millis();

  if (tempo_atual - tempo_anterior >= intervalo_leitura) {
    tempo_anterior = tempo_atual;

    // --- Leitura e cálculo ---
    corrente_rms   = lerCorrenteRMS(PINO_SENSOR_CORRENTE);

    // Limiar de ruido: leituras abaixo de 0.5A com carga ausente
    // sao interferencia eletromagnetica do fio, nao corrente real
    if (corrente_rms < 0.5) {
      corrente_rms = 0.0;
    }

    potencia_watts = corrente_rms * TENSAO_REDE;

    // Acumula energia em kWh (P[W] * dt[h])
    float dt_horas = intervalo_leitura / 3600000.0;
    energia_kwh   += (potencia_watts * dt_horas) / 1000.0;

    // Tempo decorrido em segundos
    unsigned long tempo_seg = (tempo_atual - tempo_inicio) / 1000;

    // --- Saída Serial ---
    Serial.print("Tempo(s): ");
    Serial.print(tempo_seg);
    Serial.print(" | Corrente: ");
    Serial.print(corrente_rms, 3);
    Serial.print(" A | Potencia: ");
    Serial.print(potencia_watts, 2);
    Serial.print(" W | Energia: ");
    Serial.print(energia_kwh, 6);
    Serial.println(" kWh");

    // --- Envia dados para o servidor Flask ---
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(SERVER_URL);
      http.addHeader("Content-Type", "application/json");

      // Cria o JSON com os dados
      String jsonData = "{";
      jsonData += "\"corrente\":" + String(corrente_rms, 3) + ",";
      jsonData += "\"potencia\":" + String(potencia_watts, 2) + ",";
      jsonData += "\"energia\":" + String(energia_kwh, 6);
      jsonData += "}";

      int httpResponseCode = http.POST(jsonData);

      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.println("Dados enviados com sucesso!");
      } else {
        Serial.print("Erro ao enviar: ");
        Serial.println(httpResponseCode);
      }

      http.end();
    } else {
      Serial.println("WiFi desconectado!");
    }
  }
}
