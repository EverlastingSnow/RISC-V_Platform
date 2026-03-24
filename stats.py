import sqlite3
import threading
import os

DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stats.db")
_lock = threading.Lock()


def _get_connection():
    conn = sqlite3.connect(DB_FILE, check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL")
    return conn


def init_stats_db():
    with _lock:
        conn = _get_connection()
        cursor = conn.cursor()
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS usage_stats (
                id INTEGER PRIMARY KEY CHECK (id = 1),
                total_time INTEGER DEFAULT 0,
                visit_count INTEGER DEFAULT 0
            )
        """)
        cursor.execute("SELECT COUNT(*) FROM usage_stats")
        if cursor.fetchone()[0] == 0:
            cursor.execute("INSERT INTO usage_stats (id, total_time, visit_count) VALUES (1, 0, 0)")
        conn.commit()
        conn.close()


def get_stats():
    with _lock:
        conn = _get_connection()
        cursor = conn.cursor()
        cursor.execute("SELECT total_time, visit_count FROM usage_stats WHERE id = 1")
        row = cursor.fetchone()
        conn.close()
        if row:
            return {"total_time": row[0], "visit_count": row[1]}
        return {"total_time": 0, "visit_count": 0}


def increment_visit():
    with _lock:
        conn = _get_connection()
        cursor = conn.cursor()
        cursor.execute("UPDATE usage_stats SET visit_count = visit_count + 1 WHERE id = 1")
        conn.commit()
        conn.close()


def add_duration(seconds):
    with _lock:
        conn = _get_connection()
        cursor = conn.cursor()
        cursor.execute("UPDATE usage_stats SET total_time = total_time + ? WHERE id = 1", (seconds,))
        conn.commit()
        conn.close()


def reset_stats():
    with _lock:
        conn = _get_connection()
        cursor = conn.cursor()
        cursor.execute("UPDATE usage_stats SET total_time = 0, visit_count = 0 WHERE id = 1")
        conn.commit()
        conn.close()


if __name__ == "__main__":
    init_stats_db()
    print(get_stats())
    increment_visit()
    increment_visit()
    add_duration(125)
    print(get_stats())
    reset_stats()
    print(get_stats())
