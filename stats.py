import sqlite3
import threading
import os
import time
from datetime import datetime
from queue import Queue, Empty
from contextlib import contextmanager

DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stats.db")

_conn_local = threading.local()
_pending_duration = {}
_pending_lock = threading.Lock()
_flush_interval = 5
_last_flush = [time.time()]


def _get_connection():
    if not hasattr(_conn_local, 'conn') or _conn_local.conn is None:
        _conn_local.conn = sqlite3.connect(DB_FILE, check_same_thread=False, timeout=30.0)
        _conn_local.conn.execute("PRAGMA journal_mode=WAL")
        _conn_local.conn.execute("PRAGMA synchronous=NORMAL")
        _conn_local.conn.execute("PRAGMA cache_size=10000")
    return _conn_local.conn


def _close_connection():
    if hasattr(_conn_local, 'conn') and _conn_local.conn:
        _conn_local.conn.close()
        _conn_local.conn = None


def _flush_pending_duration():
    with _pending_lock:
        if not _pending_duration:
            return
        pending = _pending_duration.copy()
        _pending_duration.clear()

    conn = _get_connection()
    cursor = conn.cursor()
    try:
        for date_str, duration in pending.items():
            cursor.execute("""
                INSERT INTO daily_stats (date, total_time, visit_count)
                VALUES (?, ?, 0)
                ON CONFLICT(date) DO UPDATE SET
                    total_time = total_time + excluded.total_time
            """, (date_str, duration))
        conn.commit()
    except Exception as e:
        print(f"[STATS] Flush error: {e}")


def init_stats_db():
    conn = sqlite3.connect(DB_FILE, check_same_thread=False, timeout=30.0)
    cursor = conn.cursor()
    cursor.execute("PRAGMA journal_mode=WAL")
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS daily_stats (
            date TEXT PRIMARY KEY,
            total_time INTEGER DEFAULT 0,
            visit_count INTEGER DEFAULT 0
        )
    """)
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


def get_daily_stats(start_date=None, end_date=None):
    if time.time() - _last_flush[0] > _flush_interval:
        _flush_pending_duration()
        _last_flush[0] = time.time()

    conn = _get_connection()
    cursor = conn.cursor()

    if start_date and end_date:
        cursor.execute("""
            SELECT date, total_time, visit_count
            FROM daily_stats
            WHERE date >= ? AND date <= ?
            ORDER BY date DESC
        """, (start_date, end_date))
    elif start_date:
        cursor.execute("""
            SELECT date, total_time, visit_count
            FROM daily_stats
            WHERE date >= ?
            ORDER BY date DESC
        """, (start_date,))
    elif end_date:
        cursor.execute("""
            SELECT date, total_time, visit_count
            FROM daily_stats
            WHERE date <= ?
            ORDER BY date DESC
        """, (end_date,))
    else:
        cursor.execute("""
            SELECT date, total_time, visit_count
            FROM daily_stats
            ORDER BY date DESC
        """)

    rows = cursor.fetchall()

    daily_list = [{"date": row[0], "total_time": row[1], "visit_count": row[2]} for row in rows]

    total_time = sum(d["total_time"] for d in daily_list)
    total_visits = sum(d["visit_count"] for d in daily_list)

    return {
        "total_time": total_time,
        "visit_count": total_visits,
        "daily": daily_list
    }


def get_stats():
    return get_daily_stats()


def _update_daily_stats(date_str, duration=0, visit_increment=1):
    conn = _get_connection()
    cursor = conn.cursor()
    if visit_increment > 0:
        cursor.execute("""
            INSERT INTO daily_stats (date, total_time, visit_count)
            VALUES (?, ?, ?)
            ON CONFLICT(date) DO UPDATE SET
                visit_count = visit_count + excluded.visit_count
        """, (date_str, duration, visit_increment))
        conn.commit()
    else:
        with _pending_lock:
            _pending_duration[date_str] = _pending_duration.get(date_str, 0) + duration


def increment_visit():
    date_str = datetime.now().strftime("%Y-%m-%d")
    _update_daily_stats(date_str, 0, 1)


def add_duration(seconds):
    date_str = datetime.now().strftime("%Y-%m-%d")
    _update_daily_stats(date_str, seconds, 0)


def reset_stats():
    _flush_pending_duration()
    conn = _get_connection()
    cursor = conn.cursor()
    cursor.execute("UPDATE usage_stats SET total_time = 0, visit_count = 0 WHERE id = 1")
    cursor.execute("DELETE FROM daily_stats")
    conn.commit()


def flush_all():
    _flush_pending_duration()


if __name__ == "__main__":
    init_stats_db()
    print(get_stats())
    increment_visit()
    increment_visit()
    add_duration(125)
    flush_all()
    print(get_stats())
    print(get_daily_stats("2025-01-01", "2025-12-31"))
    reset_stats()
    print(get_stats())