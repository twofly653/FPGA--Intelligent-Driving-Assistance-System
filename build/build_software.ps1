param(
    [string]$ToolRoot = 'D:\FPGA\2026.1',
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.-]*\.elf$')][string]$OutputName = '',
    [ValidateSet(12,14,44,76,108)][int]$CameraControl = 14,
    [switch]$DisableTof,
    [switch]$EnableTofSoftReset,
    [switch]$DisableTelemetry,
    [switch]$TrafficDebugCapture,
    [switch]$EnableObstacles,
    [switch]$EnableWebSync,
    [switch]$EnableManualDetection,
    [switch]$EnablePhotoSettle,
    [ValidateSet(1200,4000)][int]$PhotoSettleMs = 1200,
    [switch]$ObstacleDebugCapture,
    [switch]$KeepBuildFiles,
    [switch]$EnablePcNetwork
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path (Split-Path -Parent $root) 'vehicle_system'
$source = Join-Path $root 'src\firmware\src'
$bsp = Join-Path $project 'vehicle_system.sdk\vehicle_platform\export\vehicle_platform\sw\vehicle_platform\standalone_domain'
$localBsp = Join-Path $root 'bsp\standalone_domain'
if (Test-Path -LiteralPath (Join-Path $localBsp 'bsplib\lib\libxil.a')) { $bsp = $localBsp }
$include = Join-Path $bsp 'bspinclude\include'
$library = Join-Path $bsp 'bsplib\lib'
$compiler = Join-Path $ToolRoot 'gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe'
foreach ($item in @($compiler, (Join-Path $library 'libxil.a'), (Join-Path $library 'xil-crt0.o'), (Join-Path $source 'lscript.ld'))) {
    if (-not (Test-Path -LiteralPath $item -PathType Leaf)) { throw "Required file missing: $item" }
}

$buildRoot = Join-Path $root 'src\firmware\build'
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$work = Join-Path $buildRoot ('software_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$tofFlag = if ($DisableTof) { '-DAPP_ENABLE_TOF=0' } else { '-DAPP_ENABLE_TOF=1' }
$telemetryFlag = if ($DisableTelemetry) { '-DAPP_ENABLE_TELEMETRY=0' } else { '-DAPP_ENABLE_TELEMETRY=1' }
$flags = @('-mcpu=cortex-a9', '-mfpu=vfpv3', '-mfloat-abi=hard', '-O1', '-g', '-std=gnu11',
    '-Wall', '-Wextra', '-Werror', '-ffunction-sections', '-fdata-sections', $tofFlag, $telemetryFlag,
    "-DAPP_CAMERA_STARTUP_CONTROL=$CameraControl",
    "-DAPP_CAMERA_BT656=0",
    "-I$source", "-I$include")
if($EnableTofSoftReset){
    if($DisableTof){throw 'EnableTofSoftReset requires ToF'}
    $flags += '-DAPP_TOF_SOFT_RESET=1'
}
if($TrafficDebugCapture){$flags += "-DTRAFFIC_CAPTURE_DEBUG=1"}
if($ObstacleDebugCapture){if(-not $EnableObstacles){throw 'ObstacleDebugCapture requires EnableObstacles'};$flags += '-DOBSTACLE_CAPTURE_DEBUG=1'}
if($EnablePhotoSettle){if(-not $EnableManualDetection){throw 'PhotoSettle requires EnableManualDetection'};$flags += '-DAPP_ENABLE_PHOTO_SETTLE=1';$flags += ('-DAPP_PHOTO_SETTLE_MIN_US=' + ($PhotoSettleMs * 1000) + 'u')}
if($EnableManualDetection){if(-not $EnableWebSync){throw 'ManualDetection requires EnableWebSync'};$flags += '-DAPP_ENABLE_MANUAL_DETECTION=1'}
if($EnableWebSync){if(-not $EnableObstacles){throw 'WebSync requires EnableObstacles'};$flags += '-DAPP_ENABLE_WEB_SYNC=1'}
$flags += if($EnableObstacles){'-DAPP_ENABLE_OBSTACLES=1'}else{'-DAPP_ENABLE_OBSTACLES=0'}
$objects = @()
if($EnablePcNetwork){
    if(-not ($EnableObstacles -and $EnableWebSync -and $EnableManualDetection)){throw 'PC network requires obstacles, web sync and manual detection'}
    if(-not $OutputName){throw 'PC network must use a separate explicitly named candidate ELF'}
    $network=Join-Path $root 'src\firmware\network'
    $lwip=Join-Path $ToolRoot 'Vitis\data\embeddedsw\ThirdParty\sw_services\lwip220_v1_4\src\lwip-2.2.0'
    if(-not (Test-Path -LiteralPath (Join-Path $lwip 'src\core\udp.c'))){throw 'AMD lwIP 2.2 source not found'}
    $flags+=@('-DAPP_ENABLE_PC_NETWORK=1',"-I$network",("-I"+(Join-Path $lwip 'src\include')))
    $netSources=@(Get-ChildItem -LiteralPath (Join-Path $lwip 'src\core') -Filter '*.c')
    $netSources+=@(Get-ChildItem -LiteralPath (Join-Path $lwip 'src\core\ipv4') -Filter '*.c')
    $netSources+=Get-Item -LiteralPath (Join-Path $lwip 'src\netif\ethernet.c')
    foreach($file in $netSources){
        $object=Join-Path $work ('lwip_'+$file.BaseName+'.o')
        & $compiler @flags '-O2' '-w' -c $file.FullName -o $object
        if($LASTEXITCODE -ne 0){throw "lwIP compile failed: $($file.Name)"}
        $objects+=$object
    }
    $object=Join-Path $work 'gem_poll.o'
    & $compiler @flags '-O2' -c (Join-Path $network 'gem_poll.c') -o $object
    if($LASTEXITCODE -ne 0){throw 'GEM polling driver compile failed'}
    $objects+=$object
}
foreach ($file in Get-ChildItem -LiteralPath $source -Filter '*.c') {
    if ($DisableTof -and ($file.Name -in @('collision_model.c', 'collision_service.c', 'ui_distance.c', 'vl53l0x.c', 'vl53l0x_platform.c', 'vl53l0x_probe.c') -or $file.Name -like 'vl53l0x_api*.c')) { continue }
    $object = Join-Path $work ($file.BaseName + '.o')
    $compileFlags = @($flags)
    if ($file.Name -in @('traffic_vision_core.c','obstacle_motion_core.c')) { $compileFlags += '-O2' }
    if ($file.Name -like 'vl53l0x_api*.c') { $compileFlags += '-Wno-unused-parameter' }
    & $compiler @compileFlags -c $file.FullName -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($file.Name); files retained in $work" }
    $objects += $object
}

# The archived 2022 BSP startup expects the empty newlib constructor hooks.
$shim = Join-Path $work 'startup_shim.c'
Set-Content -LiteralPath $shim -Encoding ascii -Value @('void _init(void) {}', 'void _fini(void) {}')
$shimObject = Join-Path $work 'startup_shim.o'
& $compiler @flags -c $shim -o $shimObject
if ($LASTEXITCODE -ne 0) { throw "Startup compile failed; files retained in $work" }
$objects += $shimObject
$outputDirectory = Join-Path $root 'src\firmware\release'
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
$name = if($EnablePhotoSettle){if($PhotoSettleMs -eq 4000){'vehicle_assist_lcd_english.elf'}else{'vehicle_assist_photo.elf'}}elseif($EnableManualDetection){'vehicle_assist_manual.elf'}elseif($EnableWebSync){'vehicle_assist_sync.elf'}elseif($EnableObstacles){if($ObstacleDebugCapture){'vehicle_assist_obstacle_debug.elf'}elseif($DisableTof){'vehicle_assist_obstacle_no_tof.elf'}else{'vehicle_assist_obstacle.elf'}} elseif($TrafficDebugCapture) { 'vehicle_assist_traffic_debug.elf' } elseif ($DisableTof) { 'vehicle_assist_no_tof.elf' } else { 'vehicle_assist.elf' }
if($OutputName){$name=$OutputName}
# Retained-frame diagnostics must never overwrite the integrated release ELF.
if($ObstacleDebugCapture -and ($EnableWebSync -or $EnableManualDetection -or $EnablePhotoSettle)){$name = $name.Replace('.elf', '_obstacle_debug.elf')}
if($EnableTofSoftReset){$name = $name.Replace('.elf', '_tof_soft_reset_experimental.elf')}
$elf = Join-Path $outputDirectory $name
$map = [System.IO.Path]::ChangeExtension($elf, '.map')
$candidate = Join-Path $work $name
$candidateMap = [System.IO.Path]::ChangeExtension($candidate, '.map')
& $compiler @flags '-nostartfiles' '-Wl,-build-id=none' '-Wl,--gc-sections' "-Wl,-T,$source\lscript.ld" "-Wl,-Map,$candidateMap" "-L$library" -o $candidate (Join-Path $library 'xil-crt0.o') @objects '-Wl,--start-group' '-lxil' '-lc' '-lm' '-lgcc' '-Wl,--end-group'
if ($LASTEXITCODE -ne 0) { throw "Link failed; files retained in $work" }
$bytes = [IO.File]::ReadAllBytes($candidate)
if ($bytes.Length -lt 52 -or $bytes[0] -ne 0x7f -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x4c -or $bytes[3] -ne 0x46 -or
    $bytes[4] -ne 1 -or $bytes[5] -ne 1 -or [BitConverter]::ToUInt16($bytes, 18) -ne 40) {
    throw 'Output is not a 32-bit little-endian ARM ELF; refusing publication.'
}
# Application segments include BSS (the 2MiB photo buffer), heap and every
# exception stack. None may overlap the PL-owned 1MiB preview frame slots.
$programHeaders = [BitConverter]::ToUInt32($bytes, 28)
$programEntrySize = [BitConverter]::ToUInt16($bytes, 42)
$programCount = [BitConverter]::ToUInt16($bytes, 44)
$appEnd = [uint64]0
for ($headerIndex = 0; $headerIndex -lt $programCount; ++$headerIndex) {
    $position = $programHeaders + $headerIndex * $programEntrySize
    if ($programEntrySize -lt 32 -or $position + 32 -gt $bytes.Length) { throw 'Invalid ELF program table.' }
    if ([BitConverter]::ToUInt32($bytes, $position) -ne 1) { continue }
    $physical = [uint64][BitConverter]::ToUInt32($bytes, $position + 12)
    $memorySize = [uint64][BitConverter]::ToUInt32($bytes, $position + 20)
    if ($memorySize -eq 0) { continue }
    $segmentEnd = $physical + $memorySize
    if ($physical -lt 0x00100000 -or $segmentEnd -gt 0x10000000) {
        throw ('ELF load segment overlaps reserved PL memory: 0x{0:X8}..0x{1:X8}' -f $physical, $segmentEnd)
    }
    if ($segmentEnd -gt $appEnd) { $appEnd = $segmentEnd }
}
Copy-Item -LiteralPath $candidate -Destination $elf -Force
Copy-Item -LiteralPath $candidateMap -Destination $map -Force
Write-Output ('ARM ELF verified: {0} bytes; entry=0x{1:X8}' -f $bytes.Length, [BitConverter]::ToUInt32($bytes, 24))
Write-Output "Built: $elf"
Write-Output "ToF enabled: $(-not $DisableTof); telemetry enabled: $(-not $DisableTelemetry)"
Write-Output "Camera startup control: $CameraControl"
Write-Output "Experimental ToF soft reset: $($EnableTofSoftReset.IsPresent)"
Write-Output ('Application DDR load end: 0x{0:X8}; PL preview begins: 0x10000000; DDR_READY bit15 enabled at startup' -f $appEnd)

if ($KeepBuildFiles) { Write-Output "Build files kept: $work" }
else {
    $resolvedWork = (Resolve-Path -LiteralPath $work).ProviderPath
    $resolvedBuildRoot = (Resolve-Path -LiteralPath $buildRoot).ProviderPath
    if ((Split-Path -Parent $resolvedWork) -ne $resolvedBuildRoot -or (Split-Path -Leaf $resolvedWork) -notmatch '^software_[0-9a-f]{32}$') {
        throw "Refusing unexpected cleanup path: $resolvedWork"
    }
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force
}



