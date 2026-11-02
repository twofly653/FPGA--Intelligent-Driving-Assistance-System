$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build_preview.ps1') 2>&1 | Tee-Object -FilePath (Join-Path $PSScriptRoot 'render.log')
$previewPython='C:/Users/twofl/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& $previewPython (Join-Path $PSScriptRoot 'convert_preview.py')
if($LASTEXITCODE -ne 0){throw 'PNG conversion failed'}
