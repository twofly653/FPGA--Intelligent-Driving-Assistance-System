#!/usr/bin/env python3
"""Local UART dashboard bridge. Python standard library; no pip required on Windows.

python server.py COM10
python server.py                       # choose a port in the browser
python server.py --replay trace.txt --http-port 8879
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import socket
import threading
import time
from collections import deque
from datetime import datetime, timedelta, timezone
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, unquote

from telemetry_protocol import parse_packet
from photo_capture import PhotoJobs

ROOT = Path(__file__).resolve().parent
DRIVESENSE_ROOT = ROOT / "drivesense"
SHANGHAI_TIMEZONE = timezone(timedelta(hours=8), name="Asia/Shanghai")
CLOCK_SYNC_SECONDS = 60.0
STALE_SECONDS = 0.5
TRAFFIC_STALE_MS = 1500
CAMERA_STALE_MS = 1000
# The board's own state field is the authority on result freshness: it flips to
# STALE (3) about 500 ms after the last accepted PC result. This window only
# has to notice a dead link, and 500 ms made the panels flicker because the
# board reports a capture-to-telemetry age that sits near 400 ms even when the
# link is healthy.
OBSTACLE_STALE_MS = 1000
DETECTION_STALE_MS = 1500
# A counter that jumps backwards after this long without a frame is a board
# restart whose boot banner was lost, not a late duplicate.
EPOCH_RESET_SECONDS = 2.0
BOOT_BANNER = re.compile(r"CAMERA_(?:SYNC|OBSTACLE|INTEGRATED|OPTIMIZED)_[A-Z0-9_]+ / [^\r\n\x00]*")


def _forward_sequence(candidate: int, previous: int | None) -> bool:
    """Order uint32 counters across wrap; reboot is an explicit new epoch."""
    return previous is None or 0 < ((candidate - previous) & 0xffffffff) < 0x80000000


def _accept_sequence(candidate: int, previous, received, now: float) -> bool:
    """True when a video/obstacle counter belongs to the current epoch.

    A board CPU restart restarts every counter at 1, and the boot banner is not
    always captured because the first characters after rst -system are lost.
    Without this, a stream that is already stale would reject every new frame
    forever: the cached high-water mark sits above anything the fresh board can
    reach for minutes, and the camera/detection panels stay empty. A backwards
    jump on an already-stale stream is therefore a new epoch, not a duplicate.
    """
    if _forward_sequence(candidate, previous):
        return True
    if previous is None or candidate >= previous:
        return False
    return received is None or now - received > EPOCH_RESET_SECONDS


def serial_ports() -> list[str]:
    """List ports without opening them, including without pyserial on Windows."""
    if os.name == "nt":
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM") as key:
                return sorted({str(winreg.EnumValue(key, i)[1]) for i in range(winreg.QueryInfoKey(key)[1])})
        except OSError:
            return []
    try:
        from serial.tools import list_ports
        return [p.device for p in list_ports.comports()]
    except ImportError:
        return []


class WindowsSerial:
    """115200 8N1 reader: read one byte then drain the queue, avoiding read(256)
    timeouts. Only allowlisted clock, calibration and detector-control writes; no DTR/RTS toggling.
    """
    def __init__(self, port: str):
        from ctypes import wintypes as wt

        class DCB(ctypes.Structure):
            _fields_ = [("length", wt.DWORD), ("baud", wt.DWORD), ("flags", wt.DWORD),
                        ("reserved", wt.WORD), ("xon_limit", wt.WORD), ("xoff_limit", wt.WORD),
                        ("byte_size", wt.BYTE), ("parity", wt.BYTE), ("stop_bits", wt.BYTE),
                        ("xon", ctypes.c_char), ("xoff", ctypes.c_char), ("error", ctypes.c_char),
                        ("eof", ctypes.c_char), ("event", ctypes.c_char), ("reserved1", wt.WORD)]

        class Timeouts(ctypes.Structure):
            _fields_ = [(n, wt.DWORD) for n in ("interval", "multiplier", "constant", "write_multiplier", "write_constant")]

        class Stat(ctypes.Structure):
            _fields_ = [("flags", wt.DWORD), ("in_queue", wt.DWORD), ("out_queue", wt.DWORD)]

        self.wt, self.Stat = wt, Stat
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD, wt.HANDLE]
        self.api.CreateFileW.restype = wt.HANDLE
        self.api.GetCommState.argtypes = [wt.HANDLE, ctypes.POINTER(DCB)]
        self.api.SetCommState.argtypes = [wt.HANDLE, ctypes.POINTER(DCB)]
        self.api.SetCommTimeouts.argtypes = [wt.HANDLE, ctypes.POINTER(Timeouts)]
        self.api.ReadFile.argtypes = [wt.HANDLE, wt.LPVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]
        self.api.WriteFile.argtypes = [wt.HANDLE, wt.LPCVOID, wt.DWORD, ctypes.POINTER(wt.DWORD), wt.LPVOID]
        self.api.ClearCommError.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD), ctypes.POINTER(Stat)]
        self.api.CloseHandle.argtypes = [wt.HANDLE]
        self.handle = self.api.CreateFileW("\\\\.\\" + port, 0xC0000000, 0, None, 3, 0, None)
        if self.handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            dcb = DCB()
            dcb.length = ctypes.sizeof(DCB)
            self._check(self.api.GetCommState(self.handle, ctypes.byref(dcb)))
            dcb.baud, dcb.byte_size, dcb.parity, dcb.stop_bits = 115200, 8, 0, 0
            dcb.flags = 1  # fBinary; no parity, RTS/DTR or flow control
            self._check(self.api.SetCommState(self.handle, ctypes.byref(dcb)))
            self._check(self.api.SetCommTimeouts(self.handle, ctypes.byref(Timeouts(1, 0, 50, 0, 50))))
        except BaseException:
            self.close()
            raise

    @staticmethod
    def _check(ok):
        if not ok:
            raise ctypes.WinError(ctypes.get_last_error())

    def _read(self, size: int) -> bytes:
        data = ctypes.create_string_buffer(size)
        count = self.wt.DWORD()
        self._check(self.api.ReadFile(self.handle, data, size, ctypes.byref(count), None))
        return data.raw[:count.value]

    def read_chunk(self) -> bytes:
        data = self._read(1)
        errors, stat = self.wt.DWORD(), self.Stat()
        self._check(self.api.ClearCommError(self.handle, ctypes.byref(errors), ctypes.byref(stat)))
        if stat.in_queue:
            data += self._read(min(stat.in_queue, 4096))
        return data

    def write(self, data: bytes):
        count = self.wt.DWORD()
        self._check(self.api.WriteFile(self.handle, data, len(data), ctypes.byref(count), None))
        if count.value != len(data):
            raise OSError("串口命令发送不完整，请重试")

    def close(self):
        if self.handle is not None:
            self.api.CloseHandle(self.handle)
            self.handle = None


class Bridge:
    def __init__(self):
        self.changed = threading.Condition()
        self.packet: dict = {"imu": 0, "mag": 0}
        self.compass: dict = {}
        self.compass_received = 0.0
        self.traffic: dict = {}
        self.traffic_received = 0.0
        self.camera: dict = {}
        self.camera_received: float | None = None
        self.obstacles: dict = {}
        self.obstacles_received: float | None = None
        self.detection: dict = {}
        self.detection_received: float | None = None
        self.detection_obstacle_floor: int | None = None
        self.received = 0.0
        self.arrivals: deque[float] = deque(maxlen=100)
        self.sequence = 0
        self.video_epoch = 0
        self.connected = False
        self.port = ""
        self.error = ""
        self.mode = "LIVE"
        self.stop = threading.Event()
        self.worker: threading.Thread | None = None
        self.control = threading.Lock()
        # Register/close and all command writes share one lock. A close cannot
        # invalidate the native handle part way through a command.
        self.serial_lock = threading.Lock()
        self.reader = None
        self.next_clock_sync = 0.0
        self.photo = PhotoJobs(self, ROOT / "photos")

    def snapshot(self) -> dict:
        with self.changed:
            now = time.monotonic()
            age = now - self.received if self.received else None
            obj = dict(self.packet)
            obj.update(seq=self.sequence, video_epoch=self.video_epoch, port=self.port, connected=self.connected,
                       error=self.error, mode=self.mode, stale=age is None or age > STALE_SECONDS,
                       age_ms=round(age * 1000, 1) if age is not None else None,
                       rx_hz=round((len(self.arrivals) - 1) / (self.arrivals[-1] - self.arrivals[0]), 1)
                       if len(self.arrivals) > 1 and self.arrivals[-1] > self.arrivals[0] else 0.0)
            if obj["stale"] or not self.connected:
                obj["imu"] = obj["mag"] = 0
            compass_age = time.monotonic() - self.compass_received if self.compass_received else None
            compass = dict(self.compass)
            compass_stale = compass_age is None or compass_age > STALE_SECONDS
            compass.update(stale=compass_stale, age_ms=round(compass_age * 1000, 1) if compass_age is not None else None)
            compass["valid"] = int(bool(compass.get("valid") and self.connected and not compass_stale))
            obj["compass"] = compass
            traffic = dict(self.traffic)
            traffic_age = (max(0.0, time.monotonic() - self.traffic_received) * 1000
                           + traffic.get("age_ms", 0)) if self.traffic_received else None
            traffic_stale = traffic_age is None or traffic_age > TRAFFIC_STALE_MS
            traffic.update(stale=traffic_stale, age_ms=round(traffic_age, 1) if traffic_age is not None else None,
                           source="REPLAY" if self.mode == "REPLAY" else "BOARD_UART")
            traffic["valid"] = int(bool(traffic.get("valid") and self.connected and not traffic_stale))
            if not traffic["valid"]:
                traffic.update(color=0, countdown=-1, digit_valid=0, confidence=0)
            obj["traffic"] = traffic
            source = "REPLAY" if self.mode == "REPLAY" else "BOARD_UART"
            camera = dict(kind="CAMERA", sequence=None, image_mode=None,
                          live=0, configured=0, width=640, height=480)
            camera.update(self.camera)
            camera_age = max(0.0, now - self.camera_received) * 1000 if self.camera_received is not None else None
            camera_stale = not self.connected or camera_age is None or camera_age > CAMERA_STALE_MS
            camera.update(stale=camera_stale, age_ms=round(camera_age, 1) if camera_age is not None else None,
                          source=source)
            if camera_stale:
                camera.update(live=0, configured=0)
            obj["camera"] = camera
            detection = dict(kind="DETECT", sequence=None, enabled=None)
            detection.update(self.detection)
            detection_age = max(0.0, now - self.detection_received) * 1000 if self.detection_received is not None else None
            detection_stale = not self.connected or detection_age is None or detection_age > DETECTION_STALE_MS
            detection.update(supported=bool(self.detection), stale=detection_stale,
                             age_ms=round(detection_age, 1) if detection_age is not None else None,
                             source=source,
                             can_control=bool(self.mode == "LIVE" and self.connected
                                              and self.reader is not None and not detection_stale))
            if detection_stale:
                detection["enabled"] = None
            obj["detection"] = detection
            obstacles = dict(kind="OBSTACLE", result_seq=None, source_frame=None,
                             state=5, available=0, age_ms=None, proc_ms=None, count=0, boxes=[])
            obstacles.update(self.obstacles)
            obstacle_age = (max(0.0, now - self.obstacles_received) * 1000 + self.obstacles.get("age_ms", 0)) if self.obstacles_received is not None else None
            usable = bool(self.connected and not camera_stale and camera["live"] and camera["configured"]
                          and obstacle_age is not None and obstacle_age <= OBSTACLE_STALE_MS
                          and obstacles["available"] and obstacles["state"] == 0
                          and (not self.detection or (not detection_stale and detection["enabled"] == 1
                               and _forward_sequence(obstacles["result_seq"], self.detection_obstacle_floor))))
            obstacles.update(stale=not usable, valid=usable,
                             age_ms=round(obstacle_age, 1) if obstacle_age is not None else None, source=source)
            if usable:
                obstacles["boxes"] = [dict(box) for box in obstacles["boxes"]]
            else:
                obstacles.update(count=0, boxes=[])
            obj["obstacles"] = obstacles
            return obj

    def publish(self, packet: dict):
        with self.changed:
            now = time.monotonic()
            if packet.get("kind") == "CAMERA":
                if _accept_sequence(packet["sequence"], self.camera.get("sequence"),
                                    self.camera_received, now):
                    self.camera = dict(packet)
                    self.camera_received = now
            elif packet.get("kind") == "DETECT":
                if _accept_sequence(packet["sequence"], self.detection.get("sequence"),
                                    self.detection_received, now):
                    # A disable/enable transition must not resurrect a cached
                    # pre-disable HDMI table. Wait for a newer result sequence.
                    if packet["enabled"] != self.detection.get("enabled"):
                        self.detection_obstacle_floor = self.obstacles.get("result_seq")
                    self.detection = dict(packet)
                    self.detection_received = now
            elif packet.get("kind") == "OBSTACLE":
                if _accept_sequence(packet["result_seq"], self.obstacles.get("result_seq"),
                                    self.obstacles_received, now):
                    if (self.detection_obstacle_floor is not None
                            and not _forward_sequence(packet["result_seq"], self.detection_obstacle_floor)):
                        self.detection_obstacle_floor = None
                    self.obstacles = dict(packet, boxes=[dict(box) for box in packet["boxes"]])
                    self.obstacles_received = now
            elif packet.get("kind") == "TRAFFIC":
                # A frozen result retransmitted with the same frame sequence
                # must not become fresh again. Only new observations reset age.
                if packet.get("frame_seq") != self.traffic.get("frame_seq"):
                    self.traffic = dict(packet)
                    self.traffic_received = time.monotonic()
            elif packet.get("kind") == "CMP":
                self.compass = packet
                self.compass_received = time.monotonic()
            else:
                self.packet = packet
                self.received = time.monotonic()
                self.arrivals.append(self.received)
            self.sequence += 1
            self.changed.notify_all()

    def _clear_video_locked(self):
        self.video_epoch += 1
        self.next_clock_sync = 0.0
        self.camera, self.obstacles, self.detection = {}, {}, {}
        self.camera_received = self.obstacles_received = self.detection_received = None
        self.detection_obstacle_floor = None

    def board_boot(self, line: str) -> bool:
        """Recognized firmware startup resets sequence epochs after CPU restart."""
        if BOOT_BANNER.fullmatch(line.strip()) is None:
            return False
        with self.changed:
            self._clear_video_locked()
            self.traffic, self.compass = {}, {}
            self.traffic_received = self.compass_received = self.received = 0.0
            self.packet = {"imu": 0, "mag": 0}
            self.arrivals.clear()
            self.sequence += 1
            self.changed.notify_all()
        return True

    def _status(self, connected: bool, error: str = ""):
        with self.changed:
            self.connected, self.error = connected, error
            if not connected:
                self._clear_video_locked()
            self.changed.notify_all()

    def connect(self, port: str):
        port = port.strip()
        if not port or (os.name == "nt" and not (port.upper().startswith("COM") and port[3:].isdigit())):
            raise ValueError("请选择有效的 COM 串口")
        with self.control:
            self._disconnect()
            self.port, self.mode = port, "LIVE"
            self.worker = threading.Thread(target=self._serial, args=(port, self.stop), daemon=True)
            self.worker.start()

    def _disconnect(self):
        self.stop.set()
        if self.worker and self.worker.is_alive():
            self.worker.join(timeout=1.0)
            if self.worker.is_alive():
                raise RuntimeError("串口正在关闭，请稍后重试")
        self.stop = threading.Event()
        with self.changed:
            self.connected = False
            self.packet = {"imu": 0, "mag": 0}
            self.compass = {}
            self.compass_received = 0.0
            self.traffic = {}
            self.traffic_received = 0.0
            self._clear_video_locked()
            self.received = 0.0
            self.arrivals.clear()
            self.error = ""
            self.changed.notify_all()

    def disconnect(self):
        with self.control:
            self._disconnect()

    def compass_command(self, obj: dict):
        action = obj.get("action")
        if not isinstance(action, str):
            raise ValueError("不支持的指南针操作")
        commands = {"begin": b"CAL BEGIN\n", "end": b"CAL END\n", "flip": b"CAL FLIP\n",
                    "cancel": b"CAL CANCEL\n", "confirm": b"CAL CONFIRM\n"}
        if action == "ref":
            heading = obj.get("heading_x10")
            if type(heading) is not int or not 0 <= heading < 3600:
                raise ValueError("参考角必须为 0.0–359.9°，精度 0.1°")
            data = f"CAL REF {heading}\n".encode("ascii")
        elif action in commands:
            data = commands[action]
        else:
            raise ValueError("不支持的指南针操作")
        with self.control:
            with self.serial_lock:
                if self.mode != "LIVE" or not self.connected or self.reader is None:
                    raise RuntimeError("请先连接真实板卡；回放与演示不发送校准命令")
                count = self.reader.write(data)
                if count is not None and count != len(data):
                    raise RuntimeError("串口命令发送不完整，请重试")

    def obstacle_command(self, obj: dict):
        if set(obj) != {"enabled"} or type(obj.get("enabled")) is not bool:
            raise ValueError("识别控制只接受 enabled: true 或 false")
        data = b"OBSTACLE ON\n" if obj["enabled"] else b"OBSTACLE OFF\n"
        with self.control:
            with self.serial_lock:
                if self.mode != "LIVE" or not self.connected or self.reader is None:
                    raise RuntimeError("请先连接真实板卡；回放不发送识别控制命令")
                with self.changed:
                    age = (time.monotonic() - self.detection_received) * 1000 if self.detection_received is not None else None
                    if not self.detection or age is None or age > DETECTION_STALE_MS:
                        raise RuntimeError("板端未提供有效识别开关状态，请使用新版固件并等待状态同步")
                count = self.reader.write(data)
                if count is not None and count != len(data):
                    raise RuntimeError("串口命令发送不完整，请重试")
        # State is updated only by a subsequent DETECT report, never the POST.


    def _sync_clock_if_due(self):
        """Send local civil time only on a live, talking board; never in replay.

        Nonblocking locks are intentional: disconnect joins this RX worker
        while holding control. Waiting here for control would deadlock it.
        The PHOTO path uses the same serial lifetime/write lock.
        """
        now = time.monotonic()
        if now < self.next_clock_sync or not self.control.acquire(blocking=False):
            return False
        try:
            if not self.serial_lock.acquire(blocking=False):
                return False
            try:
                with self.changed:
                    if (self.mode != "LIVE" or not self.connected or self.reader is None
                            or not self.received or now - self.received > STALE_SECONDS):
                        return False
                # Photo start also takes serial_lock before setting busy.
                if self.photo.snapshot().get("busy", False):
                    return False
                local = datetime.now(SHANGHAI_TIMEZONE)
                data = local.strftime("TIME SET %Y-%m-%dT%H:%M:%S\n").encode("ascii")
                count = self.reader.write(data)
                if count is not None and count != len(data):
                    raise OSError("Clock synchronization write incomplete")
                self.next_clock_sync = now + CLOCK_SYNC_SECONDS
                return True
            finally:
                self.serial_lock.release()
        finally:
            self.control.release()

    def _serial(self, port: str, stop: threading.Event):
        reader = None
        try:
            if os.name == "nt":
                reader = WindowsSerial(port)
            else:
                import serial
                reader = serial.Serial(port, 115200, timeout=0.05)
            with self.serial_lock:
                self.reader = reader
            self._status(True)
            print(f"[UART] {port} @115200 8N1", flush=True)
            buf = b""
            while not stop.is_set():
                chunk = reader.read_chunk() if os.name == "nt" else reader.read(1) + reader.read(reader.in_waiting)
                buf += chunk
                if len(buf) > 8192:
                    buf = buf[-4096:]
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    # Replacement preserves corruption so strict parsers reject
                    # it; ignoring bytes can silently turn 1<bad>0 into 10.
                    decoded = line.decode("ascii", errors="replace")
                    if self.board_boot(decoded):
                        continue
                    if re.fullmatch(r"\[PHOTO\] settle frames=[0-9]+ ms=[0-9]+ result=[A-Z0-9_]+", decoded.strip()):
                        print(decoded.strip(), flush=True)
                    if self.photo.accept_line(decoded):
                        continue
                    packet = parse_packet(decoded)
                    if packet is not None:
                        self.publish(packet)
                self._sync_clock_if_due()
        except Exception as exc:
            text = f"{port}: {exc}"
            if isinstance(exc, PermissionError) or getattr(exc, "winerror", 0) == 5:
                text = f"{port} 已被占用，请关闭 Vitis Serial Terminal 或其他串口程序后重新连接。"
            self._status(False, text)
            print(f"[UART] {text}", flush=True)
        finally:
            with self.serial_lock:
                if self.reader is reader:
                    self.reader = None
                if reader:
                    reader.close()
            with self.changed:
                self.connected = False
                self._clear_video_locked()
                self.changed.notify_all()

    def replay(self, path: Path, hz: float, loop: bool):
        packets = []
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            if BOOT_BANNER.fullmatch(line.strip()):
                packets.append(line)
            elif (packet := parse_packet(line)) is not None:
                packets.append(packet)
        if not any(isinstance(packet, dict) for packet in packets):
            raise ValueError("回放文件中没有有效 TEL / CMP / TRAFFIC / CAMERA / OBSTACLE / DETECT 帧")
        with self.control:
            self._disconnect()
            self.mode, self.port = "REPLAY", path.name
            self._status(True)
            stop = self.stop

            def run():
                while not stop.is_set():
                    # A loop is an explicit replay epoch, never a heartbeat for
                    # the previous loop's frozen observation.
                    with self.changed:
                        self._clear_video_locked()
                    deadline = time.monotonic()
                    for packet in packets:
                        if stop.is_set():
                            return
                        if isinstance(packet, str):
                            self.board_boot(packet)
                        else:
                            self.publish(packet)
                        deadline += 1.0 / hz
                        if stop.wait(max(0.0, deadline - time.monotonic())):
                            return
                    if not loop:
                        return
            self.worker = threading.Thread(target=run, daemon=True)
            self.worker.start()


def make_handler(bridge: Bridge):
    class Handler(SimpleHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def setup(self):
            super().setup()
            self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

        def handle(self):
            try:
                super().handle()
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(ROOT), **kwargs)

        def log_message(self, fmt, *args):
            pass

        def _json(self, value, status=200):
            body = json.dumps(value, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/api/status":
                self._json(bridge.snapshot())
            elif path == "/api/photo":
                self._json(bridge.photo.snapshot())
            elif "photos" in unquote(path).replace("\\", "/").split("/"):
                self._photo(path)
            elif path == "/api/ports":
                self._json({"ports": serial_ports()})
            elif path == "/events":
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Cache-Control", "no-cache, no-transform")
                self.send_header("Connection", "keep-alive")
                self.end_headers()
                try:
                    while True:
                        obj = bridge.snapshot()
                        self.wfile.write(("data: " + json.dumps(obj) + "\n\n").encode("utf-8"))
                        self.wfile.flush()
                        with bridge.changed:
                            if bridge.sequence == obj["seq"]:
                                bridge.changed.wait(timeout=0.2)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    pass
            elif path == "/legacy" or path.startswith("/legacy/"):
                self.path = self.path[len("/legacy"):] or "/index.html"
                super().do_GET()
            elif not path.startswith("/api/"):
                self.directory = str(DRIVESENSE_ROOT)
                super().do_GET()
            else:
                self.send_error(404)

        def _photo(self, path, head=False):
            match = re.fullmatch(r"/photos/([0-9]{1,10}-[0-9a-f]{12})\.jpg", path)
            source = bridge.photo.photo_path(match.group(1)) if match else None
            if source is None or not source.is_file():
                self.send_error(404)
                return
            body = source.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "private, max-age=3600")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            if not head:
                self.wfile.write(body)

        def do_HEAD(self):
            path = urlparse(self.path).path
            if "photos" in unquote(path).replace("\\", "/").split("/"):
                self._photo(path, head=True)
            else:
                super().do_HEAD()

        def do_POST(self):
            origin = self.headers.get("Origin")
            if origin and origin != f"http://{self.headers.get('Host')}":
                self.close_connection = True
                self._json({"error": "Origin rejected"}, 403)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 <= length <= 1024:
                    raise ValueError("Invalid request")
                obj = json.loads(self.rfile.read(length) or b"{}")
                if not isinstance(obj, dict):
                    raise ValueError("Invalid request")
                if self.path == "/api/connect":
                    bridge.connect(str(obj.get("port", "")))
                elif self.path == "/api/disconnect":
                    bridge.disconnect()
                elif self.path == "/api/compass":
                    bridge.compass_command(obj)
                elif self.path == "/api/obstacle/control":
                    bridge.obstacle_command(obj)
                elif self.path == "/api/photo":
                    if obj:
                        raise ValueError("拍照请求必须为空对象")
                    self._json(bridge.photo.start(), 202)
                    return
                else:
                    self._json({"error": "Not found"}, 404)
                    return
                self._json(bridge.snapshot())
            except (ValueError, RuntimeError, OSError) as exc:
                self._json({"error": str(exc)}, 400)

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="COM port (optional; choose in browser)")
    parser.add_argument("--http-port", type=int, default=8765)
    parser.add_argument("--replay", type=Path, help="TEL log replay, never opens serial")
    parser.add_argument("--replay-hz", type=float, default=25.0)
    parser.add_argument("--loop", action="store_true")
    parser.add_argument("--open", action="store_true", help="open the original DriveSense page in the default browser")
    args = parser.parse_args()
    if not 1.0 <= args.replay_hz <= 200.0:
        parser.error("--replay-hz must be 1..200")
    if args.port and args.replay:
        parser.error("port and --replay cannot be combined")
    bridge = Bridge()
    httpd = ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(bridge))
    httpd.daemon_threads = True
    if args.replay:
        bridge.replay(args.replay, args.replay_hz, args.loop)
    elif args.port:
        bridge.connect(args.port)
    print(f"[HTTP] http://127.0.0.1:{args.http_port}/  [{bridge.mode}]", flush=True)
    if args.open:
        import webbrowser
        threading.Timer(0.2, lambda: webbrowser.open(f"http://127.0.0.1:{args.http_port}/#vehicle")).start()
    try:
        httpd.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        bridge.disconnect()
        httpd.server_close()


if __name__ == "__main__":
    main()
