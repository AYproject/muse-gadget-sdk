"""Agent Plan protocol and credential provisioning checks against actual code."""
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools/muse'

class TtsTests(unittest.TestCase):
    def test_native_audio_envelope(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('host C compiler is not installed')
        cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
        if not (cjson / 'cJSON.c').exists():
            self.skipTest('build once to obtain cJSON')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'media.exe'
            subprocess.run([*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-I', str(cjson), '-I', str(ROOT / 'components/muse'),
                str(ROOT / 'tests/muse_reply_media_harness.c'),
                str(ROOT / 'components/muse/muse_reply_media.c'), str(cjson / 'cJSON.c'),
                '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_real_protocol_parser(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not cc or not shutil.which(cc[0]):
            self.skipTest('host C compiler is not installed')
        cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
        if not (cjson / 'cJSON.c').exists():
            self.skipTest('build once to obtain cJSON')
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / 'tts.exe'
            subprocess.run([*cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(cjson), '-I', str(ROOT / 'components/muse'),
                            str(ROOT / 'tests/muse_tts_protocol_harness.c'),
                            str(ROOT / 'components/muse/muse_tts_protocol.c'),
                            str(cjson / 'cJSON.c'), '-lm', '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def load_tool(self):
        sys.path.insert(0, str(TOOLS))
        self.addCleanup(sys.path.remove, str(TOOLS))
        spec = importlib.util.spec_from_file_location('xiao_tts', TOOLS / 'xiao147_tts.py')
        tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tool)
        return tool

    def test_invalid_config_does_not_open_serial_or_print_key(self):
        tool = self.load_tool()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'config.json'
            for config in [None, {'api_key': ''}, {'api_key': 'bad\nkey'},
                           {'api_key': 'synthetic', 'endpoint': 'https://another-host/'},
                           {'api_key': 'synthetic', 'speaker': 'voice\nvalue'}]:
                path.write_text(json.dumps(config), encoding='utf-8')
                with self.assertRaises(RuntimeError) as error:
                    tool.load_config(path)
                self.assertNotIn('synthetic', str(error.exception))

    def test_wrong_device_or_missing_feature_never_receives_key(self):
        tool = self.load_tool()
        writes = []
        tool.send_command = lambda *args: writes.append(args)
        tool.receive_status = lambda _: {'board': 'different board'}
        with self.assertRaises(RuntimeError): tool.configure(object(), 'synthetic', tool.DEFAULT_SPEAKER)
        tool.receive_status = lambda _: {'board': tool.BOARD_NAME, 'device': {}}
        with self.assertRaises(RuntimeError): tool.configure(object(), 'synthetic', tool.DEFAULT_SPEAKER)
        self.assertEqual(writes, [])

    def test_unacknowledged_step_stops_provisioning(self):
        tool = self.load_tool()
        writes = []
        tool.receive_status = lambda _: {'board': tool.BOARD_NAME, 'device': {'tts': {'supported': True}}}
        def fail(connection, name, value):
            writes.append(name)
            raise RuntimeError('no acknowledgement')
        tool.send_command = fail
        with self.assertRaises(RuntimeError): tool.configure(object(), 'synthetic', tool.DEFAULT_SPEAKER)
        self.assertEqual(writes, ['tts.speaker'])

if __name__ == '__main__': unittest.main()
