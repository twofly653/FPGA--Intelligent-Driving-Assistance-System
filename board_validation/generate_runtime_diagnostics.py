"""Generate bounded, read-only XSCT diagnostics for the current ARM ELF.

This generator never connects to hardware. Re-run it after every firmware build.
ELF symbol names are qualified by their STT_FILE owner, so duplicate static names
such as g_ready cannot accidentally resolve to another service.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

REQUESTS = {
    "collision_ready": ("collision_service.c", "g_ready"),
    "collision_state": ("collision_service.c", "g_state"),
    "tof_sensor": ("vl53l0x.c", "g_sensor"),
    "tof_init_diagnostic": ("vl53l0x.c", "g_init_diag"),
    "tof_running": ("vl53l0x.c", "g_running"),
    "tof_io_error": ("vl53l0x_platform.c", "g_io_error"),
    "clock_valid": ("ui_clock.c", "g_valid"),
    "home_clock_text": ("ui_home.c", "g_last_clock"),
    "ui_page": ("ui_settings.c", "g_page"),
    "small_font_pixels": ("ui_draw.c", "ui_font_pixels_0"),
    "small_font_width": ("ui_draw.c", "ui_font_width_0"),
    "small_font_offset": ("ui_draw.c", "ui_font_offset_0"),
}
OPTIONAL_REQUESTS = {"tof_reset_diagnostic": ("vl53l0x_reset.c", "g_reset_diag")}
# Current ST VL53L0X_Dev_t ABI: Data.LastRangeMeasure begins at byte 80.
# These are field offsets within g_sensor, never absolute RAM addresses.
TOF_FIELDS = {
    "range_status": {"offset": 104, "mask": 0xFF},
    "range_mm": {"offset": 88, "mask": 0xFFFF},
    "signal_16_16": {"offset": 92, "mask": 0xFFFFFFFF},
    "ambient_16_16": {"offset": 96, "mask": 0xFFFFFFFF},
}
HARDWARE = {"camera_control": 0x40000078,
            "camera_identity_mode": 0x4000005C,
            "lcd_tx_status": 0x40000050}


def cstring(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        raise ValueError("String-table offset out of bounds")
    end = data.find(b"\0", offset)
    if end < 0:
        raise ValueError("Unterminated ELF string")
    return data[offset:end].decode("ascii", errors="replace")


def local_symbols(data: bytes) -> list[dict]:
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Expected a little-endian ELF32 file")
    if struct.unpack_from("<H", data, 18)[0] != 40:
        raise ValueError("Expected an ARM ELF")
    shoff = struct.unpack_from("<I", data, 32)[0]
    shsize, shcount, shstridx = struct.unpack_from("<HHH", data, 46)
    if shsize < 40 or not shcount or shstridx >= shcount:
        raise ValueError("Unsupported ELF section table")
    if shoff + shsize * shcount > len(data):
        raise ValueError("Truncated ELF section table")
    sections = [struct.unpack_from("<10I", data, shoff + i * shsize)
                for i in range(shcount)]

    def section_bytes(section):
        offset, length = section[4], section[5]
        if offset + length > len(data):
            raise ValueError("Truncated ELF section data")
        return data[offset:offset + length]

    section_names = section_bytes(sections[shstridx])
    result = []
    for section in sections:
        if section[1] != 2:  # SHT_SYMTAB
            continue
        if section[6] >= shcount or section[9] < 16 or section[5] % section[9]:
            raise ValueError("Invalid ELF symbol table")
        table = section_bytes(section)
        strings = section_bytes(sections[section[6]])
        owner = ""
        for offset in range(0, len(table), section[9]):
            nameoff, address, size, info, other, index = struct.unpack_from(
                "<IIIBBH", table, offset)
            name = cstring(strings, nameoff)
            symbol_type, binding = info & 15, info >> 4
            if symbol_type == 4:  # STT_FILE
                owner = name.replace("\\", "/").rsplit("/", 1)[-1]
            if symbol_type != 1 or binding != 0 or not size:  # local STT_OBJECT
                continue
            if index == 0 or index >= shcount:
                continue
            sec = sections[index]
            if not (sec[3] <= address and address + size <= sec[3] + sec[5]):
                raise ValueError(f"Object {owner}:{name} exceeds its ELF section")
            result.append({"file": owner, "name": name, "address": address,
                           "address_hex": f"0x{address:08X}", "size": size,
                           "section": cstring(section_names, sec[0])})
    if not result:
        raise ValueError("No local object symbols; use the unstripped release ELF")
    return result


def render_tcl(symbols: dict, elf_sha256: str) -> tuple[str, int]:
    scalar_keys = ["collision_ready", "tof_running", "tof_io_error",
                   "clock_valid", "ui_page"]
    for key in scalar_keys:
        if symbols[key]["size"] not in (1, 2, 4):
            raise ValueError(f"Unexpected scalar size for {key}: {symbols[key]['size']}")
    if symbols["collision_state"]["size"] > 64:
        raise ValueError("Refusing unexpectedly large collision-state read")
    if symbols["home_clock_text"]["size"] > 32:
        raise ValueError("Refusing unexpectedly large clock-text read")
    sensor = symbols["tof_sensor"]
    if any(spec["offset"] + 4 > sensor["size"] for spec in TOF_FIELDS.values()):
        raise ValueError("ToF cache offsets exceed g_sensor; verify the current ABI")
    lines = [
        "# Generated from the current ARM ELF; regenerate after every build.",
        "# ELF SHA256: " + elf_sha256,
        "# Bounded live-state reads only. No halt, reset, download, or framebuffer dump.",
        "# Font arrays are listed as metadata only and are NOT read from hardware.",
        "proc read_u32 {address} {",
        "    return [expr {[mrd -value $address] & 0xffffffff}]",
        "}",
        "proc read_ascii {address size} {",
        '    set result ""',
        "    for {set i 0} {$i < $size} {incr i 4} {",
        "        set value [read_u32 [expr {$address + $i}]]",
        "        for {set b 0} {$b < 4 && $i + $b < $size} {incr b} {",
        "            set ch [expr {($value >> (8 * $b)) & 0xff}]",
        "            if {$ch == 0} {return $result}",
        '            if {$ch >= 32 && $ch <= 126} {append result [format %c $ch]} else {append result "?"}',
        "        }",
        "    }",
        "    return $result",
        "}",
        "if {[catch {",
        "    connect -url tcp:127.0.0.1:3122",
        '    targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}',
        "    configparams force-mem-access 1",
    ]
    read_bytes = 0
    for key in scalar_keys:
        sym = symbols[key]
        mask = (1 << (sym["size"] * 8)) - 1
        lines.append(f'    puts "{key.upper()} [expr {{[read_u32 {sym["address_hex"]}] & 0x{mask:X}}}]"')
        read_bytes += 4
    diagnostic = symbols["tof_init_diagnostic"]
    if diagnostic["size"] != 28:
        raise ValueError("Unexpected ToF init diagnostic ABI")
    init_fields = ["stage", "result", "api_status", "io_error", "reset_enabled",
                   "reset_io_seen", "reset_read_nacks"]
    for index, name in enumerate(init_fields):
        address = diagnostic["address"] + index * 4
        lines.append(f'    set init_value [read_u32 0x{address:08X}]')
        if name in ("result", "api_status"):
            lines.append('    if {$init_value >= 2147483648} {set init_value [expr {$init_value - 4294967296}]}')
        lines.append(f'    puts "TOF_INIT_{name.upper()} $init_value"')
        read_bytes += 4
    reset = symbols.get("tof_reset_diagnostic")
    if reset is not None:
        if reset["size"] != 28:
            raise ValueError("Unexpected ToF reset diagnostic ABI")
        reset_fields = ["stage", "result", "first_api_status", "assert_reads",
                        "boot_reads", "read_nacks", "release_attempts"]
        for index, name in enumerate(reset_fields):
            address = reset["address"] + index * 4
            lines.append(f'    set reset_value [read_u32 0x{address:08X}]')
            if name in ("result", "first_api_status"):
                lines.append('    if {$reset_value >= 2147483648} {set reset_value [expr {$reset_value - 4294967296}]}')
            lines.append(f'    puts "TOF_RESET_{name.upper()} $reset_value"')
            read_bytes += 4
    state = symbols["collision_state"]
    words = (state["size"] + 3) // 4
    lines.append(f'    puts "COLLISION_STATE_RAW [mrd {state["address_hex"]} {words}]"')
    read_bytes += 4 * words
    clock = symbols["home_clock_text"]
    lines.append(f'    puts "HOME_CLOCK [read_ascii {clock["address_hex"]} {clock["size"]}]"')
    read_bytes += 4 * ((clock["size"] + 3) // 4)
    for key, spec in TOF_FIELDS.items():
        address = sensor["address"] + spec["offset"]
        lines.append(f'    set {key} [expr {{[read_u32 0x{address:08X}] & 0x{spec["mask"]:X}}}]')
        lines.append(f'    puts "TOF_{key.upper()} ${key}"')
        read_bytes += 4
    lines.extend([
        '    puts "TOF_SIGNAL_MCPS [expr {$signal_16_16 / 65536.0}]"',
        '    puts "TOF_AMBIENT_MCPS [expr {$ambient_16_16 / 65536.0}]"',
    ])
    for key, address in HARDWARE.items():
        lines.append(f'    puts "{key.upper()} [format 0x%08X [read_u32 0x{address:08X}]]"')
        read_bytes += 4
    for key in ("small_font_pixels", "small_font_width", "small_font_offset"):
        sym = symbols[key]
        lines.append(f'    puts "{key.upper()}_METADATA {sym["address_hex"]} BYTES {sym["size"]}"')
    lines.extend([
        "    configparams force-mem-access 0",
        "    disconnect",
        "} err opts]} {",
        "    puts stderr $err",
        "    if {[dict exists $opts -errorinfo]} {puts stderr [dict get $opts -errorinfo]}",
        "    catch {configparams force-mem-access 0}",
        "    catch {disconnect}",
        "    exit 1",
        "}",
        "exit 0",
    ])
    return "\n".join(lines) + "\n", read_bytes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    base = Path(__file__).resolve().parent
    parser.add_argument("--elf", type=Path, default=base.parent / "firmware/release/vehicle_assist_lcd_english.elf")
    parser.add_argument("--output-dir", type=Path, default=base)
    args = parser.parse_args()
    elf = args.elf.resolve()
    data = elf.read_bytes()
    all_symbols = local_symbols(data)
    selected = {}
    for key, (owner, name) in REQUESTS.items():
        matches = [s for s in all_symbols if s["file"] == owner and s["name"] == name]
        if len(matches) != 1:
            raise ValueError(f"Expected one local symbol {owner}:{name}; found {len(matches)}")
        selected[key] = matches[0]
    for key, (owner, name) in OPTIONAL_REQUESTS.items():
        matches = [s for s in all_symbols if s["file"] == owner and s["name"] == name]
        if len(matches) > 1:
            raise ValueError(f"Ambiguous optional symbol {owner}:{name}")
        if matches:
            selected[key] = matches[0]
    digest = hashlib.sha256(data).hexdigest()
    tcl, read_bytes = render_tcl(selected, digest)
    metadata = {
        "elf": str(elf), "elf_sha256": digest, "elf_bytes": len(data),
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "hardware_contacted_by_generator": False,
        "runtime_read_bytes_max": read_bytes,
        "framebuffer_read": False,
        "symbols": selected,
        "tof_cached_fields": {key: dict(spec, address=selected["tof_sensor"]["address"] + spec["offset"],
            address_hex=f'0x{selected["tof_sensor"]["address"] + spec["offset"]:08X}')
            for key, spec in TOF_FIELDS.items()},
        "tof_field_abi_note": "Offsets in current VL53L0X_Dev_t; revalidate if ST API struct layout changes.",
        "hardware_registers": {key: f"0x{address:08X}" for key, address in HARDWARE.items()},
    }
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "runtime_diagnostics.tcl").write_text(tcl, encoding="ascii", newline="\n")
    (output / "diagnostic_symbols.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"ELF: {elf}\nSHA256: {digest}")
    for key, symbol in selected.items():
        print(f'{key}: {symbol["file"]}:{symbol["name"]} {symbol["address_hex"]} size={symbol["size"]}')
    print(f"Generated {output / 'runtime_diagnostics.tcl'}; bounded reads <= {read_bytes} bytes; no hardware access.")


if __name__ == "__main__":
    main()
