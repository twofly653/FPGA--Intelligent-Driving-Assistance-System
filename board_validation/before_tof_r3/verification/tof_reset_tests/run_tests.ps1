$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $testRoot '../../firmware/src'))
$compiler = 'D:/FPGA/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe'
$exe = Join-Path $testRoot 'test_tof_reset.exe'
& $compiler -std=c11 -O2 -Wall -Wextra -Werror "-I$testRoot/include" "-I$sourceRoot" "$sourceRoot/vl53l0x_reset.c" "$sourceRoot/vl53l0x.c" "$testRoot/test_tof_reset.c" -o $exe
if ($LASTEXITCODE -ne 0) { throw 'ToF reset test compile failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'ToF reset tests failed' }
