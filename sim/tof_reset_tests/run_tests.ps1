$ErrorActionPreference = 'Stop'
$testRoot = $PSScriptRoot
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $testRoot '../../src/firmware/src'))
$compiler = 'D:/FPGA/2026.1/tps/mingw/10.0.0/win64.o/nt/bin/gcc.exe'
foreach ($mode in @(0, 1)) {
    $exe = Join-Path $testRoot ("test_tof_reset_$mode.exe")
    & $compiler -std=c11 -O2 -Wall -Wextra -Werror "-DAPP_TOF_SOFT_RESET=$mode" "-I$testRoot/include" "-I$sourceRoot" "$sourceRoot/vl53l0x_reset.c" "$sourceRoot/vl53l0x.c" "$testRoot/test_tof_reset.c" -o $exe
    if ($LASTEXITCODE -ne 0) { throw "ToF test compile failed (reset=$mode)" }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw "ToF tests failed (reset=$mode)" }
}
