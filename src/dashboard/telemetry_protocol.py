"""Dependency-free parsers: TEL attitude and independent CMP magnetic heading."""
from __future__ import annotations
import re


def parse_compass(line: str) -> dict | None:
    parts = line.split()
    if len(parts) not in (18, 21, 23) or parts[0] != "CMP":
        return None
    try:
        values = [int(value) for value in parts[1:]]
    except (ValueError, OverflowError):
        return None
    (heading, valid, detected, fresh, calibrated, aligned, collecting, level,
     span_x, span_y, coverage, samples, raw_x, raw_y, raw_z, reference, direction) = values[:17]
    verified, fit_error, tilt = values[17:20] if len(values) >= 20 else (0, -1, -1)
    selftest_result, selftest_status = values[20:22] if len(values) == 22 else (0, 0)
    if (any(flag not in (0, 1) for flag in values[1:8])
            or not 0 <= heading < 3600 or not 0 <= reference < 3600
            or direction not in (-1, 1) or not 0 <= coverage <= 8
            or min(span_x, span_y, samples) < 0
            or max(span_x, span_y, samples) > 2147483647
            or any(abs(raw) > 2147483647 for raw in (raw_x, raw_y, raw_z))
            or verified not in (0, 1) or not -1 <= fit_error <= 2147483647
            or not -1 <= tilt <= 1800 or not 0 <= selftest_result <= 5
            or not 0 <= selftest_status <= 255):
        return None
    # An asserted valid bit must be consistent with the firmware's gates.
    if valid and (not all((detected, fresh, calibrated, aligned, level)) or collecting):
        return None
    return dict(kind="CMP", heading_x10=heading, valid=valid, detected=detected,
                fresh=fresh, calibrated=calibrated, aligned=aligned, collecting=collecting,
                level=level, span_x=span_x, span_y=span_y, coverage=coverage, samples=samples,
                raw_x=raw_x, raw_y=raw_y, raw_z=raw_z, reference_x10=reference, direction=direction,
                direction_verified=verified, fit_error_ppm=fit_error, tilt_deg10=tilt,
                selftest_result=selftest_result, selftest_status=selftest_status)


def parse_traffic(line: str) -> dict | None:
    """Observed board result. Seconds are never inferred from elapsed time."""
    parts = line.split()
    if len(parts) != 9 or parts[0] != "TRAFFIC":
        return None
    if any(re.fullmatch(r"-?[0-9]{1,10}", item) is None for item in parts[1:]):
        return None
    seq, color, valid, confidence, countdown, digit_valid, age, proc = map(int, parts[1:])
    if (not 0 <= seq <= 0xffffffff or not 0 <= color <= 3
            or valid not in (0, 1) or digit_valid not in (0, 1)
            or not 0 <= confidence <= 100 or not -1 <= countdown <= 99
            or not 0 <= age <= 2147483647 or not 0 <= proc <= 2147483647
            or (valid and color == 0)
            or (digit_valid and (not valid or countdown < 0))
            or (not digit_valid and countdown != -1)):
        return None
    return dict(kind="TRAFFIC", frame_seq=seq, color=color, valid=valid,
                confidence=confidence, countdown=countdown, digit_valid=digit_valid,
                age_ms=age, proc_ms=proc)


def _unsigned_fields(parts: list[str]) -> list[int] | None:
    # Limit length before int(): Python accepts underscores, Unicode digits and
    # arbitrary-size values, while this wire format deliberately does not.
    if any(re.fullmatch(r"[0-9]{1,10}", item) is None for item in parts):
        return None
    return [int(item) for item in parts]


def parse_camera(line: str) -> dict | None:
    """Actual PL video state; the browser does not issue or infer a mode."""
    if "\x00" in line:
        return None
    parts = line.split()
    if len(parts) != 7 or parts[0] != "CAMERA":
        return None
    fields = _unsigned_fields(parts[1:])
    if fields is None:
        return None
    seq, mode, live, configured, width, height = fields
    if (seq > 0xffffffff or mode > 3 or live not in (0, 1)
            or configured not in (0, 1) or (width, height) != (640, 480)):
        return None
    return dict(kind="CAMERA", sequence=seq, image_mode=mode, live=live,
                configured=configured, width=width, height=height)


def parse_detection(line: str) -> dict | None:
    """Board-confirmed manual detector state; never infer this from a command."""
    if "\x00" in line:
        return None
    parts = line.split()
    if len(parts) != 3 or parts[0] != "DETECT":
        return None
    fields = _unsigned_fields(parts[1:])
    if fields is None:
        return None
    sequence, enabled = fields
    if sequence > 0xffffffff or enabled not in (0, 1):
        return None
    return dict(kind="DETECT", sequence=sequence, enabled=enabled)


def parse_obstacle(line: str) -> dict | None:
    """Inclusive native-video coordinates. No predicted or web-made boxes."""
    if "\x00" in line:
        return None
    parts = line.split()
    if not 8 <= len(parts) <= 48 or parts[0] != "OBSTACLE":
        return None
    fields = _unsigned_fields(parts[1:])
    if fields is None:
        return None
    seq, source_frame, state, available, age, proc, count = fields[:7]
    if (seq > 0xffffffff or source_frame > 0xffffffff or state > 5
            or available not in (0, 1) or age > 2147483647 or proc > 2147483647
            or count > 8 or len(fields) != 7 + 5 * count
            or (count and (state != 0 or not available or age > 500))):
        return None
    boxes, identities = [], set()
    for offset in range(7, len(fields), 5):
        identity, xmin, ymin, xmax, ymax = fields[offset:offset + 5]
        if (not 1 <= identity <= 65535 or identity in identities
                or not 0 <= xmin <= xmax <= 639 or not 0 <= ymin <= ymax <= 479):
            return None
        identities.add(identity)
        boxes.append(dict(id=identity, xmin=xmin, ymin=ymin, xmax=xmax, ymax=ymax))
    return dict(kind="OBSTACLE", result_seq=seq, source_frame=source_frame,
                state=state, available=available, age_ms=age, proc_ms=proc,
                count=count, boxes=boxes)


def parse_packet(line: str) -> dict | None:
    # Never sanitize corruption into a valid camera/detection packet.
    header = line.lstrip().split(None, 1)[0] if line.strip() else ""
    if header in ("CAMERA", "OBSTACLE", "DETECT"):
        return {"CAMERA": parse_camera, "OBSTACLE": parse_obstacle, "DETECT": parse_detection}[header](line)
    if line.lstrip().startswith("TRAFFIC ") and "\x00" in line:
        return None
    line = line.strip().replace("\x00", "")
    if line.startswith("TRAFFIC "):
        return parse_traffic(line)
    if line.startswith("CMP "):
        return parse_compass(line)
    if "TEL " not in line:
        return None
    parts = line[line.find("TEL "):].split()
    if len(parts) != 15:
        return None
    try:
        values = [int(value) for value in parts[1:]]
    except (ValueError, OverflowError):
        return None
    r, p, heading, rr, pr, yr, temp, rh, hpa, lux, distance, level, imu, mag = values
    if imu not in (0, 1) or mag not in (0, 1) or level not in (0, 1, 2, 3) or not (-3600 <= r <= 3600 and -1800 <= p <= 1800):
        return None
    return {
        "roll": r / 10.0, "pitch": p / 10.0, "yaw": heading % 360,
        "rr": rr / 10.0, "pr": pr / 10.0, "yr": yr / 10.0,
        "temp": None if temp == -9999 else temp / 10.0,
        "rh": None if rh < 0 else rh / 10.0,
        "hpa": None if hpa < 0 else hpa,
        "lux": None if lux < 0 else lux,
        "range_mm": distance, "level": level, "imu": imu, "mag": mag,
        "range_state": ("NO DATA", "SAFE", "WARNING", "DANGER")[level] if distance >= 0 else "NO DATA",
        "heading_mode": "MAG" if mag else "REL",
    }

