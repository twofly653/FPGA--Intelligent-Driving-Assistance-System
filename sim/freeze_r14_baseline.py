"""Create and verify a sealed R14 recovery archive; never contacts hardware."""
from datetime import datetime
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PARENT = ROOT.parent
PAIRED = Path(r'D:\project\deliveries\Task3_Submission_20261026\github\PYNQ-Z2-Vehicle-Vision\current_release')
DELIVERY = PAIRED.parent
BSP = PARENT / 'vehicle_system/vehicle_system.sdk/vehicle_platform/export/vehicle_platform/sw/vehicle_platform/standalone_domain'
DEST = PARENT / 'deliverables/frozen_baselines'
VERSION = 'CAMERA_SYNC_R14TRACK_20261030'
ELF_HASH = '2e5c82e4e3beca92e1f63dece80d7e869842a43e9c32abf9e05aa853089c86f2'
EXCLUDE = {'__pycache__', '.git', 'node_modules', '.Xil', 'photos', 'build', 'dist-cache'}
entries = {}

def digest(data):
    return hashlib.sha256(data).hexdigest()

def add_file(path, archive_name):
    path = Path(path)
    assert path.is_file(), path
    assert archive_name not in entries, archive_name
    entries[archive_name] = path

def add_tree(path, archive_prefix):
    path = Path(path)
    assert path.is_dir(), path
    for p in sorted(path.rglob('*')):
        rel = p.relative_to(path)
        if p.is_file() and not any(part in EXCLUDE for part in rel.parts):
            add_file(p, archive_prefix + '/' + rel.as_posix())

# Freeze the production source matching the exact already-programmed ELF.
offline_manifest = json.loads((ROOT / 'sim/tracking_r14_20261030/release_manifest.json').read_text())
for item in offline_manifest['files']:
    assert digest((ROOT / item['path']).read_bytes()) == item['sha256'], item['path']
elf = ROOT / 'src/firmware/release/vehicle_assist_r14_tracking_candidate.elf'
assert digest(elf.read_bytes()) == ELF_HASH
board = json.loads((ROOT / 'board/r14_tracking_20261030/board_manifest.json').read_text())
assert board['programmed'] and board['version'] == VERSION and board['elf_sha256'] == ELF_HASH
for item in board['hardware']:
    assert digest(Path(item['path']).read_bytes()) == item['sha256']

add_tree(ROOT / 'src/firmware/src', 'app/firmware/src')
add_tree(ROOT / 'dashboard', 'app/dashboard')
add_tree(ROOT / 'report', 'app/docs')
add_file(ROOT / 'README.md', 'app/README.md')
for name in ('vehicle_assist_r14_tracking_candidate.elf', 'vehicle_assist_r14_tracking_candidate.map'):
    add_file(ROOT / 'src/firmware/release' / name, 'app/firmware/release/' + name)
for name in ('build_software.ps1', 'generate_ui_fonts.py'):
    add_file(ROOT / 'sim' / name, 'app/verification/' + name)
for name in ('bspinclude', 'bsplib'):
    add_tree(BSP / name, 'app/bsp/standalone_domain/' + name)
add_file(BSP / 'system.mss', 'app/bsp/standalone_domain/system.mss')
for p in sorted(PAIRED.iterdir()):
    if p.is_file() and p.suffix.lower() not in {'.elf', '.map'}:
        add_file(p, 'hardware/' + p.name)

# Keep source-level hardware references, separate from the verified binary pair.
camera = DELIVERY / 'project/camera_optimized_20261025'
for folder in ('rtl', 'constr', 'licenses', 'vendor'):
    if (camera / folder).is_dir():
        add_tree(camera / folder, 'hardware_source/camera/' + folder)
for folder in ('rtl', 'constr', 'tcl', 'scripts'):
    if (PARENT / folder).is_dir():
        add_tree(PARENT / folder, 'hardware_source/base/' + folder)
add_file(DELIVERY / 'THIRD_PARTY_NOTICES.md', 'THIRD_PARTY_NOTICES.md')
for folder in ('r14_tracking_20261030', 'tof_priority_20261030'):
    run = ROOT / 'board' / folder
    for p in sorted(run.iterdir()):
        if p.is_file() and p.suffix.lower() in {'.json', '.md', '.log', '.tcl', '.py'}:
            add_file(p, 'evidence/board/' + folder + '/' + p.name)
tracking = ROOT / 'sim/tracking_r14_20261030'
for p in sorted(tracking.iterdir()):
    if p.is_file() and p.suffix.lower() in {'.json', '.md', '.log'}:
        add_file(p, 'evidence/offline/' + p.name)
add_file(tracking / 'scene_regression/regression.json', 'evidence/offline/scene_regression.json')
add_file(tracking / 'test_extent.c', 'app/verification/tracking_r14_20261030/test_extent.c')

generated = {
    'VERIFY.ps1': r'''$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'MANIFEST.json') -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($f in $manifest.files) {
    $p = Join-Path $PSScriptRoot $f.path
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing: $($f.path)" }
    if ((Get-Item -LiteralPath $p).Length -ne $f.bytes) { throw "Size mismatch: $($f.path)" }
    if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() -ne $f.sha256) { throw "Hash mismatch: $($f.path)" }
}
Write-Output "Verified R14 recovery archive: $($manifest.files.Count) files."
''',
    'RESTORE_R14.ps1': r'''param([string]$ToolRoot='D:\FPGA\2026.1')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'VERIFY.ps1')
$xsdb = Join-Path $ToolRoot 'Vitis\bin\xsdb.bat'
if (-not (Test-Path -LiteralPath $xsdb)) { throw "XSDB missing: $xsdb" }
Write-Output 'Restore R14 to PYNQ-Z2 RAM. Close the serial viewer first. Keep existing camera wiring.'
Write-Output 'Requires a running hardware server on 127.0.0.1:3122 and the powered target board.'
& $xsdb (Join-Path $PSScriptRoot 'restore_r14.tcl')
if ($LASTEXITCODE -ne 0) { throw 'R14 restore failed.' }
''',
    'restore_r14.tcl': r'''if {[catch {
set root [file dirname [file normalize [info script]]]
set release [file join $root hardware]
connect -url tcp:127.0.0.1:3122
targets -set -nocase -filter {name == "APU"}
rst -system
after 1000
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
catch {stop}
targets -set -nocase -filter {name =~ "*Cortex-A9*#1"}
catch {stop}
targets -set -nocase -filter {name == "APU"}
fpga -file [file join $release vehicle_top.bit]
targets -set -nocase -filter {name =~ "*Cortex-A9*#0"}
loadhw -hw [file join $release vehicle_top.xsa] -mem-ranges [list {0x40000000 0xbfffffff}] -regs
configparams force-mem-access 1
source [file join $release ps7_init.tcl]
ps7_init
ps7_post_config
if {[mrd -value 0x400000f4] != 0x4f423031} {error "Wrong OB01 hardware"}
dow [file join $root app firmware release vehicle_assist_r14_tracking_candidate.elf]
configparams force-mem-access 0
con -addr 0x00100000
puts "R14_TRACKING_RESTORE_COMPLETE"
disconnect
} err opts]} {
 puts stderr $err
 puts stderr [dict get $opts -errorinfo]
 catch {configparams force-mem-access 0}
 catch {disconnect}
 exit 1
}
exit 0
''',
    'START_WEB.ps1': r'''param([string]$Port='COM10',[int]$HttpPort=8767,[string]$Python='')
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'VERIFY.ps1')
if (-not $Python) {
    $bundled = Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if (Test-Path -LiteralPath $bundled) { $Python = $bundled }
    else { $Python = (Get-Command python -ErrorAction Stop).Source }
}
Write-Output "Camera data/control: http://127.0.0.1:$HttpPort/#camera . Video uses HDMI."
& $Python -E -B -u (Join-Path $PSScriptRoot 'app\dashboard\server.py') $Port --http-port $HttpPort
''',
    'README_恢复说明.md': '''# R14 当前在板版本：可恢复基线

此包冻结于 2026-10-30，版本 CAMERA_SYNC_R14TRACK_20261030。
这是用户要求保留的当前已烧录版本。包内固件是独立 R14 candidate 文件，避免误用默认路径中的 R13。
固件 SHA256：2e5c82e4e3beca92e1f63dece80d7e869842a43e9c32abf9e05aa853089c86f2。

## 保存的内容

- app/firmware/src：与已烧录 R14 对应的应用源码、传感器和图像/目标识别服务。
- app/firmware/release：原始 R14 ELF 和链接 map，未重新编译。
- app/dashboard：当前网页、后端、离线静态资源和测试代码；不包含个人照片、运行缓存和旧 build。
- app/bsp：原构建使用的 BSP 头文件、库和启动对象，软件构建无需依赖原工程外的 BSP。
- hardware：与本次下载一致的 BIT、XSA、PS7 初始化文件。
- hardware_source：相应交付工程的摄像头 RTL/约束/依赖及集成基础工程参考源码；不宣称已重新构建并复现相同 BIT。
- evidence：R14 本轮启动/网页采样/离线报告和测距恢复的历史证据。
- MANIFEST.json：每个文件的长度和 SHA256。

## 以后恢复当前效果

1. 解压此包到单独的 D 盘目录，保留原包不编辑。保持现有摄像头及扩展板接线。
2. 在解压目录运行 `powershell -File .\\VERIFY.ps1`，应显示全部文件通过校验。
3. 接通 PYNQ-Z2 电源和 USB；使用 Vivado/Vitis hardware server，端口为 3122。可启动 `D:\\FPGA\\2026.1\\Vivado\\bin\\hw_server.bat -s tcp:127.0.0.1:3122`。若已有同端口服务，不要重复启动。
4. 停止旧网页后端或断开其串口占用，再运行 `powershell -File .\\RESTORE_R14.ps1`。脚本会校验包、复位处理器、加载配套硬件和原始 R14 ELF，再自动启动。不会修改 QSPI/SD 卡或系统配置。
5. 运行 `powershell -File .\\START_WEB.ps1`；默认 COM10、网页 http://127.0.0.1:8767/#camera。若旧网页还在运行，请先停止旧服务，避免端口占用。
6. 摄像头视频继续由 HDMI OUT 输出。网页只同步数据和控制。确认启动标识 CAMERA_SYNC_R14TRACK_20261030、HDMI 实时图像及扩展屏。

这是 JTAG RAM 恢复；断电后不会自动保留。若扩展屏与多个传感器同时无应答，可保持信号线不动，完全断开独立 5V 和 USB，等待 5 秒后原样接回，再下载。此方法在旧版本排查中恢复过设备，不表示所有故障均由此解决。

## 恢复源码及重新编译

优先使用包内原始 ELF 恢复当前版本，不需要重编译。需要继续开发时使用 app 目录；构建命令：

```powershell
powershell -File .\\app\\verification\\build_software.ps1 -OutputName vehicle_assist_r14_tracking_candidate.elf -EnableObstacles -EnableWebSync -EnableManualDetection -EnablePhotoSettle -PhotoSettleMs 4000
```

工具链默认 D:\\FPGA\\2026.1，可用 -ToolRoot 指定。源码修改/重编译后应另存新版本；不要覆盖此冻结包，也不要将新 ELF 当作该 SHA256 的原始 R14。

## 验证边界

本次 R14 启动标识、640×480 摄像头实时输入、网页数据与识别开启已确认。LCD 首页和 ToF 初始化成功；本轮实体显示、纸板测距和跟踪效果仍需现场确认。跟踪为固定摄像头运动区域检测、外观匹配和位置预测，最多 8 目标；尚未实现参考截图中完整的车辆类别识别、长期编号保持或测速。历史四种模式帧率和完整功能验收以各自版本记录为准。

打包过程不连接/下载板卡，不停止现有网页，不改变当前运行版本。运行时轨迹、计时和内存中的临时校准状态不作为恢复项保存。
'''
}

DEST.mkdir(parents=True, exist_ok=True)
stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
output = DEST / ('DriveSense_R14_' + stamp + '_RESTORE.zip')
records = []
with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for name, path in sorted(entries.items()):
        data = path.read_bytes()
        archive.writestr(name, data)
        records.append({'path': name, 'bytes': len(data), 'sha256': digest(data), 'source': str(path)})
    for name, value in sorted(generated.items()):
        data = value.encode('utf-8-sig' if name.endswith('.ps1') else 'utf-8')
        archive.writestr(name, data)
        records.append({'path': name, 'bytes': len(data), 'sha256': digest(data), 'source': 'generated recovery helper; not executed'})
    manifest = {'version': VERSION, 'created_local': datetime.now().astimezone().isoformat(),
                'purpose': 'Immutable current R14 baseline before persistent-ID tracking changes',
                'programmed_elf_sha256': ELF_HASH, 'file_count': len(records), 'files': records,
                'hardware_contact_during_backup': False, 'restore_scripts_executed': False,
                'offline_release_manifest_kept_historical': True,
                'runtime_tracks_timers_calibration_ram_not_saved': True}
    archive.writestr('MANIFEST.json', json.dumps(manifest, ensure_ascii=False, indent=2).encode('utf-8'))

# Full decompression, CRC and per-entry SHA256, then prove inputs did not change.
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    assert len(archive.namelist()) == len(records) + 1
    for item in records:
        data = archive.read(item['path'])
        assert len(data) == item['bytes'] and digest(data) == item['sha256'], item['path']
        if item['path'] in entries:
            assert digest(entries[item['path']].read_bytes()) == item['sha256'], item['source']
zip_hash = digest(output.read_bytes())
output.with_suffix('.zip.sha256').write_text(zip_hash + '  ' + output.name + '\n', encoding='ascii')
summary = {'zip': str(output), 'bytes': output.stat().st_size, 'sha256': zip_hash,
           'file_count': len(records) + 1, 'all_crc_and_sha256_verified': True,
           'original_source_unchanged': True, 'hardware_untouched': True,
           'created_local': datetime.now().astimezone().isoformat()}
output.with_suffix('.verified.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False))
