#!/usr/bin/env python3
from flask import Flask, jsonify, request, send_file, send_from_directory
from flask_cors import CORS
import psycopg2
import psycopg2.extras
import os

app = Flask(__name__)
CORS(app)

@app.route('/')
def index():
	return send_from_directory('/home/bianka/access_server', 'admin.html')

def get_conn():
    return psycopg2.connect(
        dbname="access_control",
        user="bianka",
        password="raspberry",
        host="localhost"
    )

# logs
@app.route('/api/logs')
def get_logs():
    conn = get_conn()
    cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
    cur.execute("""
        SELECT l.id, l.uid, l.timestamp, l.result, u.name
        FROM logs l
        LEFT JOIN users u ON l.uid = u.uid
        ORDER BY l.timestamp DESC
        LIMIT 200
    """)
    rows = cur.fetchall()
    cur.close()
    conn.close()
    return jsonify([dict(r) for r in rows])

# videos
@app.route('/api/videos')
def get_videos():
    conn = get_conn()
    cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
    cur.execute("""
        SELECT v.id, v.uid, v.file_path, v.recorded_at, v.duration_seconds, v.result, v.faces_count, u.name
        FROM videos v
        LEFT JOIN users u ON v.uid = u.uid
        ORDER BY v.recorded_at DESC
        LIMIT 200
    """)
    rows = cur.fetchall()
    cur.close()
    conn.close()
    return jsonify([dict(r) for r in rows])

@app.route('/api/videos/<int:video_id>/stream')
def stream_video(video_id):
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("SELECT file_path FROM videos WHERE id = %s", (video_id,))
    row = cur.fetchone()
    cur.close()
    conn.close()
    if not row or not os.path.exists(row[0]):
        return jsonify({'error': 'Video not found'}), 404
    return send_file(row[0], mimetype='video/mp4')

# users
@app.route('/api/users')
def get_users():
    conn = get_conn()
    cur = conn.cursor(cursor_factory=psycopg2.extras.RealDictCursor)
    cur.execute("SELECT * FROM users ORDER BY id")
    rows = cur.fetchall()
    cur.close()
    conn.close()
    return jsonify([dict(r) for r in rows])

@app.route('/api/users', methods=['POST'])
def add_user():
    data = request.json
    uid = data.get('uid', '').strip()
    name = data.get('name', '').strip()
    if not uid or not name:
        return jsonify({'error': 'uid and name required'}), 400
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("INSERT INTO users (uid, name, active) VALUES (%s, %s, true)", (uid, name))
    conn.commit()
    cur.close()
    conn.close()
    return jsonify({'ok': True})

@app.route('/api/users/<int:user_id>', methods=['DELETE'])
def delete_user(user_id):
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("DELETE FROM users WHERE id = %s", (user_id,))
    conn.commit()
    cur.close()
    conn.close()
    return jsonify({'ok': True})

@app.route('/api/users/<int:user_id>/toggle', methods=['POST'])
def toggle_user(user_id):
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("UPDATE users SET active = NOT active WHERE id = %s", (user_id,))
    conn.commit()
    cur.close()
    conn.close()
    return jsonify({'ok': True})

# stats, dashboard
@app.route('/api/stats')
def get_stats():
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("SELECT COUNT(*) FROM logs WHERE result = 'GRANTED'")
    granted = cur.fetchone()[0]
    cur.execute("SELECT COUNT(*) FROM logs WHERE result = 'DENIED'")
    denied = cur.fetchone()[0]
    cur.execute("SELECT COUNT(*) FROM users WHERE active = true")
    active_users = cur.fetchone()[0]
    cur.execute("SELECT COUNT(*) FROM videos")
    total_videos = cur.fetchone()[0]
    cur.execute("""
        SELECT DATE(timestamp) as day, COUNT(*) as count
        FROM logs
        WHERE timestamp > NOW() - INTERVAL '7 days'
        GROUP BY day ORDER BY day
    """)
    daily = [{'day': str(r[0]), 'count': r[1]} for r in cur.fetchall()]
    cur.close()
    conn.close()
    return jsonify({
        'granted': granted,
        'denied': denied,
        'active_users': active_users,
        'total_videos': total_videos,
        'daily': daily
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
