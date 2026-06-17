"""
访问统计模块。

使用 SQLite 持久化记录每日访问次数与累计使用时长。
采用线程局部连接 + 时长批量缓冲策略，减少高并发场景下的写盘压力。
"""

import sqlite3
import threading
import os
import time
from datetime import datetime
from queue import Queue, Empty
from contextlib import contextmanager

# SQLite 数据库文件路径：与本模块同目录下的 stats.db
DB_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stats.db")

# 线程局部连接对象，确保每个线程复用独立连接（避免跨线程共享 sqlite3 连接）
_conn_local = threading.local()
# 待刷新的累计时长缓冲：key 为日期字符串，value 为该日期累计秒数
_pending_duration = {}
# 保护 _pending_duration 的并发写锁
_pending_lock = threading.Lock()
# 自动刷新间隔（秒）：超过该时间会在下次查询时触发 flush
_flush_interval = 5
# 上次刷新时间戳（用列表实现可变闭包）
_last_flush = [time.time()]


def _get_connection():
    """
    获取当前线程的 SQLite 连接（懒加载）。

    若当前线程尚未创建连接，则新建一个，并启用 WAL 模式与常用性能 PRAGMA。
    所有调用方需自行管理连接的关闭（通过 _close_connection）。

    Returns:
        sqlite3.Connection: 当前线程复用的数据库连接。
    """
    if not hasattr(_conn_local, 'conn') or _conn_local.conn is None:
        # check_same_thread=False 允许多线程共享连接对象，但通过线程局部避免真共享
        _conn_local.conn = sqlite3.connect(DB_FILE, check_same_thread=False, timeout=30.0)
        # WAL 模式提升并发读写性能
        _conn_local.conn.execute("PRAGMA journal_mode=WAL")
        # 降低同步级别为 NORMAL（牺牲部分崩溃安全性换取写入性能）
        _conn_local.conn.execute("PRAGMA synchronous=NORMAL")
        # 增加缓存页数，减少磁盘 IO
        _conn_local.conn.execute("PRAGMA cache_size=10000")
    return _conn_local.conn


def _close_connection():
    """关闭当前线程持有的 SQLite 连接并清空线程局部引用。"""
    if hasattr(_conn_local, 'conn') and _conn_local.conn:
        _conn_local.conn.close()
        _conn_local.conn = None


def _flush_pending_duration():
    """
    将内存中累积的时长数据批量写入数据库。

    使用 _pending_lock 短暂取走缓冲并清空，随后通过一次事务完成所有 upsert。
    写入失败仅打印日志，不抛出异常（保证调用方不被统计错误影响主流程）。
    """
    with _pending_lock:
        if not _pending_duration:
            return
        # 拷贝后立即清空，避免持锁 IO
        pending = _pending_duration.copy()
        _pending_duration.clear()

    conn = _get_connection()
    cursor = conn.cursor()
    try:
        for date_str, duration in pending.items():
            # upsert 语义：日期不存在则插入，存在则累加 total_time
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
    """
    初始化统计数据库（创建表结构）。

    应在服务启动时调用一次：
    - daily_stats：按日期维度记录访问次数与累计时长
    - usage_stats：全局累计统计（单行表，id 固定为 1）
    """
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
    # 若 usage_stats 为空则插入初始占位行（保证后续 UPDATE 一定能命中）
    cursor.execute("SELECT COUNT(*) FROM usage_stats")
    if cursor.fetchone()[0] == 0:
        cursor.execute("INSERT INTO usage_stats (id, total_time, visit_count) VALUES (1, 0, 0)")
    conn.commit()
    conn.close()


def get_daily_stats(start_date=None, end_date=None):
    """
    查询指定日期范围的每日统计数据。

    调用前会自动判断是否需要 flush 时长缓冲（避免长时间无 flush 导致数据滞后）。

    Args:
        start_date (str, optional): 起始日期（YYYY-MM-DD），None 表示不限制下限。
        end_date (str, optional): 结束日期（YYYY-MM-DD），None 表示不限制上限。

    Returns:
        dict: 包含 total_time（区间总时长）、
              visit_count（区间总访问次数）、
              daily（每日明细列表，按日期倒序）。
    """
    # 超过刷新间隔则触发一次批量写入
    if time.time() - _last_flush[0] > _flush_interval:
        _flush_pending_duration()
        _last_flush[0] = time.time()

    conn = _get_connection()
    cursor = conn.cursor()

    # 根据入参组合拼接不同 SQL，避免无效条件
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

    # 将元组列表转为结构化字典列表
    daily_list = [{"date": row[0], "total_time": row[1], "visit_count": row[2]} for row in rows]

    # 聚合区间内总和
    total_time = sum(d["total_time"] for d in daily_list)
    total_visits = sum(d["visit_count"] for d in daily_list)

    return {
        "total_time": total_time,
        "visit_count": total_visits,
        "daily": daily_list
    }


def get_stats():
    """
    获取全量每日统计（无日期过滤）。

    Returns:
        dict: 同 get_daily_stats() 返回结构。
    """
    return get_daily_stats()


def _update_daily_stats(date_str, duration=0, visit_increment=1):
    """
    内部更新函数：写入访问次数与/或累加时长。

    访问次数直接同步写入（要求实时性高），
    时长数据先放入内存缓冲、由 _flush_pending_duration 批量落盘。

    Args:
        date_str (str): 目标日期（YYYY-MM-DD）。
        duration (int): 待累加的时长（秒），默认 0。
        visit_increment (int): 访问次数增量，大于 0 时立即写入；否则忽略。
    """
    conn = _get_connection()
    cursor = conn.cursor()
    if visit_increment > 0:
        # 访问次数即时 upsert
        cursor.execute("""
            INSERT INTO daily_stats (date, total_time, visit_count)
            VALUES (?, ?, ?)
            ON CONFLICT(date) DO UPDATE SET
                visit_count = visit_count + excluded.visit_count
        """, (date_str, duration, visit_increment))
        conn.commit()
    else:
        # 时长走内存缓冲路径，避免频繁事务
        with _pending_lock:
            _pending_duration[date_str] = _pending_duration.get(date_str, 0) + duration


def increment_visit():
    """
    记录一次访问（以当天日期为 key，visit_count +1）。"""
    date_str = datetime.now().strftime("%Y-%m-%d")
    _update_daily_stats(date_str, 0, 1)


def add_duration(seconds):
    """
    为当天累加使用时长（秒）。数据先写入内存缓冲，由后续 flush 落盘。

    Args:
        seconds (int): 时长增量（秒）。
    """
    date_str = datetime.now().strftime("%Y-%m-%d")
    _update_daily_stats(date_str, seconds, 0)


def reset_stats():
    """
    重置全部统计数据：清空 daily_stats 全部行并把 usage_stats 归零。"""
    # 先把内存中的时长落盘，避免被一并清空
    _flush_pending_duration()
    conn = _get_connection()
    cursor = conn.cursor()
    cursor.execute("UPDATE usage_stats SET total_time = 0, visit_count = 0 WHERE id = 1")
    cursor.execute("DELETE FROM daily_stats")
    conn.commit()


def flush_all():
    """手动触发一次时长缓冲落盘（通常在服务关闭或会话结束时调用）。"""
    _flush_pending_duration()


if __name__ == "__main__":
    # 命令行直接运行：演示完整统计生命周期
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