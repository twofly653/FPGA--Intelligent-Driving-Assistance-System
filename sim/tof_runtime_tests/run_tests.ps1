$ErrorActionPreference='Stop'
$project=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$src=Join-Path $project 'src\firmware\src'
$stub=Join-Path (Split-Path $project -Parent) 'vehicle_system\tests\host_stubs'
$gcc='D:\FPGA\2026.1\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe'
$build=Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$exe=Join-Path $build 'test_tof_runtime.exe'
$buildArgs=@('-std=c11','-O2','-Wall','-Wextra','-Werror',"-I$src","-I$stub",'-include',(Join-Path $stub 'pl_hub_mock.h'),(Join-Path $src 'collision_service.c'),(Join-Path $src 'collision_model.c'),(Join-Path $PSScriptRoot 'test_tof_runtime.c'),'-o',$exe)
& $gcc @buildArgs
if($LASTEXITCODE -ne 0){throw 'ToF runtime compile failed'}
& $exe
if($LASTEXITCODE -ne 0){throw 'ToF runtime failed'}
