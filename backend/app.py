"""
SRAEI — Sistema Residencial de Análise Energética Inteligente
UNIVESP - Grupo 14 - DRP04-PI em Computação V

Backend Flask que:
  - Recebe dados do ESP32 (corrente, potência, energia) via POST /api/esp32
  - Armazena leituras em SQLite (últimas 200)
  - Expõe API REST para o frontend Chart.js
  - Permite configurar preço do kWh e resetar acumuladores
"""

import csv
import math
import os
import sqlite3
import threading
import time
from flask import Flask, render_template, jsonify, request

app = Flask(__name__)
DATABASE = 'sraei.db'

# =====================================================================
# CONFIGURAÇÕES PADRÃO
# =====================================================================
PRICE_PER_KWH_DEFAULT = 0.85       # R$/kWh
LIMITE_LEITURAS       = 200        # Máximo de registros mantidos no banco

# =====================================================================
# CONFIGURAÇÕES DE SIMULAÇÃO
# =====================================================================
TENSAO_REDE_DEFAULT       = 127.0  # Volts
SIM_AMPLITUDE_DEFAULT     = 6.0    # Variação máxima da corrente simulada (A)
SIM_OFFSET_DEFAULT        = 8.0    # Corrente base simulada (A)
SIM_FREQUENCIA_DEFAULT    = 0.05   # Frequência da onda senoidal

# Controle da thread de simulação
_simulation_stop   = threading.Event()
_simulation_thread = None

# =====================================================================
# CONFIGURAÇÕES DE ANÁLISE
# =====================================================================
CSV_PATH = os.path.expanduser('~/Downloads/consumo_eletrico_editado.csv')

# =====================================================================
# FUNÇÕES AUXILIARES — BANCO DE DADOS
# =====================================================================

def get_db():
    """Retorna conexão com o SQLite (cacheada por request)."""
    db = getattr(app, '_database', None)
    if db is None:
        db = app._database = sqlite3.connect(DATABASE)
        db.row_factory = sqlite3.Row
    return db

def close_db(exception=None):
    """Fecha a conexão com o banco ao finalizar o request."""
    db = getattr(app, '_database', None)
    if db is not None:
        db.close()
        app._database = None

def init_db():
    """Cria as tabelas no SQLite caso não existam."""
    with app.app_context():
        db = get_db()
        cursor = db.cursor()

        # Tabela de configuração (chave-valor)
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS config (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            )
        """)
        cursor.execute("""
            INSERT OR IGNORE INTO config (key, value) VALUES (?, ?)
        """, ('price_per_kwh', str(PRICE_PER_KWH_DEFAULT)))
        cursor.execute("""
            INSERT OR IGNORE INTO config (key, value) VALUES (?, ?)
        """, ('simulation_enabled', '0'))
        cursor.execute("""
            INSERT OR IGNORE INTO config (key, value) VALUES (?, ?)
        """, ('sim_step', '0'))

        # Tabela de leituras do sensor
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL,
                corrente REAL,
                potencia REAL,
                energia REAL
            )
        """)
        db.commit()

# =====================================================================
# TABELA E IMPORTAÇÃO — DADOS DE ANÁLISE (CSV)
# =====================================================================

def init_analise_db():
    """Cria a tabela de análise e importa o CSV se vazia."""
    with app.app_context():
        db = get_db()
        db.execute("""
            CREATE TABLE IF NOT EXISTS consumo_analise (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                entidade TEXT NOT NULL,
                consumo_kwh REAL NOT NULL
            )
        """)
        db.commit()

        total = db.execute("SELECT COUNT(*) FROM consumo_analise").fetchone()[0]
        if total == 0 and os.path.exists(CSV_PATH):
            print(f"[Analise] Importando {CSV_PATH}...")
            with open(CSV_PATH, newline='') as f:
                reader = csv.DictReader(f)
                batch = []
                for row in reader:
                    batch.append((
                        row['Timestamp'],
                        row['Entidade'],
                        float(row['Consumo_kWh']),
                    ))
                db.executemany(
                    "INSERT INTO consumo_analise (timestamp, entidade, consumo_kwh) "
                    "VALUES (?, ?, ?)", batch
                )
                db.commit()
            print(f"[Analise] Importados {len(batch)} registros.")


def get_entidades():
    """Retorna lista de entidades únicas."""
    db = sqlite3.connect(DATABASE)
    rows = db.execute(
        "SELECT DISTINCT entidade FROM consumo_analise ORDER BY entidade"
    ).fetchall()
    db.close()
    return [r[0] for r in rows]


# =====================================================================
# FUNÇÕES AUXILIARES — PREÇO
# =====================================================================

def get_price_per_kwh():
    """Lê o preço do kWh armazenado no banco."""
    db = sqlite3.connect(DATABASE)
    row = db.execute(
        "SELECT value FROM config WHERE key = ?", ('price_per_kwh',)
    ).fetchone()
    db.close()
    return float(row[0]) if row else PRICE_PER_KWH_DEFAULT

def set_price_per_kwh(value):
    """Atualiza o preço do kWh no banco."""
    db = sqlite3.connect(DATABASE)
    db.execute("UPDATE config SET value = ? WHERE key = ?",
               (str(value), 'price_per_kwh'))
    db.commit()
    db.close()

# =====================================================================
# FUNÇÕES AUXILIARES — SIMULAÇÃO
# =====================================================================

def simulation_worker():
    """Gera leituras sintéticas no banco enquanto simulation_enabled=true."""
    step = 0
    while not _simulation_stop.is_set():
        try:
            db = sqlite3.connect(DATABASE)
            row = db.execute(
                "SELECT value FROM config WHERE key = ?", ('simulation_enabled',)
            ).fetchone()
            enabled = row and row[0] == '1'

            if enabled:
                corrente = max(0.0, SIM_OFFSET_DEFAULT + SIM_AMPLITUDE_DEFAULT *
                               math.sin(2 * math.pi * SIM_FREQUENCIA_DEFAULT * step))
                tensao = TENSAO_REDE_DEFAULT
                potencia = corrente * tensao

                last = db.execute(
                    "SELECT energia FROM readings ORDER BY id DESC LIMIT 1"
                ).fetchone()
                energia_anterior = last[0] if last else 0.0
                energia = energia_anterior + potencia * (1.0 / 3600.0) / 1000.0

                db.execute(
                    "INSERT INTO readings (timestamp, corrente, potencia, energia) "
                    "VALUES (?, ?, ?, ?)",
                    (time.time(), corrente, potencia, energia)
                )
                db.execute("""
                    DELETE FROM readings WHERE id NOT IN (
                        SELECT id FROM readings ORDER BY id DESC LIMIT ?
                    )
                """, (LIMITE_LEITURAS,))
                db.commit()
                step += 1

            db.close()
        except Exception as e:
            print(f"[Simulacao] Erro: {e}")

        _simulation_stop.wait(1.0)


def iniciar_simulacao():
    """Garante que a thread da simulação esteja rodando em background."""
    global _simulation_thread
    if _simulation_thread is None or not _simulation_thread.is_alive():
        _simulation_stop.clear()
        _simulation_thread = threading.Thread(target=simulation_worker, daemon=True)
        _simulation_thread.start()


def is_simulation_enabled():
    """Retorna True se a simulação estiver ativa no banco."""
    try:
        db = sqlite3.connect(DATABASE)
        row = db.execute(
            "SELECT value FROM config WHERE key = ?", ('simulation_enabled',)
        ).fetchone()
        db.close()
        return row is not None and row[0] == '1'
    except Exception:
        return False


def is_simulation_running():
    """Retorna True se o ESP32 não enviou dados recentemente."""
    try:
        db = sqlite3.connect(DATABASE)
        row = db.execute(
            "SELECT timestamp FROM readings ORDER BY id DESC LIMIT 1"
        ).fetchone()
        db.close()
        if row is None:
            return False
        return (time.time() - row[0]) > 10
    except Exception:
        return False


# =====================================================================
# ROTA PRINCIPAL — Frontend
# =====================================================================

@app.route('/')
def index():
    """Serve a página HTML do dashboard."""
    return render_template('simple_index.html')

# =====================================================================
# API — Dados para o gráfico (GET)
# =====================================================================

@app.route('/api/data')
def api_data():
    """
    Retorna JSON com:
      - Última leitura (corrente, potência, energia)
      - Custo estimado (energia × preço do kWh)
      - Histórico das últimas 50 leituras para o gráfico
    """
    db = sqlite3.connect(DATABASE)

    # Última leitura disponível
    ultima = db.execute("""
        SELECT corrente, potencia, energia FROM readings
        ORDER BY id DESC LIMIT 1
    """).fetchone()

    if ultima:
        corrente, potencia, energia = ultima[0], ultima[1], ultima[2]
    else:
        corrente = potencia = energia = 0.0

    # Custo estimado
    price = get_price_per_kwh()
    custo = energia * price

    # Histórico para o gráfico de linha (últimas 50 leituras)
    rows = db.execute("""
        SELECT timestamp, corrente FROM readings
        ORDER BY timestamp DESC LIMIT 50
    """).fetchall()
    db.close()

    # Prepara labels e dados em ordem cronológica
    timestamps = [r[0] for r in reversed(rows)]
    correntes  = [r[1] for r in reversed(rows)]
    if timestamps:
        base   = timestamps[0]
        labels = [f"{int(ts - base)}s" for ts in timestamps]
    else:
        labels = []
        correntes = []

    return jsonify({
        'corrente':       round(corrente, 3),
        'potencia':       round(potencia, 2),
        'energia':        round(energia, 6),
        'custo':          round(custo, 4),
        'price_per_kwh':  price,
        'chart_labels':   labels,
        'chart_data':     correntes,
    })

# =====================================================================
# API — Recebe dados do ESP32 (POST)
# =====================================================================

@app.route('/api/esp32', methods=['POST'])
def api_esp32():
    """
    Endpoint chamado pelo ESP32.
    Recebe JSON { corrente, potencia, energia } e salva no banco.
    Mantém apenas as últimas LIMITE_LEITURAS leituras.
    """
    data = request.get_json()
    if not data:
        return jsonify({'error': 'No data received'}), 400

    corrente = data.get('corrente')
    potencia = data.get('potencia')
    energia  = data.get('energia')

    if corrente is None or potencia is None or energia is None:
        return jsonify({'error': 'Missing fields'}), 400

    db = sqlite3.connect(DATABASE)
    db.execute("""
        INSERT INTO readings (timestamp, corrente, potencia, energia)
        VALUES (?, ?, ?, ?)
    """, (time.time(), corrente, potencia, energia))

    # Remove registros excedentes
    db.execute("""
        DELETE FROM readings WHERE id NOT IN (
            SELECT id FROM readings ORDER BY id DESC LIMIT ?
        )
    """, (LIMITE_LEITURAS,))
    db.commit()
    db.close()

    return jsonify({'success': True})

# =====================================================================
# API — Atualiza preço do kWh (POST)
# =====================================================================

@app.route('/api/price', methods=['POST'])
def api_price():
    """Recebe { price: <float> } e atualiza o preço do kWh."""
    data = request.get_json()
    if not data or 'price' not in data:
        return jsonify({'error': 'Missing price'}), 400

    try:
        price = float(data['price'])
        if price < 0:
            raise ValueError
    except (ValueError, TypeError):
        return jsonify({'error': 'Invalid price'}), 400

    set_price_per_kwh(price)
    return jsonify({'success': True, 'price': price})

# =====================================================================
# API — Reseta energia acumulada e leituras (POST)
# =====================================================================

@app.route('/api/reset', methods=['POST'])
def api_reset():
    """Apaga todas as leituras do banco."""
    db = sqlite3.connect(DATABASE)
    db.execute("DELETE FROM readings")
    db.commit()
    db.close()
    return jsonify({'success': True})

# =====================================================================
# PÁGINA DE CONFIGURAÇÃO DO DESENVOLVEDOR
# =====================================================================

@app.route('/dev-config')
def dev_config():
    """Serve a página de configuração do desenvolvedor."""
    return render_template('dev_config.html')


# =====================================================================
# API — Status da simulação (GET)
# =====================================================================

@app.route('/api/simulation', methods=['GET'])
def api_simulation_status():
    """Retorna o estado atual da simulação."""
    enabled = is_simulation_enabled()
    simulando = is_simulation_running()

    db = sqlite3.connect(DATABASE)
    row_step = db.execute(
        "SELECT value FROM config WHERE key = ?", ('sim_step',)
    ).fetchone()
    db.close()

    step = int(row_step[0]) if row_step else 0
    corrente = max(0.0, SIM_OFFSET_DEFAULT + SIM_AMPLITUDE_DEFAULT *
                   math.sin(2 * math.pi * SIM_FREQUENCIA_DEFAULT * step))

    return jsonify({
        'enabled':          enabled,
        'simulando':        simulando,
        'step':             step,
        'corrente_atual':   round(corrente, 3),
        'amplitude':        SIM_AMPLITUDE_DEFAULT,
        'offset':           SIM_OFFSET_DEFAULT,
        'frequencia':       SIM_FREQUENCIA_DEFAULT,
        'tensao_rede':      TENSAO_REDE_DEFAULT,
    })


# =====================================================================
# API — Alterna simulação (POST)
# =====================================================================

@app.route('/api/simulation', methods=['POST'])
def api_simulation_toggle():
    """Ativa ou desativa a simulação. Espera { enabled: bool }."""
    data = request.get_json()
    if not data or 'enabled' not in data:
        return jsonify({'error': 'Missing enabled field'}), 400

    enabled = '1' if data['enabled'] else '0'

    db = sqlite3.connect(DATABASE)
    db.execute("UPDATE config SET value = ? WHERE key = ?",
               (enabled, 'simulation_enabled'))
    if enabled == '1':
        db.execute("UPDATE config SET value = ? WHERE key = ?",
                   ('0', 'sim_step'))
    db.commit()
    db.close()

    # Se ativou e não há dados recentes, gera um ponto inicial
    if enabled == '1':
        try:
            db2 = sqlite3.connect(DATABASE)
            ultima = db2.execute(
                "SELECT corrente, energia FROM readings ORDER BY id DESC LIMIT 1"
            ).fetchone()
            if not ultima:
                corrente = SIM_OFFSET_DEFAULT
                tensao = TENSAO_REDE_DEFAULT
                potencia = corrente * tensao
                energia = 0.0
                db2.execute(
                    "INSERT INTO readings (timestamp, corrente, potencia, energia) "
                    "VALUES (?, ?, ?, ?)",
                    (time.time(), corrente, potencia, energia)
                )
                db2.commit()
            db2.close()
        except Exception as e:
            print(f"[Simulacao] Erro ao gerar primeiro ponto: {e}")

    return jsonify({'success': True, 'enabled': enabled == '1'})


# =====================================================================
# PÁGINA DE ANÁLISE DE DADOS
# =====================================================================

@app.route('/analise')
def analise():
    """Serve a página de análise de dados."""
    entidades = get_entidades()
    return render_template('analise.html', entidades=entidades)


# =====================================================================
# API — Resumo da análise (GET)
# =====================================================================

@app.route('/api/analise/resumo')
def api_analise_resumo():
    """Retorna totais e médias gerais."""
    entidade = request.args.get('entidade')
    entidades_raw = request.args.get('entidades', '')
    data_ini = request.args.get('data_ini')
    data_fim = request.args.get('data_fim')

    entidades = [e.strip() for e in entidades_raw.split(',') if e.strip()]

    where = []
    params = []
    if entidade:
        where.append("entidade = ?")
        params.append(entidade)
    if entidades:
        placeholders = ','.join('?' for _ in entidades)
        where.append(f"entidade IN ({placeholders})")
        params.extend(entidades)
    if data_ini:
        where.append("timestamp >= ?")
        params.append(data_ini)
    if data_fim:
        where.append("timestamp <= ?")
        params.append(data_fim + " 23:59:59")

    clause = (" WHERE " + " AND ".join(where)) if where else ""

    db = sqlite3.connect(DATABASE)
    row = db.execute(f"""
        SELECT COUNT(*), ROUND(SUM(consumo_kwh), 2),
               ROUND(AVG(consumo_kwh), 3),
               ROUND(MAX(consumo_kwh), 2), ROUND(MIN(consumo_kwh), 2)
        FROM consumo_analise{clause}
    """, params).fetchone()

    row_data = db.execute(f"""
        SELECT COUNT(DISTINCT DATE(timestamp)) FROM consumo_analise{clause}
    """, params).fetchone()
    db.close()

    preco = get_price_per_kwh()
    total_kwh = row[1] or 0
    return jsonify({
        'total_kwh':        total_kwh,
        'media_kwh':        row[2] or 0,
        'max_kwh':          row[3] or 0,
        'min_kwh':          row[4] or 0,
        'total_registros':  row[0],
        'dias':             row_data[0] or 0,
        'preco_kwh':        preco,
        'custo_total':      round(total_kwh * preco, 2),
    })


# =====================================================================
# API — Consumo por entidade (GET)
# =====================================================================

@app.route('/api/analise/por-entidade')
def api_analise_por_entidade():
    """Retorna consumo total por entidade."""
    entidades_raw = request.args.get('entidades', '')
    data_ini = request.args.get('data_ini')
    data_fim = request.args.get('data_fim')

    entidades = [e.strip() for e in entidades_raw.split(',') if e.strip()]

    where = []
    params = []
    if entidades:
        placeholders = ','.join('?' for _ in entidades)
        where.append(f"entidade IN ({placeholders})")
        params.extend(entidades)
    if data_ini:
        where.append("timestamp >= ?")
        params.append(data_ini)
    if data_fim:
        where.append("timestamp <= ?")
        params.append(data_fim + " 23:59:59")

    clause = (" WHERE " + " AND ".join(where)) if where else ""

    db = sqlite3.connect(DATABASE)
    rows = db.execute(f"""
        SELECT entidade, ROUND(SUM(consumo_kwh), 2) as total,
               ROUND(AVG(consumo_kwh), 3) as media,
               COUNT(*) as leituras
        FROM consumo_analise{clause}
        GROUP BY entidade ORDER BY total DESC
    """, params).fetchall()
    db.close()

    return jsonify({
        'entidades': [{
            'nome':     r[0],
            'total':    r[1],
            'media':    r[2],
            'leituras': r[3],
        } for r in rows]
    })


# =====================================================================
# API — Consumo ao longo do tempo (GET)
# =====================================================================

@app.route('/api/analise/por-tempo')
def api_analise_por_tempo():
    """Retorna consumo agregado por dia."""
    entidades_raw = request.args.get('entidades', '')
    data_ini = request.args.get('data_ini')
    data_fim = request.args.get('data_fim')

    entidades = [e.strip() for e in entidades_raw.split(',') if e.strip()]

    where = []
    params = []
    if entidades:
        placeholders = ','.join('?' for _ in entidades)
        where.append(f"entidade IN ({placeholders})")
        params.extend(entidades)
    if data_ini:
        where.append("timestamp >= ?")
        params.append(data_ini)
    if data_fim:
        where.append("timestamp <= ?")
        params.append(data_fim + " 23:59:59")

    clause = (" WHERE " + " AND ".join(where)) if where else ""

    preco = get_price_per_kwh()
    db = sqlite3.connect(DATABASE)
    rows = db.execute(f"""
        SELECT DATE(timestamp) as dia, ROUND(SUM(consumo_kwh), 2) as total
        FROM consumo_analise{clause}
        GROUP BY dia ORDER BY dia
    """, params).fetchall()
    db.close()

    return jsonify({
        'dias':      [r[0] for r in rows],
        'valores':   [r[1] for r in rows],
        'custo':     [round(r[1] * preco, 2) for r in rows],
        'preco_kwh': preco,
    })


# =====================================================================
# API — Consumo por hora do dia (GET)
# =====================================================================

@app.route('/api/analise/por-hora')
def api_analise_por_hora():
    """Retorna consumo médio por hora do dia."""
    entidades_raw = request.args.get('entidades', '')
    entidades = [e.strip() for e in entidades_raw.split(',') if e.strip()]

    where = []
    params = []
    if entidades:
        placeholders = ','.join('?' for _ in entidades)
        where.append(f"entidade IN ({placeholders})")
        params.extend(entidades)

    clause = (" WHERE " + " AND ".join(where)) if where else ""

    db = sqlite3.connect(DATABASE)
    rows = db.execute(f"""
        SELECT CAST(STRFTIME('%H', timestamp) AS INTEGER) as hora,
               ROUND(AVG(consumo_kwh), 3) as media
        FROM consumo_analise{clause}
        GROUP BY hora ORDER BY hora
    """, params).fetchall()
    db.close()

    return jsonify({
        'horas':  [r[0] for r in rows],
        'medias': [r[1] for r in rows],
    })


# =====================================================================
# PONTO DE ENTRADA
# =====================================================================

if __name__ == '__main__':
    init_db()
    init_analise_db()
    iniciar_simulacao()
    app.run(host='0.0.0.0', port=5000, debug=True)
