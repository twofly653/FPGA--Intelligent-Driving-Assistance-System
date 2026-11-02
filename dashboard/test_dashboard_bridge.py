"""Offline bridge tests. No real serial port is opened."""
import http.client
import json
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from server import Bridge, ThreadingHTTPServer, make_handler
from telemetry_protocol import parse_packet

FRAME = "TEL 125 -76 359 2 -3 4 251 500 1013 600 800 2 1 0"
CMP = "CMP 1327 1 1 1 1 1 0 1 603 512 8 140 212 -152 400 740 -1 1 22000 31"


class BridgeTest(unittest.TestCase):
    def test_tel_and_compass_update_and_expire_independently(self):
        bridge = Bridge()
        bridge.connected = True
        bridge.publish(parse_packet(FRAME))
        bridge.publish(parse_packet(CMP))
        state = bridge.snapshot()
        self.assertEqual(state["yaw"], 359)
        self.assertEqual(state["compass"]["heading_x10"], 1327)
        self.assertEqual(state["compass"]["valid"], 1)
        bridge.received -= 0.6
        state = bridge.snapshot()
        self.assertEqual(state["imu"], 0)
        self.assertEqual(state["compass"]["valid"], 1)
        bridge.publish(parse_packet(FRAME))
        bridge.compass_received -= 0.6
        state = bridge.snapshot()
        self.assertEqual(state["imu"], 1)
        self.assertEqual(state["compass"]["valid"], 0)
        self.assertTrue(state["compass"]["stale"])
        bridge.connected = False
        bridge.compass_received = time.monotonic()
        self.assertEqual(bridge.snapshot()["compass"]["valid"], 0)

    def test_compass_commands_are_allowlisted_and_serialized(self):
        class Writer:
            def __init__(self):
                self.frames, self.writing, self.overlap = [], False, False
            def write(self, data):
                if self.writing:
                    self.overlap = True
                self.writing = True
                time.sleep(.005)
                self.frames.append(data)
                self.writing = False
                return len(data)

        bridge = Bridge()
        writer = Writer()
        bridge.reader, bridge.connected = writer, True
        for action in ("begin", "end", "cancel", "flip", "confirm"):
            bridge.compass_command({"action": action})
        bridge.compass_command({"action": "ref", "heading_x10": 740})
        self.assertEqual(writer.frames, [b"CAL BEGIN\n", b"CAL END\n", b"CAL CANCEL\n",
                                       b"CAL FLIP\n", b"CAL CONFIRM\n", b"CAL REF 740\n"])
        for obj in ({"action": []}, {"action": "CAL BEGIN\nCAL FLIP"},
                    {"action": "ref", "heading_x10": True},
                    {"action": "ref", "heading_x10": "740\nCAL FLIP"},
                    {"action": "ref", "heading_x10": 3600},
                    {"action": "ref", "heading_x10": -1}):
            with self.assertRaises(ValueError):
                bridge.compass_command(obj)
        self.assertEqual(len(writer.frames), 6)
        workers = [threading.Thread(target=bridge.compass_command, args=({"action": "ref", "heading_x10": i},)) for i in range(8)]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join(timeout=1)
        self.assertFalse(writer.overlap)
        self.assertEqual(len(writer.frames), 14)
        for mode, connected, reader in (("REPLAY", True, writer), ("LIVE", False, writer), ("LIVE", True, None)):
            bridge.mode, bridge.connected, bridge.reader = mode, connected, reader
            with self.assertRaises(RuntimeError):
                bridge.compass_command({"action": "begin"})

    def test_http_compass_contract_rejects_invalid_actions_and_external_origin(self):
        class Writer:
            frames = []
            def write(self, data):
                self.frames.append(data)
                return len(data)
        bridge = Bridge()
        bridge.reader, bridge.connected = Writer(), True
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(bridge))
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            conn = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
            conn.request("POST", "/api/compass", json.dumps({"action": "ref", "heading_x10": 740}), {"Content-Type": "application/json"})
            response = conn.getresponse()
            self.assertEqual(response.status, 200)
            self.assertIn("compass", json.loads(response.read()))
            self.assertEqual(Writer.frames, [b"CAL REF 740\n"])
            conn.request("POST", "/api/compass", '{"action":[]}', {"Content-Type": "application/json"})
            response = conn.getresponse()
            self.assertEqual(response.status, 400)
            response.read()
            conn.request("POST", "/api/compass", '{"action":"begin"}', {"Origin": "https://example.com"})
            response = conn.getresponse()
            self.assertEqual(response.status, 403)
            response.read()
            self.assertEqual(Writer.frames, [b"CAL REF 740\n"])
            conn.close()
        finally:
            server.shutdown()
            server.server_close()

    def test_state_never_claims_valid_pose_before_first_packet(self):
        state = Bridge().snapshot()
        self.assertTrue(state["stale"])
        self.assertEqual(state["imu"], 0)
        self.assertIsNone(state["age_ms"])

    def test_expiration_does_not_hide_data_loss_behind_old_pose(self):
        bridge = Bridge()
        bridge.connected = True
        bridge.publish(parse_packet(FRAME))
        self.assertEqual(bridge.snapshot()["imu"], 1)
        bridge.received -= 0.6
        state = bridge.snapshot()
        self.assertTrue(state["stale"])
        self.assertEqual(state["imu"], 0)
        self.assertEqual(state["mag"], 0)

    def test_serial_chunks_partial_and_multiple_lines(self):
        stop = threading.Event()
        chunks = iter([b"Boot ready\r\nTE", FRAME[2:20].encode(),
                       (FRAME[20:] + "\r\n" + FRAME + "\n").encode()])

        class Reader:
            closed = False
            def __init__(self, _port):
                pass
            def read_chunk(self):
                try:
                    return next(chunks)
                except StopIteration:
                    stop.set()
                    return b""
            def write(self, data):
                return len(data)
            def close(self):
                Reader.closed = True

        bridge = Bridge()
        with patch("server.WindowsSerial", Reader), patch("server.os.name", "nt"):
            bridge._serial("FAKE_PORT", stop)
        self.assertEqual(bridge.error, "")
        self.assertTrue(stop.is_set(), "Fake RX must finish without an exception")
        self.assertEqual(bridge.sequence, 2)
        self.assertEqual(bridge.packet["roll"], 12.5)
        self.assertEqual(bridge.packet["yaw"], 359)
        self.assertTrue(Reader.closed)
        self.assertFalse(bridge.connected)

    def test_http_and_sse_emit_same_received_frame_and_label_replay(self):
        bridge = Bridge()
        bridge.mode, bridge.port, bridge.connected = "REPLAY", "offline-test", True
        bridge.publish(parse_packet(FRAME))
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(bridge))
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            conn = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
            conn.request("GET", "/api/status")
            response = conn.getresponse()
            state = json.loads(response.read())
            self.assertEqual(state["mode"], "REPLAY")
            self.assertEqual(state["yaw"], 359)
            conn.request("GET", "/")
            page = conn.getresponse().read().decode("utf-8")
            self.assertIn("DriveSense <span>Studio</span>", page)
            self.assertIn('src="car_model_data.js"', page)
            self.assertIn('id="linkConnect"', page)
            conn.request("GET", "/events")
            response = conn.getresponse()
            self.assertEqual(response.getheader("Content-Type"), "text/event-stream")
            payload = json.loads(response.readline().decode()[6:])
            self.assertEqual(payload["seq"], state["seq"])
            self.assertEqual(payload["roll"], state["roll"])
            conn.close()
        finally:
            server.shutdown()
            server.server_close()

    def test_external_origin_cannot_open_serial(self):
        bridge = Bridge()
        server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(bridge))
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            conn = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
            conn.request("POST", "/api/connect", '{"port":"COM10"}', {"Origin":"https://example.com"})
            self.assertEqual(conn.getresponse().status, 403)
            self.assertIsNone(bridge.worker)
            conn.close()
        finally:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    unittest.main()
