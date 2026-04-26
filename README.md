# PI-SRAEI

Sistema Residencial de Análise Energética Inteligente

**UNIVESP - Grupo 14**

---

## Descrição

Sistema de monitoramento energético IoT baseado em ESP32 com sensor de corrente SCT-013-000 (100A). O sistema realiza leitura contínua da corrente elétrica, calcula potência e acumula o consumo de energia em kWh, transmitindo os dados via WiFi para um backend Flask que armazena e exibe os dados em um dashboard web.

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

`arduino/leitorcorrente-esp32-wifi.ino`

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
| `arduino/leitorcorrente-esp32-wifi.ino` | **Ativo** | Firmware ESP32 com WiFi + HTTP para backend |

---

## Conexão do ESP32 ao Backend (IoT)

Após montar o circuito do sensor SCT-013-000 com o ESP32, siga os passos abaixo para conectar ao backend Flask.

### 1. Circuito: SCT-013-000 → ESP32

```
SCT-013-000 (Sensor de Corrente)
├── Fio S (sinal) ──→ Burden Resistor (33Ω) ──→ ESP32 GPIO34 (ADC1_CH6)
└── Alimentação: VCC do ESP32 (3.3V) e GND comum
```

Sensor: SCT-013-000 (100A:50mA) com resistor burden (33Ω)

> **Nota:** O divisor de tensão (2 resistores de 10kΩ em série) cria um bias de 1.65V (metade de 3.3V), necessário para leitura de sinais AC.

### 2. Configurar o Firmware WiFi

Edite o arquivo `arduino/leitorcorrente-esp32-wifi.ino` e altere:

```cpp
const char* SSID     = "SEU_WIFI";      // Nome da sua rede WiFi
const char* PASSWORD = "SUA_SENHA";     // Senha do WiFi

const char* SERVER_URL = "http://192.168.1.XXX:5000/api/esp32";  // IP do computador + porta do Flask
```

Para descobrir o IP do computador rodando o Flask:
- **Windows:** `ipconfig` → procure "IPv4 Address"
- **Linux/macOS:** `ifconfig` ou `ip addr`

### 3. Upload do Firmware

1. Abra `leitorcorrente-esp32-wifi.ino` no Arduino IDE
2. Selecione a placa: **ESP32 Dev Module**
3. Selecione a porta COM correta
4. Clique em **Upload**

### 4. Rodar o Backend Flask

```bash
cd backend
pip install flask
python app.py
```

O backend ficará disponível em `http://SEU_IP:5000`.

### 5. Monitorar a Conexão

#### No ESP32 (Serial Monitor - 115200 baud):
```
Conectando ao WiFi SEU_WIFI
WiFi conectado!
IP do ESP32: 192.168.1.YYY
Iniciando monitoramento...

Tempo(s): 1 | Corrente: 8.000 A | Potencia: 1016.00 W | Energia: 0.000282 kWh
Dados enviados com sucesso!
```

#### No Terminal do Flask:
```
 * Running on http://0.0.0.0:5000
POST /api/esp32 - 200 OK
```

#### No Dashboard Web (`http://SEU_IP:5000`):
O site exibirá automaticamente os dados reais do ESP32 (sem simulação).

### 6. Funcionamento

```
[SCT-013-000] → [ESP32 GPIO34] → [lerCorrenteRMS()] → [WiFi HTTP POST] → [Flask /api/esp32]
                                                                          ↓
                                                                     [SQLite]
                                                                          ↓
                                                                  [Dashboard Web]
```

O backend detecta automaticamente se há dados do ESP32 no banco. Se houver, exibe dados reais; senão, usa simulação Python.

---

## Dashboard Web

O projeto conta com um backend em **Flask + SQLite** que recebe dados do ESP32 via WiFi, servindo os dados via API REST para um frontend web simplificado.

### Arquitetura

```
[ESP32 + SCT-013-000] ──WiFi──→ [Flask API /api/esp32] → [SQLite] → [Frontend Web]
                                      ↑
                              (simulação Python se sem ESP32)
```

O ESP32 envia os dados via HTTP POST para o endpoint `/api/esp32`. O backend armazena no SQLite e o frontend consome via `/api/data`.

### Funcionalidades do Site (Simplificado)

- Indicadores em tempo real: **Corrente RMS**, **Potência**, **Energia Acumulada**, **Custo Estimado**
- Gráfico de linha com histórico das últimas 50 leituras de corrente
- Controles: pausar/retomar, zerar energia, alterar tensão da rede (127V/220V), intervalo de leitura
- **Campo para alterar o valor do kWh** (preço da energia em R$)

> Seções removidas no layout simplificado: Log Serial, Distribuição de Potência, Hardware, Fórmulas, Projeto, Objetivos do Projeto, Exploratórios, Descritivos, Explicativos.

### Como rodar

#### 1. Backend (Flask + SQLite)

Pré-requisitos: Python 3.7+

```bash
cd backend

# Instalar dependências
pip install flask

# Rodar a aplicação
python app.py
```

O backend ficará disponível em `http://localhost:5000` (ou IP da máquina na rede).

#### 2. Frontend

Com o backend rodando, acesse no navegador:

```
http://localhost:5000
```

> O Flask serve o template `backend/templates/simple_index.html` automaticamente.

#### 3. (Opcional) Simulação sem ESP32

Se não tiver o hardware montado, o backend usa simulação Python automaticamente. Basta rodar o Flask e acessar o site.

### Tecnologias

| Tecnologia | Uso |
|---|---|
| ESP32 + WiFi | Coleta de dados e envio via HTTP |
| SCT-013-000 | Sensor de corrente não invasivo (100A) |
| Python 3 | Lógica de simulação (fallback) e API backend |
| Flask | Framework web leve para servir API e templates |
| SQLite | Armazenamento local das leituras (últimas 200) |
| HTML5 / CSS3 | Estrutura e estilo do dashboard (tema dark) |
| JavaScript (ES6+) | Consumo da API e atualização em tempo real |
| [Chart.js](https://www.chartjs.org/) (CDN) | Gráfico de linha histórico |

> A simulação no Python replica a função `lerCorrenteRMS()`: corrente senoidal entre ~2A e ~14A com período de 20s, cálculo de potência (`I × V_rede`) e acúmulo de energia em kWh.

---

## Dashboard Web

O projeto conta com um backend em **Flask + SQLite** que simula o comportamento do firmware ESP32, servindo os dados via API REST para um frontend web simplificado.

### Arquitetura

```
[ESP32 / Simulação Python] → [Flask API] → [SQLite] → [Frontend Web]
```

A simulação da função `lerCorrenteRMS()` roda em um thread em background no Flask, lendo/gerando dados idênticos ao firmware e armazenando leituras no SQLite.

### Funcionalidades do Site (Simplificado)

- Indicadores em tempo real: **Corrente RMS**, **Potência**, **Energia Acumulada**, **Custo Estimado**
- Gráfico de linha com histórico das últimas 50 leituras de corrente
- Controles: pausar/retomar, zerar energia, alterar tensão da rede (127V/220V), intervalo de leitura
- **Campo para alterar o valor do kWh** (preço da energia em R$)

> Seções removidas no layout simplificado: Log Serial, Distribuição de Potência, Hardware, Fórmulas, Projeto, Objetivos do Projeto, Exploratórios, Descritivos, Explicativos.

### Como rodar

#### 1. Backend (Flask + SQLite)

Pré-requisitos: Python 3.7+

```bash
cd backend

# Instalar dependências
pip install flask

# Rodar a aplicação
python app.py
```

O backend ficará disponível em `http://localhost:5000`.

#### 2. Frontend

Com o backend rodando, acesse no navegador:

```
http://localhost:5000
```

> O Flask serve o template `backend/templates/simple_index.html` automaticamente.

### Tecnologias

| Tecnologia | Uso |
|---|---|
| Python 3 | Lógica de simulação e API backend |
| Flask | Framework web leve para servir API e templates |
| SQLite | Armazenamento local das leituras (últimas 200) |
| HTML5 / CSS3 | Estrutura e estilo do dashboard (tema dark) |
| JavaScript (ES6+) | Consumo da API e atualização em tempo real |
| [Chart.js](https://www.chartjs.org/) (CDN) | Gráfico de linha histórico |

> A simulação no Python replica a função `lerCorrenteRMS()`: corrente senoidal entre ~2A e ~14A com período de 20s, cálculo de potência (`I × V_rede`) e acúmulo de energia em kWh.

---

## Estrutura do Projeto

```
PI-SRAEI/
├── arduino/
│   └── leitorcorrente-esp32-wifi.ino       # Firmware principal (ESP32)
├── backend/
│   ├── app.py                         # Flask app + SQLite
│   ├── sraei.db                       # Banco SQLite (criado automaticamente)
│   └── templates/
│       └── simple_index.html           # Dashboard web simplificado
└── README.md
```

---

## Dependências

### Firmware (ESP32)
- [Arduino ESP32 Core](https://github.com/espressif/arduino-esp32)
- Biblioteca padrão `Arduino.h`

### Backend
```bash
pip install flask
```

### Frontend
- [Chart.js](https://www.chartjs.org/) — carregado via CDN

---

## Licença

Projeto acadêmico — UNIVESP Grupo 14.
