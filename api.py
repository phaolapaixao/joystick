from flask import Flask, request, jsonify, render_template
from flask_cors import CORS
import sqlite3
from datetime import datetime
import logging

app = Flask(__name__)
CORS(app)  # Habilita CORS para todas as rotas

# Configuração do banco de dados
DATABASE = 'joystick_data.db'

def init_db():
    conn = sqlite3.connect(DATABASE)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS joystick_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME NOT NULL,
            x REAL NOT NULL,
            y REAL NOT NULL,
            button BOOLEAN NOT NULL,
            direction TEXT NOT NULL
        )
    ''')
    conn.commit()
    conn.close()

# Variável global para armazenar o último estado
joystick_data = {
    'x': 0.0,
    'y': 0.0,
    'button': False,
    'direction': 'Centro'
}

@app.route('/')
def serve_index():
    return render_template('index.html')

@app.route('/api/joystick', methods=['POST', 'GET', 'OPTIONS'])
def handle_joystick():
    if request.method == 'OPTIONS':
        # Resposta para pré-voo CORS
        response = jsonify({'status': 'success'})
        response.headers.add('Access-Control-Allow-Origin', '*')
        response.headers.add('Access-Control-Allow-Headers', '*')
        response.headers.add('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        return response
        
    global joystick_data

    if request.method == 'POST':
        try:
            data = request.get_json()
            if not data:
                app.logger.warning("Requisição POST sem dados JSON")
                return jsonify({'status': 'error', 'message': 'Nenhum dado JSON recebido'}), 400
                
            # Atualiza os dados
            joystick_data['x'] = data.get('x', joystick_data['x'])
            joystick_data['y'] = data.get('y', joystick_data['y'])
            
            # Processa o botão
            btn_val = data.get('button', joystick_data['button'])
            if isinstance(btn_val, str):
                joystick_data['button'] = btn_val.lower() == 'true'
            else:
                joystick_data['button'] = bool(btn_val)
                
            joystick_data['direction'] = data.get('direction', joystick_data['direction'])

            # Salva no banco de dados
            conn = sqlite3.connect(DATABASE)
            cursor = conn.cursor()
            cursor.execute('''
                INSERT INTO joystick_log (timestamp, x, y, button, direction)
                VALUES (?, ?, ?, ?, ?)
            ''', (datetime.now(), joystick_data['x'], joystick_data['y'], 
                  joystick_data['button'], joystick_data['direction']))
            conn.commit()
            conn.close()

            response = jsonify({'status': 'success', 'data': joystick_data})
            response.headers.add('Access-Control-Allow-Origin', '*')
            return response

        except Exception as e:
            app.logger.error(f"Erro no POST: {str(e)}")
            return jsonify({'status': 'error', 'message': str(e)}), 500

    elif request.method == 'GET':
        response = jsonify(joystick_data)
        response.headers.add('Access-Control-Allow-Origin', '*')
        return response

@app.route('/joystick/history', methods=['GET'])
def get_history():
    limit = request.args.get('limit', default=100, type=int)
    offset = request.args.get('offset', default=0, type=int)
    
    conn = sqlite3.connect(DATABASE)
    cursor = conn.cursor()
    
    cursor.execute('''
        SELECT timestamp, x, y, button, direction 
        FROM joystick_log 
        ORDER BY timestamp DESC
        LIMIT ? OFFSET ?
    ''', (limit, offset))
    
    rows = cursor.fetchall()
    conn.close()
    
    history = []
    for row in rows:
        history.append({
            'timestamp': row[0],
            'x': row[1],
            'y': row[2],
            'button': bool(row[3]),
            'direction': row[4]
        })
    
    response = jsonify(history)
    response.headers.add('Access-Control-Allow-Origin', '*')
    return response

if __name__ == '__main__':
    init_db()
    logging.basicConfig(level=logging.DEBUG)
    app.run(host='0.0.0.0', port=5000, debug=True)