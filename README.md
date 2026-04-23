# PI-SRAEI

Sistema Residencial de Análise Energética Inteligente

**UNIVESP - Grupo 14**

---

## Descrição

Sistema de monitoramento energético IoT baseado em ESP32 com sensor de corrente SCT-013-000 (100A). O sistema realiza leitura contínua da corrente elétrica, calcula potência e acumula o consumo de energia em kWh, transmitindo os dados via Serial.

---

## Integrantes

- José Murilo Rodrigues Sabalo, 2232083
- Joseph Jucelio de Oliveira Santos, 2224541

---

## Disciplina & Polos

**Disciplina:** DRP04-Projeto Integrador em Computação V  
**Tema escolhido pelo grupo com base no tema norteador da Univesp:** Sistema de monitoramento de consumo energético residencial em tempo real e processamento de dados.  
**Título provisório do trabalho:** Sistema Residencial de Análise Energética Inteligente  
**Problema:** Consumo energético não consciente.  
**Objetivo:** Implementar um sistema de telemetria IoT capaz de coletar e processar dados de energia em tempo real para auxiliar no gerenciamento energético residencial.  
**Polo(s):** Hortolândia, Jaguariúna  
**Orientador do PI:** Lineia Soares da Silva

---

## Hardware

| Componente | Especificação |
|---|---|
| Microcontrolador | ESP32 |
| Sensor de corrente | SCT-013-000 (100A : 50mA) |
| Pino ADC | GPIO34 (ADC1_CH6 — somente leitura) |
| Tensão de referência | 3.3V |
| Resolução ADC | 12 bits (0–4095) |
| Resistor burden | 33 Ω |
| Relação de espiras | 2000 (100A / 0.05A) |
| Tensão da rede | 127V (configurável para 220V) |

---

## Firmware

### Arquivo principal

`arduino/leitorcorrente-esp32.ino`

### Configurações

```cpp
const int   PINO_SENSOR_CORRENTE = 34;      // GPIO34
const float TENSAO_REFERENCIA    = 3.3;     // ESP32 opera em 3.3V
const int   RESOLUCAO_ADC        = 4095;    // ADC de 12 bits
const float TENSAO_REDE          = 127.0;   // Ajuste para 220.0 se necessário
const float RESISTOR_BURDEN      = 33.0;    // Ohms
const float RELACAO_ESPIRAS      = 2000.0;  // 100A / 0.05A
```

### Modo simulação

O firmware possui um modo de simulação que dispensa o sensor físico:

```cpp
#define MODO_SIMULACAO 1  // 1 = simulação, 0 = sensor real
```

Em modo simulação, a corrente varia senoidalmente entre ~2A e ~14A, simulando carga residencial típica.

---

## Funcionamento

1. O ADC do ESP32 é configurado para 12 bits com atenuação de 11dB (faixa 0–3.3V).
2. A cada segundo, são coletadas **1000 amostras** do sensor SCT-013-000.
3. O offset de bias (1.65V) é removido de cada amostra.
4. A corrente RMS é calculada pela raiz da média dos quadrados.
5. Leituras abaixo de **0.3A** são descartadas como ruído.
6. Potência e energia acumulada são calculadas e exibidas via Serial (115200 baud).

### Fórmulas

```
Tensão ADC  = (leitura_adc / 4095) × 3.3V
Tensão sens = Tensão ADC − 1.65V (offset)
I_sec       = Tensão sens / 33Ω
I_prim      = I_sec × 2000
Potência    = I_rms × 127V
Energia     += Potência × Δt (kWh)
```

---

## Saída Serial

```
============================================
 Sistema de Monitoramento Energético IoT
 UNIVESP - Grupo 14
 Placa: ESP32
 Sensor: SCT-013-000 (100A)
============================================
Iniciando monitoramento...

Tempo(s): 1 | Corrente: 8.000 A | Potencia: 1016.00 W | Energia: 0.000282 kWh
Tempo(s): 2 | Corrente: 9.854 A | Potencia: 1251.46 W | Energia: 0.000629 kWh
...
```

Configurar o monitor serial para **115200 baud**.

---

## Arquivos

| Arquivo | Status | Descrição |
|---|---|---|
| `arduino/leitorcorrente-esp32.ino` | Ativo | Firmware principal para ESP32 |
| `arduino/Arduino-Sensor-SCT-013-000.ino` | **Descontinuado** | Versão legada para Arduino (ADC 10 bits, 5V) |

---

## Dashboard Web

O projeto inclui um dashboard web em `web/index.html` que simula o comportamento do firmware em tempo real no navegador, sem necessidade de hardware físico.

### Funcionalidades

- Indicadores em tempo real de **corrente RMS**, **potência**, **energia acumulada** e **custo estimado**
- Gráfico de linha com histórico das últimas 60 leituras (corrente e potência)
- Gráfico de rosca com percentual de uso da capacidade máxima do sensor
- Log serial simulado (replica a saída do `Serial.print` do firmware)
- Controles interativos: pausar/retomar, zerar energia, alterar tensão da rede (127V / 220V) e intervalo de leitura

### Como rodar

Não requer instalação de dependências. Basta abrir o arquivo diretamente no navegador:

**Opção 1 — Abrir direto (mais simples):**

```bash
# Windows
start web/index.html

# macOS
open web/index.html

# Linux
xdg-open web/index.html
```

**Opção 2 — Servidor local (recomendado para desenvolvimento):**

Com Python:
```bash
# Python 3
python -m http.server 8080 --directory web

# Acesse: http://localhost:8080
```

Com Node.js (`npx`):
```bash
npx serve web

# Acesse: http://localhost:3000
```

Com VS Code: instale a extensão **Live Server**, clique com o botão direito em `web/index.html` e selecione **Open with Live Server**.

### Tecnologias

| Tecnologia | Uso |
|---|---|
| HTML5 / CSS3 | Estrutura e estilo do dashboard |
| JavaScript (ES6+) | Lógica de simulação e atualização em tempo real |
| [Chart.js](https://www.chartjs.org/) (CDN) | Gráficos de linha e rosca |

> A simulação replica fielmente a função `lerCorrenteRMS()` do firmware: corrente senoidal entre ~2A e ~14A com período de 20s, cálculo de potência (`I × V_rede`) e acúmulo de energia em kWh.

---

## Estrutura do Projeto

```
PI-SRAEI/
├── arduino/
│   ├── leitorcorrente-esp32.ino       # Firmware principal (ESP32)
│   └── Arduino-Sensor-SCT-013-000.ino # Versão legada (descontinuada)
├── web/
│   └── index.html                     # Dashboard web
└── README.md
```

---

## Dependências

### Firmware
- [Arduino ESP32 Core](https://github.com/espressif/arduino-esp32)
- Biblioteca padrão `Arduino.h`

### Web
- [Chart.js](https://www.chartjs.org/) — carregado via CDN, sem instalação necessária

---

## Licença

Projeto acadêmico — UNIVESP Grupo 14.
