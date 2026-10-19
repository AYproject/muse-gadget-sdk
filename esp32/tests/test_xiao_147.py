"""Exercise XIAO touch decoding, codec lifecycle and USB selection."""
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Xiao147Tests(unittest.TestCase):
    def test_speaker_codec_lifecycle(self):
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or not shutil.which(cc[0]):
            self.skipTest("host C compiler is not installed")
        codec = ROOT / "managed_components/espressif__esp_codec_dev"
        if not (codec / "esp_codec_dev.c").exists():
            self.skipTest("build once to obtain the real esp_codec_dev component")
        source = (ROOT / "components/muse/boards/board_seeed_xiao_s3_147.c").read_text()
        callbacks = source[source.index("static int spk_enable("):source.index("static esp_err_t audio_init(")]
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / "board_speaker_callbacks.inc").write_text(callbacks)
            (directory / "esp_err.h").write_text(
                "typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
                "#define ESP_ERR_NO_MEM 0x101\n#define ESP_ERR_INVALID_ARG 0x102\n"
                "#define ESP_ERR_INVALID_STATE 0x103\n#define ESP_ERR_NOT_FOUND 0x105\n"
                "#define ESP_ERR_NOT_SUPPORTED 0x106\n")
            (directory / "esp_log.h").write_text(
                "#include <stdlib.h>\n#include <stdio.h>\n"
                "#define ESP_LOGE(tag, ...) do { (void)(tag); if (0) fprintf(stderr, __VA_ARGS__); } while (0)\n"
                "#define ESP_LOGI(tag, ...) do { (void)(tag); if (0) fprintf(stderr, __VA_ARGS__); } while (0)\n")
            executable = directory / "speaker.exe"
            subprocess.run([*cc, "-std=c11", "-Wall", "-Wextra",
                            "-I", str(directory), "-I", str(codec / "include"),
                            "-I", str(codec / "interface"), "-I", str(codec),
                            str(ROOT / "tests/xiao_147_audio_harness.c"),
                            str(codec / "esp_codec_dev.c"), "-lm", "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True, timeout=10)

    def test_touch_report_bounds_and_release(self):
        cc = shlex.split(os.environ.get("CC", "cc"))
        if not cc or not shutil.which(cc[0]):
            self.skipTest("host C compiler is not installed")
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "touch.exe"
            subprocess.run([*cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "components/muse/boards"),
                            str(ROOT / "tests/xiao_147_touch_harness.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_explicit_windows_port_is_matched_to_usb_identity(self):
        spec = importlib.util.spec_from_file_location("xiao_ports", ROOT / "tools/muse/ports.py")
        ports = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ports)
        devices = [SimpleNamespace(device="COM80", vid=0x303a, pid=0x1001, serial_number="first"),
                   SimpleNamespace(device="COM81", vid=0x303a, pid=0x1001, serial_number="second"),
                   SimpleNamespace(device="COM5", vid=None, pid=None, serial_number=None)]
        self.assertEqual(ports.find("xiao147", "com80", devices), "COM80")
        with self.assertRaises(ports.NotFound):
            ports.find("xiao147", None, devices)
        with self.assertRaises(ports.NotFound):
            ports.find("xiao147", "COM5", devices)
