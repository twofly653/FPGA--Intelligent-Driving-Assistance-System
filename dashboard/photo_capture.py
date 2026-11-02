"""Native OV5640 JPEG jobs. UART carries metadata; DAP carries image bytes.

No board connection, process, directory or capture is started by importing this
module. Only an explicit LIVE /api/photo request may start a job.
"""
from __future__ import annotations

import io
import os
import re
import socket
import subprocess
import threading
import time
import uuid
import zlib
from collections import deque
from pathlib import Path

MAX_PHOTO = 2 * 1024 * 1024
XSDB = Path(r"D:\FPGA\2026.1\Vivado\bin\xsdb.bat")
HW_SERVER = XSDB.with_name("hw_server.bat")
CREATE_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)
READY = re.compile(r"PHOTO READY ([0-9]{1,10}) ([0-9a-fA-F]{8}) ([0-9]{1,7}) ([0-9]{1,5}) ([0-9]{1,5}) ([0-9a-fA-F]{8})")


def parse_photo_line(line: str) -> dict | None:
    line = line.strip()
    if not line.startswith("PHOTO "):
        return None
    if line.startswith("PHOTO commands:"):
        return None  # Firmware startup help is not a protocol response.
    match = READY.fullmatch(line)
    if match:
        identity, address, size, width, height, crc = match.groups()
        result = dict(kind="ready", board_id=int(identity), address=int(address, 16),
                      size=int(size), width=int(width), height=int(height), crc32=int(crc, 16))
        # Validate the complete rounded read range, never merely its start.
        rounded = (result["size"] + 3) & ~3
        if (result["board_id"] > 0xffffffff or not 4 <= result["size"] <= MAX_PHOTO
                or result["address"] & 3 or result["address"] < 0x00100000
                or result["address"] + rounded > 0x20000000
                or (result["width"], result["height"]) != (2592, 1944)):
            raise ValueError("板卡照片元数据无效：地址、长度或原生尺寸不符合约定")
        return result
    if re.fullmatch(r"PHOTO BUSY [0-9]{1,10}", line):
        identity = int(line.rsplit(" ", 1)[1])
        if identity <= 0xffffffff:
            return dict(kind="busy", board_id=identity)
    if line == "PHOTO IDLE":
        return dict(kind="idle")
    if re.fullmatch(r"PHOTO ERROR [A-Z0-9_]{1,64}", line):
        return dict(kind="error", error=line[12:])
    raise ValueError("板卡照片响应格式无效")


def verify_jpeg(raw: bytes, metadata: dict) -> bytes:
    """CRC checks original bytes; Pillow fully decodes native 2592x1944 JPEG."""
    size = metadata["size"]
    if len(raw) != ((size + 3) & ~3):
        raise ValueError("DAP 读取长度不完整")
    jpeg = raw[:size]
    if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
        raise ValueError("JPEG 起止标记无效")
    if zlib.crc32(jpeg) & 0xffffffff != metadata["crc32"]:
        raise ValueError("照片 CRC32 校验失败，未发布")
    from PIL import Image
    with Image.open(io.BytesIO(jpeg)) as image:
        if image.format != "JPEG" or image.size != (2592, 1944):
            raise ValueError("照片解码尺寸不是原生 2592×1944")
        image.verify()
    with Image.open(io.BytesIO(jpeg)) as image:
        image.load()  # Check entropy payload, not only JPEG headers.
        if image.size != (2592, 1944):
            raise ValueError("JPEG 尺寸不一致")
    return jpeg


def _server_available() -> bool:
    try:
        with socket.create_connection(("127.0.0.1", 3122), timeout=.3):
            return True
    except OSError:
        return False


def dap_read(metadata: dict, work: Path) -> bytes:
    """Read running PS DDR through APU/DAP. Never halt/reset/program either CPU."""
    # Revalidate before generating any Tcl, including when called independently.
    parse_photo_line("PHOTO READY {board_id} {address:08x} {size} {width} {height} {crc32:08x}".format(**metadata))
    if not XSDB.is_file():
        raise RuntimeError("未找到本机 XSDB")
    work.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update(TEMP=str(work), TMP=str(work))
    if not _server_available():
        if not HW_SERVER.is_file():
            raise RuntimeError("未找到本机 hardware server")
        subprocess.Popen([str(HW_SERVER), "-stcp::3122"], cwd=str(work), env=environment,
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, creationflags=CREATE_NO_WINDOW)
        deadline = time.monotonic() + 5
        while not _server_available():
            if time.monotonic() >= deadline:
                raise RuntimeError("本机 3122 hardware server 启动失败")
            time.sleep(.1)
    binary = work / "photo.bin"
    script = work / "read_photo.tcl"
    # Paths are server-owned D: paths; user text is never put into Tcl or cmd.
    capture_path = binary.resolve().as_posix()
    if "{" in capture_path or "}" in capture_path or "\n" in capture_path:
        raise ValueError("照片临时路径无效")
    address, words = metadata["address"], (metadata["size"] + 3) // 4
    script.write_text(
        "if {[catch {\n"
        " connect -url tcp:127.0.0.1:3122\n"
        ' targets -set -filter {name =~ "APU"}\n'
        " configparams force-mem-access 1\n"
        f" mrd -bin -file {{{capture_path}}} 0x{address:08x} {words}\n"
        " configparams force-mem-access 0\n disconnect\n"
        '} message]} { puts stderr $message; catch {disconnect}; exit 1 }\n'
        'puts "PHOTO_DAP_READ_OK"\nexit 0\n', encoding="ascii")
    completed = subprocess.run([str(XSDB), str(script)], cwd=str(work), env=environment,
                               stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=90, creationflags=CREATE_NO_WINDOW)
    (work / "xsdb.log").write_bytes(completed.stdout)
    if completed.returncode or b"PHOTO_DAP_READ_OK" not in completed.stdout:
        raise RuntimeError("DAP 读取照片失败，详见照片任务日志")
    return binary.read_bytes()


class PhotoJobs:
    def __init__(self, bridge, directory: Path, *, capture_timeout=30.0, status_timeout=3.0, read_memory=dap_read):
        self.bridge, self.directory = bridge, Path(directory)
        self.capture_timeout, self.status_timeout = capture_timeout, status_timeout
        self.read_memory = read_memory
        self.condition = threading.Condition()
        self.events = deque(maxlen=80)
        self.sequence = 0
        self.last_board_id = None
        self.state = dict(phase="idle", busy=False, message="准备拍摄原生 500 万像素照片")
        self.published: dict[str, Path] = {}
        self.worker = None

    def accept_line(self, line: str) -> bool:
        try:
            event = parse_photo_line(line)
        except ValueError as exc:
            event = dict(kind="invalid", error=str(exc))
        if event is None:
            return False
        with self.condition:
            self.sequence += 1
            self.events.append((self.sequence, event))
            if event["kind"] == "ready":
                self.last_board_id = event["board_id"]
            self.condition.notify_all()
        return True

    def snapshot(self):
        with self.condition:
            return dict(self.state)

    def _stage(self, phase, message, **values):
        with self.condition:
            self.state.update(phase=phase, message=message, **values)

    def _send(self, command, reader):
        # Serial lifetime and all writes share the existing Bridge locks.
        with self.bridge.control:
            with self.bridge.serial_lock:
                if self.bridge.mode != "LIVE" or not self.bridge.connected or self.bridge.reader is not reader:
                    raise RuntimeError("真实板卡连接已断开，照片任务终止")
                with self.condition:
                    sequence = self.sequence
                data = command.encode("ascii") + b"\n"
                count = reader.write(data)
                if count is not None and count != len(data):
                    raise RuntimeError("照片命令发送不完整")
        return sequence

    def start(self):
        with self.bridge.serial_lock:
            if self.bridge.mode != "LIVE" or not self.bridge.connected or self.bridge.reader is None:
                raise RuntimeError("请先连接真实板卡；回放和模拟场景不能拍照")
            reader = self.bridge.reader
        with self.condition:
            if self.state.get("busy"):
                raise RuntimeError("已有照片任务正在进行")
            token = uuid.uuid4().hex
            self.state = dict(phase="arming", busy=True, job_id=token, message="正在查询板卡照片状态")
            self.worker = threading.Thread(target=self._run, args=(token, reader), daemon=True)
            self.worker.start()
            return dict(self.state)

    def _wait(self, after, deadline, reader):
        while True:
            with self.condition:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("等待板卡照片响应超时")
                for sequence, event in self.events:
                    if sequence > after:
                        return sequence, event
                self.condition.wait(min(.1, remaining))
            with self.bridge.serial_lock:
                if not self.bridge.connected or self.bridge.mode != "LIVE" or self.bridge.reader is not reader:
                    raise RuntimeError("板卡串口连接中断")

    def _run(self, token, reader):
        try:
            sequence = self._send("PHOTO STATUS", reader)
            sequence, baseline = self._wait(sequence, time.monotonic()+self.status_timeout, reader)
            if baseline["kind"] == "busy":
                raise RuntimeError("板卡已有拍照任务，请稍后重试")
            if baseline["kind"] == "invalid":
                raise ValueError(baseline["error"])
            self._stage("capturing", "正在切换原生 5MP JPEG 并拍摄；HDMI 与姿态会短暂停顿")
            sequence = self._send("PHOTO SNAP", reader)
            deadline = time.monotonic()+self.capture_timeout
            expected_id = None
            while True:
                sequence, event = self._wait(sequence, deadline, reader)
                if event["kind"] in ("error", "invalid"):
                    raise RuntimeError(event["error"])
                if event["kind"] == "busy":
                    expected_id = event["board_id"]
                elif event["kind"] == "ready":
                    # A request-local BUSY -> READY transition distinguishes
                    # fresh output even when ARM reboot reset the photo ID.
                    # Stale READY arriving before BUSY is never accepted.
                    if expected_id is None or event["board_id"] != expected_id:
                        continue
                    metadata = event
                    break
            self._stage("transferring", "拍摄完成，正在读取板卡 DDR 原始 JPEG；实时遥测继续")
            work = self.directory / ".work" / token
            raw = self.read_memory(metadata, work)
            self._stage("verifying", "正在校验 CRC32 并完整解码 2592×1944 JPEG")
            jpeg = verify_jpeg(raw, metadata)
            identity = f"{metadata['board_id']}-{token[:12]}"
            self.directory.mkdir(parents=True, exist_ok=True)
            path = self.directory / (identity+".jpg")
            temporary = self.directory / (identity+".part")
            temporary.write_bytes(jpeg)
            temporary.replace(path)
            photo = dict(id=identity, board_id=metadata["board_id"], width=2592, height=1944,
                         size=len(jpeg), crc32=f"{metadata['crc32']:08x}", verified=True,
                         verified_at_ms=round(time.time()*1000),
                         url=f"/photos/{identity}.jpg", filename=f"OV5640_2592x1944_{identity}.jpg")
            with self.condition:
                self.published[identity] = path
                self.state.update(phase="ready", busy=False, message="原生 500 万像素照片已校验", photo=photo)
        except Exception as exc:
            self._stage("error", str(exc), busy=False, error=str(exc))

    def photo_path(self, identity):
        with self.condition:
            return self.published.get(identity)
