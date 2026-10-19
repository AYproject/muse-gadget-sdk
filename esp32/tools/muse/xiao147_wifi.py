#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Configure XIAO 1.47 Wi-Fi through Muse's existing USB setup commands."""
import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial

BOARD_NAME = 'Seeed XIAO ESP32-S3 1.47 Touch Display'


def receive_status(connection, timeout=5):
    connection.write(b'>status\n')
    deadline = time.monotonic() + timeout
    received = b''
    while time.monotonic() < deadline:
        received += connection.read(connection.in_waiting or 1)
        # Decode the accumulated bytes so a USB read splitting a Chinese SSID
        # between UTF-8 bytes does not replace its characters.
        for match in re.finditer(r'@status (\{[^\r\n]+\})', received.decode('utf-8', errors='replace')):
            try:
                return json.loads(match.group(1))
            except ValueError:
                continue
        received = received[-8192:]
    raise RuntimeError('设备未回应 USB 状态查询，请确认固件已启动，且其他串口窗口已关闭。')


def send_command(connection, name, value=None):
    command = '>' + name + ('' if value is None else '=' + value) + '\n'
    connection.write(command.encode('utf-8'))
    # The USB console may carry status replies without ESP_LOGI output. Commands
    # and the following status query run in order on the same console task; use
    # the firmware's explicit last-command acknowledgement, never a log echo.
    status = receive_status(connection)
    if status.get('device', {}).get('last') == name + ': ok':
        return
    raise RuntimeError('设备未确认配置步骤 ' + name + '，已停止后续操作。')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--config', type=Path, required=True)
    arguments = parser.parse_args()
    try:
        settings = json.loads(arguments.config.read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        raise RuntimeError('无法读取 Wi-Fi 配置，请检查本地 JSON 文件。') from None
    if not isinstance(settings, dict):
        raise RuntimeError('Wi-Fi 配置必须包含 ssid 和 password。')
    ssid, password = settings.get('ssid'), settings.get('password')
    if not isinstance(ssid, str) or not 1 <= len(ssid.encode('utf-8')) <= 32 or any(c in ssid for c in '\r\n\0'):
        raise RuntimeError('请填写 Wi-Fi 名称，长度须为 1–32 个 UTF-8 字节，且不能含换行。')
    if not isinstance(password, str) or len(password.encode('utf-8')) > 64 or any(c in password for c in '\r\n\0'):
        raise RuntimeError('Wi-Fi 密码须为字符串，不超过 64 个 UTF-8 字节，且不能含换行。')
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
    connection.dtr = False
    connection.rts = False
    connection.port = arguments.port
    try:
        connection.open()
        status = receive_status(connection)
        if status.get('board') != BOARD_NAME:
            raise RuntimeError('所选串口没有识别为 XIAO 1.47 Muse 固件，未发送配置。')
        send_command(connection, 'wifi.ssid', ssid)
        send_command(connection, 'wifi.pass', password)
        send_command(connection, 'wifi.connect')
        password = None
        print('Wi-Fi 配置已发送，正在等待设备连接；Muse 配对信息保留。', flush=True)
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            status = receive_status(connection)
            wifi = status.get('device', {}).get('wifi', {})
            if wifi.get('state') == 'connected' and wifi.get('ssid') == ssid:
                print('已连接 Wi-Fi：' + ssid + '，IP：' + str(wifi.get('ip', '')))
                print('局域网连接成功；Muse 服务能否访问仍需通过对话验证。')
                return 0
            time.sleep(1)
        print('配置已保存，但尚未确认连接到目标 Wi-Fi。请检查热点频段、名称和密码。')
        return 1
    finally:
        connection.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
    except serial.SerialException:
        print('无法访问设备串口，请关闭其他串口程序并检查 USB 连接。', file=sys.stderr)
        sys.exit(1)
