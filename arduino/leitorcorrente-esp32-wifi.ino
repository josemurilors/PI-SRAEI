#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// --- Configurações do Sensor SCT-013-000 (100A/1V) no ESP32 ---
// Divisor de tensão: 2x 10kΩ resistores para bias de 1.65V
// Capacitor de 10µF para filtragem
// Resistor burden: 33Ω (valor típico para SCT-013-000)
const int   PINO_SENSOR_CORRENTE = 34;      // GPIO34 (ADC1_CH6 - somente leitura)
const float TENSAO_REFERENCIA    = 3.3;     // ESP32 opera em 3.3V
const int   RESOLUCAO_ADC        = 4095;    // ESP32 tem ADC de 12 bits
const float TENSAO_REDE        = 127.0;   // Ajuste para 220.0 se necessário

const float RESISTOR_BURDEN    = 33.0;   // Resistor burden típico para SCT-013-000 (33 Ohms)
const float RELACAO_ESPIRAS     = 2000.0;  // 100A / 0.05A

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
  // Leitura direta com cálculo de offset e filtragem
  // Primeiro, faz leituras iniciais para estabilizar o circuito
  for(int i = 0; i < 100; i++) {
    analogRead(pino);
    delayMicroseconds(100);
  }
  
  // Coleta as amostras e calcula a média para o offset
  float soma_valores = 0.0;
  float leituras[amostras];
  
  for (int i = 0; i < amostras; i++) {
    leituras[i] = analogRead(pino);
    soma_valores += leituras[i];
    delayMicroseconds(200);
  }
  
  // Calcula o offset (média das leituras)
  float media = soma_valores / amostras;
  float offset_tensao = (media / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;
  
  // Calcula RMS usando o offset
  float soma_quadrados = 0.0;
  for (int i = 0; i < amostras; i++) {
    float tensao_adc = (leituras[i] / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;
    float tensao_sensor = tensao_adc - offset_tensao;
    
    // Corrente no secundário: I = V / R_burden
    float corrente_sec = tensao_sensor / RESISTOR_BURDEN;
    // Corrente real (primário): I_prim = I_sec * relacao de espiras
    float corrente_prim = corrente_sec * RELACAO_ESPIRAS;
    
    // Acumula o quadrado da corrente primária
    soma_quadrados += corrente_prim * corrente_prim;
  }
  
  if (amostras > 0) {
    float corrente_rms = sqrt(soma_quadrados / amostras);
    // Garante que a corrente não seja negativa
    if (corrente_rms < 0) corrente_rms = 0.0;
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
  Serial.println(" Componentes:                              ");
  Serial.println("  - Resistor burden: 33 Ohm                 ");
  Serial.println("  - Divisor de tensão: 2x 10kOhm (bias 1.65V) ");
  Serial.println("  - Capacitor: 10uF para filtragem            ");
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
