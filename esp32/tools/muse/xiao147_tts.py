#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Provision Agent Plan TTS locally over USB without echoing the API key."""
import argparse
import json
from pathlib import Path
import re
import sys
import serial

from xiao147_wifi import BOARD_NAME, receive_status, send_command

ENDPOINT = 'https://openspeech.bytedance.com/api/v3/plan/tts/unidirectional'
DEFAULT_SPEAKER = 'zh_female_vv_uranus_bigtts'

def load_config(path):
    try:
        settings = json.loads(Path(path).read_text(encoding='utf-8-sig'))
    except (OSError, ValueError):
        raise RuntimeError('无法读取 TTS 配置，请检查本地 JSON 文件。') from None
    if not isinstance(settings, dict):
        raise RuntimeError('TTS 配置必须是 JSON 对象。')
    key, speaker = settings.get('api_key'), settings.get('speaker', DEFAULT_SPEAKER)
    if not isinstance(key, str) or not 1 <= len(key) <= 256 or any(not 33 <= ord(c) <= 126 for c in key):
        raise RuntimeError('请在本地 api_key 中填写 Agent Plan 专属 API Key（1–256 个可打印 ASCII 字符，不能含空格或换行）。')
    if not isinstance(speaker, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,95}', speaker):
        raise RuntimeError('音色 ID 格式无效。')
    if settings.get('endpoint', ENDPOINT) != ENDPOINT:
        raise RuntimeError('请使用购买的 Agent Plan /plan/ 接口。')
    return key, speaker

def checked_device(connection):
    status = receive_status(connection)
    if status.get('board') != BOARD_NAME:
        raise RuntimeError('所选串口不是 XIAO 1.47 Muse 固件，未发送密钥。')
    if not status.get('device', {}).get('tts', {}).get('supported'):
        raise RuntimeError('当前固件尚未支持 TTS 配置，请先刷写声音播报版。')
    return status

def set_reply_mode(connection, mode):
    checked_device(connection)
    send_command(connection, 'tts.reply_text', '1' if mode == 'text' else '0')
    status = receive_status(connection)
    if status.get('device', {}).get('tts', {}).get('reply_text') is not (mode == 'text'):
        raise RuntimeError('设备未确认回复请求模式，请更新固件后重试。')

def configure(connection, key, speaker):
    checked_device(connection)
    send_command(connection, 'tts.speaker', speaker)
    send_command(connection, 'tts.reply_text', '1')
    send_command(connection, 'tts.key', key)
    status = receive_status(connection)
    if not status.get('device', {}).get('tts', {}).get('configured'):
        raise RuntimeError('设备没有确认 TTS 配置，请检查本地密钥和设备日志。')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    entry = parser.add_mutually_exclusive_group(required=True)
    entry.add_argument('--config', type=Path)
    entry.add_argument('--reply-mode', choices=['text', 'native'])
    args = parser.parse_args()
    key, speaker = load_config(args.config) if args.config else (None, None)
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1, write_timeout=3)
    connection.dtr = connection.rts = False
    connection.port = args.port
    try:
        connection.open()
        if args.reply_mode:
            set_reply_mode(connection, args.reply_mode)
            print('回复请求模式已切换；TTS 密钥、配对和 Wi-Fi 保留。')
            return 0
        configure(connection, key, speaker)
        print('Agent Plan TTS 已保存到设备；密钥未输出，Wi-Fi 和 Muse 配对保留。')
        print('请按住 USR1 提问，松开后等待回复播报。')
        return 0
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
