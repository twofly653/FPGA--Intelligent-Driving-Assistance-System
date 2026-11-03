"""Offline photo pipeline tests; no COM, XSDB, hardware server or board opened."""
import http.client
import io
import json
import tempfile
import threading
import time
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch
from PIL import Image

from photo_capture import PhotoJobs, parse_photo_line, verify_jpeg, dap_read
from server import Bridge, ThreadingHTTPServer, make_handler


def fixture(size=(2592, 1944)):
    output = io.BytesIO()
    Image.new("RGB", size, (83, 141, 197)).save(output, format="JPEG", quality=85)
    return output.getvalue()


JPEG = fixture()
TEST_TMP = Path(__file__).resolve().parent / "build" / "photo_test_tmp"
TEST_TMP.mkdir(parents=True, exist_ok=True)
META = dict(kind="ready", board_id=7, address=0x00400000, size=len(JPEG),
            width=2592, height=1944, crc32=zlib.crc32(JPEG) & 0xffffffff)


def line(meta):
    return "PHOTO READY {board_id} {address:08x} {size} {width} {height} {crc32:08x}".format(**meta)


def padded(jpeg):
    return jpeg + b"\0" * ((-len(jpeg)) % 4)


class PhotoTest(unittest.TestCase):
    def test_metadata_exact_format_bounds_and_no_injection(self):
        self.assertEqual(parse_photo_line(line(META)), META)
        self.assertEqual(parse_photo_line("PHOTO ERROR FIFO_OVERFLOW"),
                         dict(kind="error", error="FIFO_OVERFLOW"))
        self.assertIsNone(parse_photo_line("TEL 1 2 3"))
        self.assertIsNone(parse_photo_line("PHOTO commands: PHOTO SNAP / PHOTO STATUS"))
        for field, value in (("address", 0), ("address", 0x40000001),
                             ("address", 0x1ffffffc), ("size", 2*1024*1024+1),
                             ("width", 640), ("height", 480), ("board_id", 0x100000000)):
            candidate = dict(META, **{field: value})
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                parse_photo_line(line(candidate))
        for text in (line(META)+"; exit", "PHOTO READY 1 bad 2 2592 1944 00000000",
                     "PHOTO ERROR unsafe error", "PHOTO BUSY 4294967296"):
            with self.assertRaises(ValueError):
                parse_photo_line(text)

    def test_crc_actual_decode_size_truncation_and_padding(self):
        self.assertEqual(verify_jpeg(padded(JPEG), META), JPEG)
        with self.assertRaises(ValueError):
            verify_jpeg(padded(JPEG), dict(META, crc32=0))
        with self.assertRaises(ValueError):
            verify_jpeg(padded(JPEG)[:-1], META)
        small = fixture((640, 480))
        with self.assertRaisesRegex(ValueError, "尺寸"):
            verify_jpeg(padded(small), dict(META, size=len(small), crc32=zlib.crc32(small)))
        corrupt = b"\xff\xd8bad\xff\xd9"
        with self.assertRaises(Exception):
            verify_jpeg(padded(corrupt), dict(META, size=len(corrupt), crc32=zlib.crc32(corrupt)))

    def test_job_sends_once_rejects_old_ready_and_only_publishes_verified_jpeg(self):
        with tempfile.TemporaryDirectory(dir=TEST_TMP) as folder:
            bridge = Bridge()
            calls = []
            def read(metadata, work):
                self.assertEqual(metadata["address"], META["address"])
                calls.append((metadata, work))
                return padded(JPEG)
            bridge.photo = PhotoJobs(bridge, Path(folder), read_memory=read)
            class Writer:
                frames = []
                rebooted = False
                def write(self, data):
                    self.frames.append(data)
                    if data == b"PHOTO STATUS\n":
                        bridge.photo.accept_line("PHOTO IDLE" if self.rebooted else line(dict(META, board_id=6)))
                    else:
                        bridge.photo.accept_line(line(dict(META, address=0x800000)))
                        bridge.photo.accept_line("PHOTO BUSY 7")
                        bridge.photo.accept_line(line(dict(META, board_id=6)))
                        bridge.photo.accept_line(line(META))
                    return len(data)
            writer = Writer()
            bridge.connected, bridge.reader = True, writer
            bridge.photo.start()
            bridge.photo.worker.join(2)
            state = bridge.photo.snapshot()
            self.assertEqual(state["phase"], "ready", state)
            self.assertEqual(writer.frames, [b"PHOTO STATUS\n", b"PHOTO SNAP\n"])
            self.assertEqual(len(calls), 1)
            photo = state["photo"]
            self.assertTrue(photo["verified"])
            self.assertEqual(bridge.photo.photo_path(photo["id"]).read_bytes(), JPEG)
            self.assertIsNone(bridge.photo.photo_path("../../server.py"))
            for _ in range(3):
                self.assertEqual(bridge.photo.snapshot()["photo"], photo)
            self.assertEqual(len(writer.frames), 2)  # GET/status never re-arms.
            # A CPU reboot may restart its ID sequence without disconnecting
            # COM. Request-local BUSY->READY still proves a fresh capture.
            writer.rebooted = True
            bridge.photo.start(); bridge.photo.worker.join(2)
            self.assertEqual(bridge.photo.snapshot()["phase"], "ready")
            self.assertEqual(len(calls), 2)
            self.assertEqual(len(bridge.photo.published), 2)

    def test_live_gate_mutex_timeout_and_crc_failure_never_publish(self):
        with tempfile.TemporaryDirectory(dir=TEST_TMP) as folder:
            bridge = Bridge()
            bridge.photo = PhotoJobs(bridge, Path(folder), status_timeout=.03, capture_timeout=.03)
            with self.assertRaises(RuntimeError):
                bridge.photo.start()
            class Silent:
                def write(self, data): return len(data)
            bridge.connected, bridge.reader, bridge.mode = True, Silent(), "REPLAY"
            with self.assertRaises(RuntimeError): bridge.photo.start()
            bridge.mode = "LIVE"
            bridge.photo.start()
            with self.assertRaises(RuntimeError): bridge.photo.start()
            bridge.photo.worker.join(1)
            self.assertEqual(bridge.photo.snapshot()["phase"], "error")
            self.assertFalse(bridge.photo.published)

            bridge.photo = PhotoJobs(bridge, Path(folder), status_timeout=.03, capture_timeout=.03)
            class NeverReady:
                def write(self, data):
                    bridge.photo.accept_line("PHOTO IDLE" if data==b"PHOTO STATUS\n" else "PHOTO BUSY 7")
                    return len(data)
            bridge.reader = NeverReady()
            bridge.photo.start(); bridge.photo.worker.join(1)
            self.assertEqual(bridge.photo.snapshot()["phase"], "error")
            self.assertFalse(bridge.photo.published)

            bridge.photo = PhotoJobs(bridge, Path(folder), read_memory=lambda *_: padded(JPEG),
                                     status_timeout=.03, capture_timeout=.03)
            class Respond:
                def write(self, data):
                    if data == b"PHOTO STATUS\n": bridge.photo.accept_line("PHOTO IDLE")
                    else:
                        bridge.photo.accept_line("PHOTO BUSY 7")
                        bridge.photo.accept_line(line(dict(META, crc32=0)))
                    return len(data)
            bridge.reader = Respond()
            bridge.photo.start(); bridge.photo.worker.join(1)
            self.assertIn("CRC", bridge.photo.snapshot()["error"])
            self.assertFalse(bridge.photo.published)
            self.assertFalse(list(Path(folder).glob("*.jpg")))

    def test_dap_script_is_read_only_numeric_and_rounded(self):
        with tempfile.TemporaryDirectory(dir=TEST_TMP) as folder:
            work = Path(folder)
            class Completed:
                returncode = 0
                stdout = b"PHOTO_DAP_READ_OK"
            def fake_run(arguments, **kwargs):
                self.assertIsInstance(arguments, list)
                self.assertNotIn("shell", kwargs)
                script = Path(arguments[1]).read_text()
                self.assertIn('name =~ "APU"', script)
                self.assertIn("mrd -bin -file", script)
                self.assertIn(f"0x00400000 {(len(JPEG)+3)//4}", script)
                for forbidden in ("rst ", "halt", "dow ", "fpga ", "stop", "con -"):
                    self.assertNotIn(forbidden, script)
                (work / "photo.bin").write_bytes(padded(JPEG))
                return Completed()
            with patch("photo_capture.XSDB", work / "xsdb.bat"), patch("photo_capture._server_available", return_value=True), patch("photo_capture.subprocess.run", side_effect=fake_run):
                (work / "xsdb.bat").touch()
                self.assertEqual(dap_read(META, work), padded(JPEG))

    def test_http_empty_object_same_origin_and_photo_allowlist(self):
        with tempfile.TemporaryDirectory(dir=TEST_TMP) as folder:
            bridge = Bridge()
            bridge.photo = PhotoJobs(bridge, Path(folder))
            approved = Path(folder) / "7-aaaaaaaaaaaa.jpg"
            approved.write_bytes(JPEG)
            bridge.photo.published["7-aaaaaaaaaaaa"] = approved
            server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(bridge))
            threading.Thread(target=server.serve_forever, daemon=True).start()
            try:
                connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
                for path, expected in (("/api/photo", 200), ("/photos/7-aaaaaaaaaaaa.jpg", 200),
                                       ("/photos/7-bbbbbbbbbbbb.jpg", 404),
                                       ("/photos/.work/abc/photo.bin", 404),
                                       ("/legacy/photos/7-aaaaaaaaaaaa.jpg", 404)):
                    connection.request("GET", path)
                    response = connection.getresponse()
                    self.assertEqual(response.status, expected, path)
                    response.read()
                for body, headers, expected in (("{}", {}, 400),
                                                ('{"address":"0x100000"}', {}, 400),
                                                ("", {"Origin":"https://example.com"}, 403)):
                    connection.request("POST", "/api/photo", body, headers)
                    response = connection.getresponse()
                    self.assertEqual(response.status, expected)
                    response.read()
                connection.close()
                class Writer:
                    def write(self, data):
                        if data == b"PHOTO STATUS\n":
                            bridge.photo.accept_line("PHOTO IDLE")
                        else:
                            bridge.photo.accept_line("PHOTO BUSY 7")
                            bridge.photo.accept_line(line(META))
                        return len(data)
                bridge.reader, bridge.connected = Writer(), True
                bridge.photo.read_memory = lambda *_: padded(JPEG)
                connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
                connection.request("POST", "/api/photo", "{}")
                response = connection.getresponse()
                self.assertEqual(response.status, 202)
                self.assertIn("job_id", json.loads(response.read()))
                bridge.photo.worker.join(2)
                connection.request("GET", "/api/photo")
                response = connection.getresponse()
                state = json.loads(response.read())
                self.assertEqual(state["phase"], "ready")
                connection.request("GET", state["photo"]["url"])
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                self.assertEqual(response.getheader("Content-Type"), "image/jpeg")
                self.assertEqual(response.read(), JPEG)
                connection.close()
            finally:
                server.shutdown(); server.server_close()


if __name__ == "__main__": unittest.main()
