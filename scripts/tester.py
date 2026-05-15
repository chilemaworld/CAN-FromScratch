#!/usr/bin/env python3
import socket
import struct
import time

CAN_INTERFACE = 'vcan0'


def build_can_frame(can_id, data):
    """构造标准 CAN 帧。"""
    data_bytes = data.ljust(8, b'\x00')
    can_dlc = len(data)
    return struct.pack("<IB3x8s", can_id, can_dlc, data_bytes)


def parse_can_frame(frame):
    """解析 raw CAN 帧，返回 id、dlc 和数据。"""
    can_id, can_dlc, data = struct.unpack("<IB3x8s", frame)
    return can_id, can_dlc, data[:can_dlc]


def send_frame(sock, can_id, data):
    frame = build_can_frame(can_id, data)
    sock.send(frame)
    print(f"发送 -> ID=0x{can_id:X}, 数据={data.hex().upper()}")


def receive_frame(sock, timeout=1.0):
    sock.settimeout(timeout)
    try:
        frame = sock.recv(16)
        can_id, can_dlc, data = parse_can_frame(frame)
        print(f"接收 <- ID=0x{can_id:X}, 数据={data.hex().upper()}")
        return can_id, data
    except socket.timeout:
        return None, None


def main():
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_INTERFACE,))

    print('=== UDS 诊断测试启动 ===')

    # 1) 发送安全访问请求 0x27 01
    print('\n--- 发送安全访问请求 0x27 01 ---')
    send_frame(sock, 0x7E0, b'\x27\x01')
    receive_frame(sock, timeout=1.0)

    # 2) 发送一个多帧读取请求，读取 20 字节版本号数据
    print('\n--- 发送多帧读取请求 (20 字节) ---')
    payload = b'VERSION-2026-123456'  # 正好 20 字节
    assert len(payload) == 20

    first_frame = bytes([0x10 | ((len(payload) >> 8) & 0x0F), len(payload) & 0xFF]) + payload[:6]
    send_frame(sock, 0x7E0, first_frame)

    # 等待 ECU 的流控帧（如果有）
    receive_frame(sock, timeout=0.5)

    # 发送后续帧，编号从 1 开始
    remaining = payload[6:]
    for seq in range(1, 3):
        start = (seq - 1) * 7
        chunk = remaining[start:start + 7]
        consecutive_frame = bytes([0x20 | (seq & 0x0F)]) + chunk
        send_frame(sock, 0x7E0, consecutive_frame)
        time.sleep(0.1)

    print('\n--- 等待诊断程序打印长报文结果 ---')
    time.sleep(0.5)

    print('\n=== UDS 诊断测试结束 ===')


if __name__ == '__main__':
    main()
