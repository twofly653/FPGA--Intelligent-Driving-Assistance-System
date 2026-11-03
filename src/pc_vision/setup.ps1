param([string]$Python='C:\Users\twofl\AppData\Local\Programs\Python\Python311\python.exe')
$ErrorActionPreference='Stop'
$venv=Join-Path $PSScriptRoot '.venv'
if(-not (Test-Path -LiteralPath (Join-Path $venv 'Scripts\python.exe'))) {
    & $Python -m venv $venv
    if($LASTEXITCODE -ne 0){throw 'Failed to create project-local Python environment'}
}
$py=Join-Path $venv 'Scripts\python.exe'
$cache=Join-Path $PSScriptRoot '.cache\pip'
& $py -m pip install --disable-pip-version-check --cache-dir $cache --upgrade pip
if($LASTEXITCODE -ne 0){throw 'Project-local pip update failed'}
& $py -E -B -u (Join-Path $PSScriptRoot 'download_cuda_wheels.py')
if($LASTEXITCODE -ne 0){throw 'Official CUDA wheel verification failed'}
$torchWheel=Join-Path $PSScriptRoot '.cache\wheels\torch-2.9.1+cu128-cp311-cp311-win_amd64.whl'
$visionWheel=Join-Path $PSScriptRoot '.cache\wheels\torchvision-0.24.1+cu128-cp311-cp311-win_amd64.whl'
& $py -m pip install --disable-pip-version-check --cache-dir $cache $torchWheel $visionWheel
if($LASTEXITCODE -ne 0){throw 'CUDA PyTorch installation failed'}
& $py -m pip install --disable-pip-version-check --cache-dir $cache -r (Join-Path $PSScriptRoot 'requirements.txt')
if($LASTEXITCODE -ne 0){throw 'Tracking dependencies installation failed'}
& $py -m pip freeze | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'environment.lock.txt') -Encoding ascii
Write-Output 'Project-local GPU tracking environment ready; global Python and board firmware unchanged.'
