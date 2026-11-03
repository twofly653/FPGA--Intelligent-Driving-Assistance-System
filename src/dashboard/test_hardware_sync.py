"""Real wire metadata regression. No serial, JTAG or hardware service access."""
import http.client
import json
import threading
import unittest
from unittest.mock import patch

from telemetry_protocol import parse_packet
from server import Bridge, ThreadingHTTPServer, make_handler

TEL = "TEL 125 -76 359 2 -3 4 251 500 1013 600 800 2 1 0"
CAMERA = "CAMERA 1 3 1 1 640 480"
OBSTACLE = "OBSTACLE 1 4294967295 0 1 10 7 2 1 0 0 639 479 65535 638 478 639 479"


class SyncProtocolTest(unittest.TestCase):
    def test_camera_all_modes_and_boundaries(self):
        for mode in range(4):
            for live in (0, 1):
                for configured in (0, 1):
                    p = parse_packet(f"CAMERA 4294967295 {mode} {live} {configured} 640 480")
                    self.assertEqual(p, dict(kind="CAMERA", sequence=0xffffffff,
                        image_mode=mode, live=live, configured=configured, width=640, height=480))

    def test_camera_rejects_malformed_fields_and_nul(self):
        fields = CAMERA.split()
        for index, values in {1: ['-1', '4294967296', '1_0', '1.0', '+1', '１'],
                2: ['-1', '4'], 3: ['2'], 4: ['2'], 5: ['320', '641'], 6: ['240', '481']}.items():
            for value in values:
                bad = fields[:]; bad[index] = value
                self.assertIsNone(parse_packet(' '.join(bad)), (index, value))
        for line in (CAMERA + ' 1', ' '.join(fields[:-1]), CAMERA.replace('640', '6\x0040'),
                     CAMERA + '\x00', 'CAMERA ' + '9' * 10000 + ' 0 1 1 640 480'):
            self.assertIsNone(parse_packet(line))

    def test_obstacle_zero_and_eight_boxes(self):
        for state in range(6):
            p = parse_packet(f"OBSTACLE 0 0 {state} 0 2147483647 2147483647 0")
            self.assertEqual(p['boxes'], [])
        p = parse_packet(OBSTACLE)
        self.assertEqual(p['count'], 2)
        self.assertEqual(p['boxes'][0], dict(id=1, xmin=0, ymin=0, xmax=639, ymax=479))
        self.assertEqual(p['boxes'][1]['id'], 65535)
        line = 'OBSTACLE 4294967295 4294967295 0 1 500 0 8 ' + ' '.join(
            f'{i + 1} {i} {i} {639 - i} {479 - i}' for i in range(8))
        self.assertEqual(parse_packet(line)['count'], 8)

    def test_obstacle_rejects_bounds_count_and_inconsistent_state(self):
        fields = OBSTACLE.split()
        for index, values in {1: ['-1', '4294967296'], 2: ['4294967296'], 3: ['1', '6'],
                4: ['0', '2'], 5: ['501', '2147483648'], 6: ['2147483648', '-1'],
                7: ['0', '1', '3', '9'], 8: ['0', '65536', '1_0', '１'],
                9: ['640', '-1'], 10: ['480'], 11: ['640'], 12: ['480'],
                13: ['1'], 14: ['640'], 15: ['480'], 16: ['637'], 17: ['477']}.items():
            for value in values:
                bad = fields[:]; bad[index] = value
                self.assertIsNone(parse_packet(' '.join(bad)), (index, value))
        for line in (OBSTACLE + ' 1', ' '.join(fields[:-1]), OBSTACLE + '\x00',
                     OBSTACLE.replace('639', '63\x009'), OBSTACLE.replace(' 65535 ', ' +2 ')):
            self.assertIsNone(parse_packet(line))


class SyncBridgeTest(unittest.TestCase):
    def setUp(self):
        self.clock = [100.0]
        self.time_patch = patch('server.time.monotonic', side_effect=lambda: self.clock[0])
        self.time_patch.start()
        self.addCleanup(self.time_patch.stop)
        self.bridge = Bridge()
        self.bridge.connected = True

    def send(self, line):
        packet = parse_packet(line)
        self.assertIsNotNone(packet)
        self.bridge.publish(packet)
        return self.bridge.snapshot()

    def test_independent_metadata_does_not_overwrite_pose(self):
        self.send(TEL)
        self.send(CAMERA)
        s = self.send(OBSTACLE)
        self.assertEqual(s['yaw'], 359)
        self.assertEqual(self.bridge.packet['roll'], 12.5)
        self.assertEqual(len(self.bridge.arrivals), 1)
        self.assertEqual(s['camera']['image_mode'], 3)
        self.assertTrue(s['obstacles']['valid'])
        self.assertEqual(s['obstacles']['boxes'][0]['xmax'], 639)
        # API consumers cannot mutate the cached transport record.
        s['obstacles']['boxes'][0]['xmin'] = 400
        self.assertEqual(self.bridge.snapshot()['obstacles']['boxes'][0]['xmin'], 0)
        self.clock[0] += .6
        self.send('CAMERA 2 2 1 1 640 480')
        self.send('OBSTACLE 2 2 0 1 0 2 0')
        self.assertEqual(self.bridge.snapshot()['imu'], 0)

    def test_obstacle_age_includes_board_age_and_http_never_refreshes(self):
        self.send(CAMERA)
        self.send(OBSTACLE)
        self.clock[0] += .49
        s = self.bridge.snapshot()['obstacles']
        self.assertEqual(s['age_ms'], 500)
        self.assertTrue(s['valid'])
        self.clock[0] += .002
        self.send(OBSTACLE)  # Same result is not a new observation.
        for _ in range(5):
            s = self.bridge.snapshot()['obstacles']
            self.assertFalse(s['valid'])
            self.assertTrue(s['stale'])
            self.assertEqual(s['boxes'], [])
            self.assertEqual(s['count'], 0)
            self.assertEqual(s['age_ms'], 502)

    def test_camera_loss_stops_boxes_without_affecting_traffic(self):
        self.send(CAMERA)
        self.send(OBSTACLE)
        self.send('TRAFFIC 1 2 1 95 10 1 0 8')
        self.clock[0] += 1.001
        self.send('OBSTACLE 2 2 0 1 0 2 1 4 10 20 30 40')
        s = self.bridge.snapshot()
        self.assertTrue(s['camera']['stale'])
        self.assertEqual(s['camera']['live'], 0)
        self.assertFalse(s['obstacles']['valid'])
        self.assertEqual(s['obstacles']['count'], 0)
        self.assertEqual(s['traffic']['countdown'], 10)
        self.send('CAMERA 2 0 1 1 640 480')
        self.assertTrue(self.bridge.snapshot()['obstacles']['valid'])

    def test_duplicate_camera_heartbeat_does_not_refresh_and_zero_clock_is_valid(self):
        self.clock[0] = 0.0
        self.send(CAMERA)
        self.assertFalse(self.bridge.snapshot()['camera']['stale'])
        self.clock[0] = 1.0
        self.send(CAMERA)
        self.assertFalse(self.bridge.snapshot()['camera']['stale'])
        self.clock[0] = 1.001
        s = self.send(CAMERA)
        self.assertTrue(s['camera']['stale'])
        self.assertEqual(s['camera']['age_ms'], 1001)
        self.assertEqual(s['camera']['configured'], 0)

    def test_configuration_live_and_state_gates_and_empty_ready(self):
        for seq, live, configured in ((1, 0, 1), (2, 1, 0), (3, 0, 0)):
            self.send(f'CAMERA {seq} 0 {live} {configured} 640 480')
            self.send(f'OBSTACLE {seq} {seq} 0 1 0 2 1 4 10 20 30 40')
            self.assertFalse(self.bridge.snapshot()['obstacles']['valid'])
        self.send('CAMERA 4 0 1 1 640 480')
        for seq, state, available in ((4, 1, 1), (5, 2, 1), (6, 3, 1), (7, 4, 1), (8, 5, 1), (9, 0, 0)):
            s = self.send(f'OBSTACLE {seq} {seq} {state} {available} 0 0 0')
            self.assertFalse(s['obstacles']['valid'])
        s = self.send('OBSTACLE 10 10 0 1 0 2 0')
        self.assertTrue(s['obstacles']['valid'])
        self.assertFalse(s['obstacles']['stale'])
        self.assertEqual(s['obstacles']['boxes'], [])

    def test_duplicates_reorder_wrap_and_boot_epoch(self):
        self.send('CAMERA 4294967295 1 1 1 640 480')
        self.send('OBSTACLE 4294967295 50 0 1 0 2 0')
        self.clock[0] += .2
        self.send('CAMERA 4294967294 3 0 0 640 480')
        self.send('OBSTACLE 4294967294 51 1 0 0 2 0')
        self.assertEqual(self.bridge.snapshot()['camera']['image_mode'], 1)
        self.assertEqual(self.bridge.snapshot()['obstacles']['result_seq'], 0xffffffff)
        self.send('CAMERA 0 2 1 1 640 480')
        self.send('OBSTACLE 0 52 0 1 0 2 0')
        self.assertEqual(self.bridge.snapshot()['camera']['sequence'], 0)
        self.assertEqual(self.bridge.snapshot()['obstacles']['result_seq'], 0)
        for banner in ('CAMERA_SYNC_R7_20261026 / RGB565 DDR VGA + SYNC',
                       'CAMERA_OBSTACLE_R6_20261025 / RGB565 DDR VGA',
                       'CAMERA_INTEGRATED_R5_20261025 / RGB565 DDR VGA'):
            self.assertTrue(self.bridge.board_boot(banner))
            self.assertTrue(self.bridge.snapshot()['camera']['stale'])
            self.assertFalse(self.bridge.snapshot()['obstacles']['valid'])
            self.assertEqual(self.bridge.traffic, {})
            self.send('CAMERA 1 0 1 1 640 480')
        self.assertFalse(self.bridge.board_boot('[CAMERA] mode=0'))

    def test_disconnected_old_firmware_and_reconnect_do_not_claim_live_boxes(self):
        self.bridge = Bridge()
        initial = self.bridge.snapshot()
        self.assertFalse(initial['connected'])
        self.assertTrue(initial['camera']['stale'])
        self.assertFalse(initial['obstacles']['valid'])
        self.bridge.connected = True
        self.send(TEL)  # Old firmware has no CAMERA / OBSTACLE metadata.
        self.assertFalse(self.bridge.snapshot()['obstacles']['valid'])
        self.send(CAMERA)
        self.send(OBSTACLE)
        self.bridge._status(False, 'read failed')
        self.assertEqual(self.bridge.camera, {})
        self.assertEqual(self.bridge.obstacles, {})
        self.assertEqual(self.bridge.snapshot()['obstacles']['count'], 0)
        self.bridge._status(True)
        self.assertFalse(self.bridge.snapshot()['obstacles']['valid'])
        self.send(CAMERA)
        self.send(OBSTACLE)
        self.bridge.disconnect()
        self.assertIsNone(self.bridge.camera_received)
        self.assertIsNone(self.bridge.obstacles_received)

    def test_replay_is_labeled_and_does_not_claim_board_source(self):
        self.bridge.mode = 'REPLAY'
        self.send(CAMERA)
        self.send(OBSTACLE)
        s = self.bridge.snapshot()
        self.assertEqual(s['camera']['source'], 'REPLAY')
        self.assertEqual(s['obstacles']['source'], 'REPLAY')

    def test_replay_keeps_boot_epoch_and_never_opens_uart(self):
        class Trace:
            name = 'test-replay.txt'
            def read_text(self, **_kwargs):
                return ('CAMERA 100 1 1 1 640 480\n'
                        'OBSTACLE 100 100 0 1 0 2 0\n'
                        'CAMERA_SYNC_R7_20261026 / READY\n'
                        'CAMERA 1 3 1 1 640 480\n'
                        'OBSTACLE 1 1 0 1 0 2 1 5 0 0 10 10\n')
        with patch('server.WindowsSerial', side_effect=AssertionError('No UART in replay')):
            self.bridge.replay(Trace(), 200.0, False)
            self.bridge.worker.join(timeout=1.0)
        self.assertFalse(self.bridge.worker.is_alive())
        s = self.bridge.snapshot()
        self.assertEqual(s['mode'], 'REPLAY')
        self.assertEqual(s['camera']['image_mode'], 3)
        self.assertEqual(s['camera']['sequence'], 1)
        self.assertEqual(s['obstacles']['result_seq'], 1)
        self.assertEqual(s['obstacles']['boxes'][0]['id'], 5)
        self.assertEqual(s['obstacles']['source'], 'REPLAY')
        self.bridge.disconnect()

    def test_http_and_sse_forward_same_real_metadata(self):
        self.send(CAMERA)
        self.send(OBSTACLE)
        server = ThreadingHTTPServer(('127.0.0.1', 0), make_handler(self.bridge))
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            conn = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=2)
            conn.request('GET', '/api/status')
            response = conn.getresponse()
            self.assertEqual(response.status, 200)
            state = json.loads(response.read())
            self.assertEqual(state['obstacles']['count'], 2)
            self.assertEqual(state['camera']['source'], 'BOARD_UART')
            conn.request('GET', '/events')
            response = conn.getresponse()
            event = json.loads(response.readline().decode()[6:])
            self.assertEqual(event['obstacles'], state['obstacles'])
            self.assertEqual(event['camera'], state['camera'])
            conn.close()
        finally:
            server.shutdown()
            server.server_close()

    def test_chunked_serial_forwarding_rejects_corruption_and_boot_clears_epoch(self):
        stop = threading.Event()
        chunks = iter([b'CAMERA_SYNC_R7_20261026 / READY\r\nCAM',
                       (CAMERA[3:] + '\n' + OBSTACLE + '\n').encode(),
                       b'CAMERA 2 0 1 1 6\xff40 480\n'])
        seen = []
        bridge = self.bridge

        class Reader:
            def __init__(self, _port): pass
            def read_chunk(self):
                try:
                    value = next(chunks)
                    return value
                except StopIteration:
                    seen.append(bridge.snapshot())
                    stop.set()
                    return b''
            def close(self): pass

        with patch('server.WindowsSerial', Reader), patch('server.os.name', 'nt'):
            bridge._serial('FAKE_PORT', stop)
        self.assertEqual(seen[0]['camera']['sequence'], 1)
        self.assertEqual(seen[0]['obstacles']['count'], 2)
        self.assertFalse(bridge.connected)
        self.assertFalse(bridge.snapshot()['obstacles']['valid'])


class ManualDetectionTest(unittest.TestCase):
    def setUp(self):
        self.clock = [100.0]
        self.time_patch = patch('server.time.monotonic', side_effect=lambda: self.clock[0])
        self.time_patch.start()
        self.addCleanup(self.time_patch.stop)
        self.bridge = Bridge()
        self.bridge.connected = True
        self.writes = []
        owner = self
        class Writer:
            def write(self, data):
                owner.writes.append(data)
                return len(data)
        self.bridge.reader = Writer()

    def send(self, line):
        packet = parse_packet(line)
        self.assertIsNotNone(packet)
        self.bridge.publish(packet)
        return self.bridge.snapshot()

    def test_detect_wire_strict_and_uint32(self):
        for seq in (0, 1, 0xffffffff):
            for enabled in (0, 1):
                self.assertEqual(parse_packet(f'DETECT {seq} {enabled}'),
                    dict(kind='DETECT', sequence=seq, enabled=enabled))
        for line in ('DETECT 1', 'DETECT 1 0 1', 'DETECT -1 1', 'DETECT 4294967296 1',
                     'DETECT +1 1', 'DETECT 1_0 1', 'DETECT １ 1', 'DETECT 1 2',
                     'DETECT 1 true', 'DETECT 1 1\x00', 'DETECT 1 0.0'):
            self.assertIsNone(parse_packet(line), line)

    def test_manual_state_does_not_overwrite_attitude_and_legacy_is_read_only(self):
        self.send(TEL); self.send(CAMERA); self.send(OBSTACLE)
        self.assertTrue(self.bridge.snapshot()['obstacles']['valid'])
        self.assertFalse(self.bridge.snapshot()['detection']['can_control'])
        with self.assertRaises(RuntimeError):
            self.bridge.obstacle_command({'enabled': False})
        s = self.send('DETECT 1 1')
        self.assertEqual(s['yaw'], 359)
        self.assertEqual(len(self.bridge.arrivals), 1)
        self.assertTrue(s['detection']['can_control'])
        self.assertFalse(s['obstacles']['valid'], 'first state waits for new OB result')
        self.send('OBSTACLE 2 2 0 1 0 2 1 4 10 20 30 40')
        self.assertTrue(self.bridge.snapshot()['obstacles']['valid'])

    def test_disable_enable_requires_new_observation(self):
        self.send(CAMERA); self.send('DETECT 1 1'); self.send(OBSTACLE)
        self.assertTrue(self.bridge.snapshot()['obstacles']['valid'])
        s = self.send('DETECT 2 0')
        self.assertFalse(s['obstacles']['valid']); self.assertEqual(s['obstacles']['boxes'], [])
        self.send('OBSTACLE 2 2 0 1 0 2 1 4 10 20 30 40')
        self.assertEqual(self.bridge.snapshot()['obstacles']['count'], 0)
        self.send('DETECT 3 1')
        self.assertEqual(self.bridge.snapshot()['obstacles']['count'], 0)
        self.send('OBSTACLE 2 2 0 1 0 2 1 4 10 20 30 40')
        self.assertEqual(self.bridge.snapshot()['obstacles']['count'], 0)
        self.send('OBSTACLE 3 3 0 1 0 2 1 4 10 20 30 40')
        self.assertEqual(self.bridge.snapshot()['obstacles']['count'], 1)

    def test_detect_repeat_reorder_wrap_expiry_and_reboot(self):
        self.send(CAMERA); self.send('DETECT 4294967295 0')
        self.send('DETECT 4294967294 1')
        self.assertEqual(self.bridge.snapshot()['detection']['enabled'], 0)
        self.send('DETECT 0 1')
        self.assertEqual(self.bridge.snapshot()['detection']['enabled'], 1)
        self.clock[0] += 1.5
        self.send('DETECT 0 1')
        self.assertFalse(self.bridge.snapshot()['detection']['stale'])
        self.clock[0] += .001
        self.send('CAMERA 2 0 1 1 640 480'); self.send(OBSTACLE)
        s = self.bridge.snapshot()
        self.assertTrue(s['detection']['stale']); self.assertIsNone(s['detection']['enabled'])
        self.assertFalse(s['detection']['can_control']); self.assertEqual(s['obstacles']['count'], 0)
        self.send('DETECT 1 1')
        self.assertFalse(self.bridge.snapshot()['detection']['stale'])
        epoch = self.bridge.snapshot()['video_epoch']
        self.assertTrue(self.bridge.board_boot('CAMERA_SYNC_R8_20261026 / MANUAL CONTROL'))
        self.assertGreater(self.bridge.snapshot()['video_epoch'], epoch)
        self.assertFalse(self.bridge.snapshot()['detection']['supported'])
        self.assertIsNone(self.bridge.detection_obstacle_floor)
        self.clock[0] = 0.0
        self.send('DETECT 1 0')
        self.assertFalse(self.bridge.snapshot()['detection']['stale'])
        self.bridge._status(False)
        self.assertEqual(self.bridge.detection, {})

    def test_explicit_commands_validate_input_and_do_not_assume_ack(self):
        self.send('DETECT 1 1')
        for enabled in (False, True, False):
            self.bridge.obstacle_command({'enabled': enabled})
            self.assertEqual(self.bridge.snapshot()['detection']['enabled'], 1)
        self.assertEqual(self.writes, [b'OBSTACLE OFF\n', b'OBSTACLE ON\n', b'OBSTACLE OFF\n'])
        for obj in ({}, {'enabled': 0}, {'enabled': 'true'}, {'enabled': None},
                    {'enabled': True, 'toggle': True}, {'action': 'toggle'}):
            with self.assertRaises(ValueError): self.bridge.obstacle_command(obj)
        self.bridge.mode = 'REPLAY'
        with self.assertRaises(RuntimeError): self.bridge.obstacle_command({'enabled': False})
        self.bridge.mode = 'LIVE'; self.bridge.connected = False
        with self.assertRaises(RuntimeError): self.bridge.obstacle_command({'enabled': False})
        self.bridge.connected = True
        self.bridge.reader.write = lambda data: len(data) - 1
        with self.assertRaises(RuntimeError): self.bridge.obstacle_command({'enabled': False})

    def test_http_manual_control_origin_and_board_confirmed_reply(self):
        self.send('DETECT 1 1')
        server = ThreadingHTTPServer(('127.0.0.1', 0), make_handler(self.bridge))
        server.daemon_threads = True
        threading.Thread(target=server.serve_forever, daemon=True).start()
        conn = http.client.HTTPConnection('127.0.0.1', server.server_port, timeout=2)
        try:
            conn.request('POST', '/api/obstacle/control', json.dumps({'enabled': False}),
                         {'Content-Type': 'application/json'})
            response = conn.getresponse(); state = json.loads(response.read())
            self.assertEqual(response.status, 200)
            self.assertEqual(self.writes, [b'OBSTACLE OFF\n'])
            self.assertEqual(state['detection']['enabled'], 1, 'POST is not an ACK')
            conn.request('POST', '/api/obstacle/control', '{"enabled":0}',
                         {'Content-Type': 'application/json'})
            response = conn.getresponse(); response.read(); self.assertEqual(response.status, 400)
            conn.request('POST', '/api/obstacle/control', '{"enabled":true}',
                         {'Content-Type': 'application/json', 'Origin': 'http://foreign.test'})
            response = conn.getresponse(); response.read(); self.assertEqual(response.status, 403)
            self.assertEqual(len(self.writes), 1)
        finally:
            conn.close(); server.shutdown(); server.server_close()


if __name__ == '__main__':
    unittest.main()
