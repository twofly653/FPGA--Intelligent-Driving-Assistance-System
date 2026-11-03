$ErrorActionPreference='Stop'
$previewRoot=$PSScriptRoot
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $previewRoot '../../src/firmware/src'))
$compiler='D:/FPGA/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe'
$sourceNames=@('lcd_st7789.c','ui_draw.c','ui_format.c','touch_hit.c','ui_menu.c','ui_home.c','ui_clock.c','ui_compass.c','ui_attitude.c','ui_distance.c','ui_obstacle.c','collision_model.c')
$sourcePaths=$sourceNames | ForEach-Object {Join-Path $sourceRoot $_}
& $compiler -std=c99 -O2 -Wall -Wextra -DUI_HOST_PREVIEW -DAPP_ENABLE_MANUAL_DETECTION=1 -DAPP_ENABLE_TOF=1 "-I$previewRoot/include" "-I$sourceRoot" "-I$previewRoot" @sourcePaths "$previewRoot/preview_stubs.c" "$previewRoot/preview_main.c" -lm -o "$previewRoot/preview.exe"
if($LASTEXITCODE -ne 0){throw 'Host preview compile failed'}
Push-Location $previewRoot
try { & ./preview.exe; if($LASTEXITCODE -ne 0){throw 'Host framebuffer text overflows detected'} } finally {Pop-Location}
