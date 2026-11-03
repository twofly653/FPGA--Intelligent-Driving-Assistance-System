"""Offline clock synchronization tests: never opens a serial device."""
import threading
import unittest
from datetime import datetime, timedelta, timezone
from unittest.mock import patch

from server import Bridge, CLOCK_SYNC_SECONDS, SHANGHAI_TIMEZONE


class Writer:
    def __init__(self):
        self.frames = []
        self.partial = False

    def write(self, data):
        self.frames.append(data)
        return len(data) - int(self.partial)


class ClockSyncTest(unittest.TestCase):
    def setUp(self):
        self.bridge = Bridge()
        self.writer = Writer()
        self.bridge.reader = self.writer
        self.bridge.connected = True
        self.bridge.received = 100.0
        self.mono = patch("server.time.monotonic", return_value=100.0)
        self.now = self.mono.start()
        self.date = patch("server.datetime")
        self.dt = self.date.start()
        # The simulated machine clock is UTC; the payload must explicitly use +8.
        self.utc = datetime(2026, 10, 28, 16, 59, 59, tzinfo=timezone.utc)
        self.dt.now.side_effect = lambda tz: self.utc.astimezone(tz)
        self.addCleanup(self.mono.stop)
        self.addCleanup(self.date.stop)

    def test_explicit_shanghai_date_and_periodic_resynchronization(self):
        self.assertEqual(SHANGHAI_TIMEZONE.utcoffset(None), timedelta(hours=8))
        self.assertTrue(self.bridge._sync_clock_if_due())
        self.dt.now.assert_called_once_with(SHANGHAI_TIMEZONE)
        self.assertEqual(self.writer.frames, [b"TIME SET 2026-10-29T00:59:59\n"])
        self.assertEqual(self.bridge.next_clock_sync, 100.0 + CLOCK_SYNC_SECONDS)
        self.now.return_value = 159.9
        self.bridge.received = 159.9
        self.assertFalse(self.bridge._sync_clock_if_due())
        self.utc += timedelta(seconds=60)
        self.now.return_value = 160.0
        self.bridge.received = 160.0
        self.assertTrue(self.bridge._sync_clock_if_due())
        self.assertEqual(self.writer.frames[-1], b"TIME SET 2026-10-29T01:00:59\n")

    def test_only_fresh_live_board_is_written(self):
        for mode, connected, reader, received in (
            ("REPLAY", True, self.writer, 100.0),
            ("LIVE", False, self.writer, 100.0),
            ("LIVE", True, None, 100.0),
            ("LIVE", True, self.writer, 0.0),
            ("LIVE", True, self.writer, 99.0),
        ):
            with self.subTest(mode=mode, connected=connected, received=received):
                self.bridge.mode = mode
                self.bridge.connected = connected
                self.bridge.reader = reader
                self.bridge.received = received
                self.assertFalse(self.bridge._sync_clock_if_due())
        self.assertEqual(self.writer.frames, [])

    def test_photo_defers_sync_without_consuming_due_time(self):
        with patch.object(self.bridge.photo, "snapshot", return_value={"busy": True}):
            self.assertFalse(self.bridge._sync_clock_if_due())
            self.assertEqual(self.bridge.next_clock_sync, 0.0)
        self.assertTrue(self.bridge._sync_clock_if_due())
        self.assertEqual(len(self.writer.frames), 1)

    def test_busy_control_and_serial_locks_do_not_block_rx_worker(self):
        for lock in (self.bridge.control, self.bridge.serial_lock):
            lock.acquire()
            try:
                results = []
                worker = threading.Thread(target=lambda: results.append(self.bridge._sync_clock_if_due()))
                worker.start()
                worker.join(timeout=.25)
                self.assertFalse(worker.is_alive(), "Clock sync must not block disconnect/PHOTO")
                self.assertEqual(results, [False])
            finally:
                lock.release()
        self.assertTrue(self.bridge._sync_clock_if_due())

    def test_boot_forces_resync_after_new_telemetry(self):
        self.assertTrue(self.bridge._sync_clock_if_due())
        self.assertTrue(self.bridge.board_boot("CAMERA_SYNC_R10 / English LCD"))
        self.assertEqual(self.bridge.next_clock_sync, 0.0)
        self.assertFalse(self.bridge._sync_clock_if_due())
        self.bridge.received = 100.0
        self.assertTrue(self.bridge._sync_clock_if_due())
        self.assertEqual(len(self.writer.frames), 2)

    def test_serial_loop_waits_for_telemetry_and_resyncs_new_boot(self):
        bridge = self.bridge
        bridge.received = 0.0
        stop = threading.Event()
        tel = b"TEL 125 -76 359 2 -3 4 251 500 1013 600 800 2 1 0\n"
        chunks = iter([b"CAMERA_SYNC_R10 / READY\n", tel[:9], tel[9:],
                       b"CAMERA_SYNC_R10 / RESTART\n", tel])
        reads = [0]
        writes = []
        closed = []

        class FakePort:
            def __init__(self, port):
                if port != "CLOCK_FAKE":
                    raise AssertionError("Only a fake port is allowed")
            def read_chunk(self):
                try:
                    value = next(chunks)
                    reads[0] += 1
                    return value
                except StopIteration:
                    stop.set()
                    return b""
            def write(self, data):
                writes.append((reads[0], data))
                return len(data)
            def close(self):
                closed.append(True)

        with patch("server.WindowsSerial", FakePort), patch("server.os.name", "nt"):
            bridge._serial("CLOCK_FAKE", stop)
        self.assertEqual(bridge.error, "")
        self.assertTrue(stop.is_set())
        self.assertEqual(closed, [True])
        self.assertEqual(writes, [(3, b"TIME SET 2026-10-29T00:59:59\n"),
                                  (5, b"TIME SET 2026-10-29T00:59:59\n")])

    def test_partial_write_is_detected_and_locks_released(self):
        self.writer.partial = True
        with self.assertRaisesRegex(OSError, "incomplete"):
            self.bridge._sync_clock_if_due()
        self.assertEqual(self.bridge.next_clock_sync, 0.0)
        self.assertTrue(self.bridge.control.acquire(blocking=False))
        self.bridge.control.release()
        self.assertTrue(self.bridge.serial_lock.acquire(blocking=False))
        self.bridge.serial_lock.release()
        self.writer.partial = False
        self.assertTrue(self.bridge._sync_clock_if_due())


if __name__ == "__main__":
    unittest.main()
