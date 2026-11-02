"""Traffic metadata tests. No board, serial port or JTAG access."""
import unittest
from unittest.mock import patch
from telemetry_protocol import parse_packet
from server import Bridge

LINE = "TRAFFIC 4294967295 1 1 93 28 1 10 11"
TEL = "TEL 125 -76 359 2 -3 4 251 500 1013 600 800 2 1 0"

class TrafficTest(unittest.TestCase):
    def test_parses_units_bounds_and_unknown(self):
        p = parse_packet(LINE)
        self.assertEqual(p['frame_seq'], 0xffffffff)
        self.assertEqual(p['countdown'], 28)
        self.assertEqual(p['color'], 1)
        self.assertEqual(p['proc_ms'], 11)
        for color in (1,2,3):
            self.assertEqual(parse_packet(f"TRAFFIC 1 {color} 1 100 0 1 0 0")['countdown'], 0)
        self.assertEqual(parse_packet("TRAFFIC 0 0 0 0 -1 0 0 0")['digit_valid'], 0)
        self.assertIsNone(parse_packet("TRAFFIC 1 1 1 90 28 1 0"))
        self.assertIsNone(parse_packet(LINE + " 0"))
        self.assertIsNone(parse_packet(LINE.replace("28", "2\x008")))

    def test_rejects_malformed_inconsistent_and_unbounded_values(self):
        parts = LINE.split()
        for index, values in {
            1:['-1','4294967296','1_000','1.0','NaN','1e2'],
            2:['-1','4','0'], 3:['-1','2'], 4:['-1','101'],
            5:['-2','100','-1'], 6:['-1','2','0'],
            7:['-1','2147483648'], 8:['-1','2147483648']}.items():
            for value in values:
                bad = parts[:]; bad[index] = value
                self.assertIsNone(parse_packet(' '.join(bad)), (index,value))
        self.assertIsNone(parse_packet("TRAFFIC 1 1 0 90 28 1 0 1"))
        self.assertIsNone(parse_packet("TRAFFIC 1 1 1 90 -1 1 0 1"))

    def test_independent_from_tel_and_freezes_expire_without_new_observation(self):
        clock = [100.0]
        with patch('server.time.monotonic', side_effect=lambda:clock[0]):
            bridge = Bridge(); bridge.connected = True
            bridge.publish(parse_packet(TEL)); bridge.publish(parse_packet(LINE))
            self.assertEqual(bridge.snapshot()['traffic']['countdown'],28)
            self.assertEqual(bridge.packet['yaw'],359)
            clock[0] += .8
            bridge.publish(parse_packet(LINE))  # frozen seq does not reset age
            self.assertEqual(bridge.snapshot()['traffic']['valid'],1)
            self.assertEqual(bridge.snapshot()['imu'],0) # independent 500ms TEL expiration
            self.assertEqual(bridge.snapshot()['traffic']['countdown'],28) # never decrement
            clock[0] += .8
            bridge.publish(parse_packet(TEL))
            s = bridge.snapshot()
            self.assertEqual(s['imu'],1)
            self.assertTrue(s['traffic']['stale'])
            self.assertEqual(s['traffic']['color'],0)
            self.assertEqual(s['traffic']['countdown'],-1)
            self.assertEqual(s['traffic']['valid'],0)
            bridge.publish(parse_packet('TRAFFIC 0 3 1 90 12 1 0 8')) # uint32 wrap
            self.assertEqual(bridge.snapshot()['traffic']['color'],3)
            self.assertEqual(bridge.snapshot()['traffic']['countdown'],12)

    def test_board_age_disconnect_reset_and_replay_source(self):
        bridge = Bridge(); bridge.connected = True
        bridge.publish(parse_packet('TRAFFIC 1 1 1 90 15 1 1501 8'))
        self.assertEqual(bridge.snapshot()['traffic']['valid'],0)
        bridge.publish(parse_packet('TRAFFIC 2 3 1 90 -1 0 0 8'))
        self.assertEqual(bridge.snapshot()['traffic']['valid'],1)
        self.assertEqual(bridge.snapshot()['traffic']['countdown'],-1)
        bridge.mode = 'REPLAY'
        self.assertEqual(bridge.snapshot()['traffic']['source'],'REPLAY')
        bridge.connected = False
        self.assertEqual(bridge.snapshot()['traffic']['color'],0)
        bridge.disconnect()
        self.assertEqual(bridge.traffic,{})
        bridge.connected = True
        self.assertEqual(bridge.snapshot()['traffic']['valid'],0)
        self.assertIsNone(bridge.snapshot()['traffic']['age_ms'])

if __name__ == '__main__': unittest.main()
