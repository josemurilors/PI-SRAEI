#include <Arduino.h>

// --- Configurações do Sensor SCT-013-000 (100A) ---
const int   PINO_SENSOR_CORRENTE = A0;    // Pino analógico do sensor
const float TENSAO_REFERENCIA    = 5.0;   // Tensão de referência do Arduino (V)
const int   RESOLUCAO_ADC        = 1023;  // Resolução do ADC (10 bits)
const float TENSAO_REDE          = 127.0; // Tensão da rede elétrica (V)

// SCT-013-000: 100A:50mA (relação de espiras = 2000)
// Resistor burden de 33Ω → Vpico máx = 0.05A * 33Ω = 1.65V
const float RESISTOR_BURDEN  = 33.0;    // Ohms
const float RELACAO_ESPIRAS  = 2000.0;  // 100A / 0.05A
const float OFFSET_ADC       = (TENSAO_REFERENCIA / 2.0); // Bias = 2.5V (Arduino 5V)

// --- Modo simulação (1 = sem sensor, 0 = sensor real) ---
#define MODO_SIMULACAO 1

// --- Variáveis de medição ---
float corrente_rms   = 0.0;
float potencia_watts = 0.0;
float energia_kwh    = 0.0;

// --- Controle de tempo ---
unsigned long tempo_anterior    = 0;
unsigned long intervalo_leitura = 1000; // Leitura a cada 1 segundo
unsigned long tempo_inicio      = 0;


// Função: lerCorrenteRMS
// Lê N amostras do SCT-013-000 e calcula o valor RMS da corrente

float lerCorrenteRMS(int pino, int amostras = 1000) {
#if MODO_SIMULACAO
  // Simula corrente residencial variando entre ~2A e ~14A
  float t = millis() / 1000.0;
  float corrente = 8.0 + 6.0 * sin(2.0 * PI * 0.05 * t);
  if (corrente < 0) corrente = 0;
  return corrente;
#else
  float soma_quadrados = 0.0;

  for (int i = 0; i < amostras; i++) {
    int leitura_adc = analogRead(pino);

    // Converte ADC para tensão
    float tensao_adc = (leitura_adc / (float)RESOLUCAO_ADC) * TENSAO_REFERENCIA;

    // Remove o offset (bias de 2.5V no Arduino)
    float tensao_sensor = tensao_adc - OFFSET_ADC;

    // Corrente no secundário: I = V / R
    float corrente_sec = tensao_sensor / RESISTOR_BURDEN;

    // Corrente real (primário): I_prim = I_sec * relação de espiras
    float corrente_prim = corrente_sec * RELACAO_ESPIRAS;

    soma_quadrados += corrente_prim * corrente_prim;
    delayMicroseconds(200); // ~5 ciclos de 60Hz em 1000 amostras
  }

  float rms = sqrt(soma_quadrados / amostras);

  // Filtra ruído: abaixo de 0.3A é desprezível para sensor de 100A
  if (rms < 0.3) rms = 0.0;

  return rms;
#endif
}

void setup() {
  Serial.begin(9600);
  tempo_inicio   = millis();
  tempo_anterior = millis();

  Serial.println("============================================");
  Serial.println(" Sistema de Monitoramento Energético IoT   ");
  Serial.println(" UNIVESP - Grupo 14                        ");
  Serial.println(" Sensor: SCT-013-000 (100A)                ");
  Serial.println("============================================");
  Serial.println("Iniciando monitoramento...");
  Serial.println();
}

void loop() {
  unsigned long tempo_atual = millis();

  if (tempo_atual - tempo_anterior >= intervalo_leitura) {
    tempo_anterior = tempo_atual;

    // --- Leitura e cálculo ---
    corrente_rms   = lerCorrenteRMS(PINO_SENSOR_CORRENTE);
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
  }
}