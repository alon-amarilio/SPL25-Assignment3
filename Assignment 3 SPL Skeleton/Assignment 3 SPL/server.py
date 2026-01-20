#!/usr/bin/env python3
import socket
import sys
import threading
import sqlite3
import os

SERVER_NAME = "STOMP_PYTHON_SQL_SERVER"
DB_FILE = "stomp_server.db"

def recv_null_terminated(sock: socket.socket) -> str:
    data = b""
    while True:
        chunk = sock.recv(1024)
        if not chunk:
            return ""
        data += chunk
        if b"\0" in data:
            msg, _ = data.split(b"\0", 1)
            return msg.decode("utf-8", errors="replace")

def init_database():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS users (
            username TEXT PRIMARY KEY,
            password TEXT NOT NULL
        )
    """)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS logins (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL,
            login_time INTEGER NOT NULL,
            logout_time INTEGER,
            FOREIGN KEY(username) REFERENCES users(username)
        )
    """)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS reports (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL,
            file_name TEXT NOT NULL,
            time INTEGER NOT NULL,
            FOREIGN KEY(username) REFERENCES users(username)
        )
    """)
    conn.commit()
    conn.close()
    print(f"[{SERVER_NAME}] Database initialized successfully.")

def execute_sql_command(sql_command: str) -> str:
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute(sql_command)
        conn.commit()
        conn.close()
        return "success"
    except sqlite3.Error as e:
        return f"error: {e}"

def execute_sql_query(sql_query: str) -> str:
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute(sql_query)
        rows = cursor.fetchall()
        conn.close()
        if not rows:
            return ""
        result_str = ""
        for row in rows:
            row_str = " ".join([str(item) for item in row])
            result_str += row_str + "\n"
        return result_str.strip()
    except sqlite3.Error as e:
        return f"error: {e}"

def handle_client(client_socket: socket.socket, addr):
    print(f"[{SERVER_NAME}] Client connected from {addr}")
    try:
        while True:
            message = recv_null_terminated(client_socket)
            if message == "":
                break
            print(f"[{SERVER_NAME}] Received SQL: {message}")
            response = ""
            command_upper = message.strip().upper()
            if command_upper.startswith("SELECT"):
                response = execute_sql_query(message)
            else:
                response = execute_sql_command(message)
            client_socket.sendall(response.encode("utf-8") + b"\0")
    except Exception as e:
        print(f"[{SERVER_NAME}] Error handling client {addr}: {e}")
    finally:
        try:
            client_socket.close()
        except Exception:
            pass
        print(f"[{SERVER_NAME}] Client {addr} disconnected")

def start_server(host="127.0.0.1", port=7778):
    init_database()
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server_socket.bind((host, port))
        server_socket.listen(5)
        print(f"[{SERVER_NAME}] Server started on {host}:{port}")
        print(f"[{SERVER_NAME}] Waiting for connections...")
        while True:
            client_socket, addr = server_socket.accept()
            t = threading.Thread(target=handle_client, args=(client_socket, addr), daemon=True)
            t.start()
    except KeyboardInterrupt:
        print(f"\n[{SERVER_NAME}] Shutting down server...")
    finally:
        try:
            server_socket.close()
        except Exception:
            pass

if __name__ == "__main__":
    port = 7778
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
    start_server(port=port)
