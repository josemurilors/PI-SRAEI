import sqlite3
import threading
import time
import math
from flask import Flask, render_template, jsonify, request, g

app = Flask(__name__)
DATABASE = 'sraei.db'
PRICE_PER_KWH_DEFAULT = 0.85
SIMULATION_INTERVAL = 1.0

t_start = time.time()
energia_kwh = 0.0
lock = threading.Lock()

def get_db():
    db = getattr(g, '_database', None)
    if db is None:
        db = g._database = sqlite3.connect(DATABASE)
        db.row_factory = sqlite3.Row
    return db

@app.teardown_appcontext
def close_connection(exception):
    db = getattr(g, '_database', None)
    if db is not None:
        db.close()

def init_db():
    with app.app_context():
        db = get_db()
        cursor = db.cursor()
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
            CREATE TABLE IF NOT EXISTS readings (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp REAL,
                corrente REAL,
                potencia REAL,
                energia REAL
            )
        """)
        db.commit()

def get_price_per_kwh():
    db = get_db()
    cursor = db.execute("SELECT value FROM config WHERE key = ?", ('price_per_kwh',))
    row = cursor.fetchone()
    db.close()
    return float(row[0]) if row else PRICE_PER_KWH_DEFAULT

def set_price_per_kwh(value):
    db = get_db()
    db.execute("UPDATE config SET value = ? WHERE key = ?", (str(value), 'price_per_kwh'))
    db.commit()

def simulate_current_rms(t):
    corrente = 8.0 + 6.0 * math.sin(2.0 * math.pi * 0.05 * t)
    if corrente < 0:
        corrente = 0.0
    return corrente

def background_thread():
    global energia_kwh
    while True:
        time.sleep(SIMULATION_INTERVAL)
        t = time.time() - t_start
        corrente = simulate_current_rms(t)
        tensao_rede = 127.0
        potencia = corrente * tensao_rede
        dt_horas = SIMULATION_INTERVAL / 3600.0
        with lock:
            energia_kwh += (potencia * dt_horas) / 1000.0
        db = sqlite3.connect(DATABASE)
        db.execute("""
            INSERT INTO readings (timestamp, corrente, potencia, energia)
            VALUES (?, ?, ?, ?)
        """, (time.time(), corrente, potencia, energia_kwh))
        db.execute("""
            DELETE FROM readings WHERE id NOT IN (
                SELECT id FROM readings ORDER BY id DESC LIMIT 200
            )
        """)
        db.commit()
        db.close()

# Remover a inicialização automática da thread de simulação
# threading.Thread(target=background_thread, daemon=True).start()

@app.route('/')
def index():
    return render_template('simple_index.html')

@app.route('/api/data')
def api_data():
    # Sempre usa dados reais do banco, sem simulação
    db = sqlite3.connect(DATABASE)
    last_reading = db.execute("""
        SELECT corrente, potencia, energia FROM readings
        ORDER BY id DESC LIMIT 1
    """).fetchone()
    db.close()

    if last_reading:
        # Dados reais do ESP32
        corrente = last_reading[0]
        potencia = last_reading[1]
        energia = last_reading[2]
    else:
        # Valores padrão quando não há dados
        corrente = 0.0
        potencia = 0.0
        energia = 0.0

    price = get_price_per_kwh()
    custo = energia * price

    # Busca histórico para o gráfico
    db = sqlite3.connect(DATABASE)
    rows = db.execute("""
        SELECT timestamp, corrente FROM readings
        ORDER BY timestamp DESC LIMIT 50
    """).fetchall()
    db.close()

    timestamps = [r[0] for r in reversed(rows)]
    correntes = [r[1] for r in reversed(rows)]
    if timestamps:
        base = timestamps[0]
        labels = [f"{int(ts - base)}s" for ts in timestamps]
    else:
        labels = []
        correntes = []
    return jsonify({
        'corrente': round(corrente, 3),
        'potencia': round(potencia, 2),
        'energia': round(energia, 6),
        'custo': round(custo, 4),
        'price_per_kwh': price,
        'chart_labels': labels,
        'chart_data': correntes
    })

@app.route('/api/esp32', methods=['POST'])
def api_esp32():
    """Recebe dados do ESP32 via HTTP POST"""
    data = request.get_json()
    if not data:
        return jsonify({'error': 'No data received'}), 400

    corrente = data.get('corrente')
    potencia = data.get('potencia')
    energia = data.get('energia')

    if corrente is None or potencia is None or energia is None:
        return jsonify({'error': 'Missing fields'}), 400

    # Salva no banco
    db = sqlite3.connect(DATABASE)
    db.execute("""
        INSERT INTO readings (timestamp, corrente, potencia, energia)
        VALUES (?, ?, ?, ?)
    """, (time.time(), corrente, potencia, energia))

    # Mantém apenas últimas 200 leituras
    db.execute("""
        DELETE FROM readings WHERE id NOT IN (
            SELECT id FROM readings ORDER BY id DESC LIMIT 200
        )
    """)
    db.commit()
    db.close()

    return jsonify({'success': True})

@app.route('/api/price', methods=['POST'])
def api_price():
    data = request.get_json()
    if not data or 'price' not in data:
        return jsonify({'error': 'Missing price'}), 400
    try:
        price = float(data['price'])
        if price < 0:
            raise ValueError
    except ValueError:
        return jsonify({'error': 'Invalid price'}), 400
    set_price_per_kwh(price)
    return jsonify({'success': True, 'price': price})

@app.route('/api/reset', methods=['POST'])
def api_reset():
    global energia_kwh
    with lock:
        energia_kwh = 0.0
    db = sqlite3.connect(DATABASE)
    db.execute("DELETE FROM readings")
    db.commit()
    db.close()
    return jsonify({'success': True})

if __name__ == '__main__':
    init_db()
    app.run(host='0.0.0.0', port=5000, debug=True)
