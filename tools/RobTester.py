# robot_sim.py
# -*- coding: utf-8 -*-
"""
模拟机器人 TCP 服务端
  IP   : 192.168.105.1
  PORT : 9200
协议: 4 字节大端长度前缀 + 数据体
心跳: 收到 "PING" 回复 "PONG"
"""

import socket
import struct
import threading
import time
import json
import sys

ROBOT_IP   = "192.168.105.10"
ROBOT_PORT = 9200

# ---------- 组包 / 拆包 ----------
def pack(payload: bytes) -> bytes:
    """4字节大端长度 + 数据体"""
    return struct.pack(">I", len(payload)) + payload

def recv_exact(sock: socket.socket, n: int) -> bytes:
    """从 socket 精确读 n 字节，连接关闭返回 b''"""
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return b""
        buf.extend(chunk)
    return bytes(buf)

def recv_packet(sock: socket.socket):
    """读一个完整包，连接断开返回 None"""
    head = recv_exact(sock, 4)
    if not head:
        return None
    (length,) = struct.unpack(">I", head)
    if length == 0:
        return b""
    body = recv_exact(sock, length)
    if not body:
        return None
    return body

# ---------- 单个客户端线程 ----------
def handle_client(conn: socket.socket, addr):
    print(f"[+] 客户端接入: {addr}")
    # 模拟：连接后先推一条 hello，验证下行链路
    try:
        hello = json.dumps({
            "type": "hello",
            "robot_id": "SIM-001",
            "ip": ROBOT_IP,
            "ts": int(time.time() * 1000),
        }).encode("GBK")
        conn.sendall(pack(hello))
    except Exception as e:
        print(f"[!] 发送 hello 失败: {e}")

    try:
        while True:
            data = recv_packet(conn)
            if data is None:
                print(f"[-] 客户端断开: {addr}")
                break

            # ---- 心跳 ----
            if data == b"PING":
                conn.sendall(pack(b"PONG"))
                print(f"[<] PING  ->  PONG  ({addr})")
                continue

            # ---- 业务数据 ----
            print(f"[<] 收到 {len(data)} 字节: {data[:80]!r}")

            # 尝试按 JSON 解析并回显一个 ack
            try:
                msg = json.loads(data.decode("utf-8"))
                cmd = msg.get("cmd", "unknown")
                ack = json.dumps({
                    "type": "ack",
                    "cmd": cmd,
                    "ok": True,
                    "ts": int(time.time() * 1000),
                }).encode("utf-8")
            except Exception:
                ack = json.dumps({
                    "type": "ack",
                    "ok": True,
                    "size": len(data),
                    "ts": int(time.time() * 1000),
                }).encode("utf-8")

            conn.sendall(pack(ack))

    except ConnectionResetError:
        print(f"[!] 连接被重置: {addr}")
    except Exception as e:
        print(f"[!] 客户端线程异常 {addr}: {e}")
    finally:
        try:
            conn.close()
        except Exception:
            pass
        print(f"[-] 连接关闭: {addr}")

def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        srv.bind((ROBOT_IP, ROBOT_PORT))
    except OSError as e:
        print(f"绑定 {ROBOT_IP}:{ROBOT_PORT} 失败: {e}")
        print("请确认:")
        print(f"  1. 本机是否有 {ROBOT_IP} 这个地址 (ipconfig / ip addr)")
        print(f"  2. 是否有权限绑定{ROBOT_PORT}端口")
        sys.exit(1)

    srv.listen(8)
    print(f"[*] 机器人已启动，监听 {ROBOT_IP}:{ROBOT_PORT}")

    try:
        while True:
            conn, addr = srv.accept()
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
            t.start()
    except KeyboardInterrupt:
        print("\n[*] 手动退出")
    finally:
        srv.close()

if __name__ == "__main__":
    main()