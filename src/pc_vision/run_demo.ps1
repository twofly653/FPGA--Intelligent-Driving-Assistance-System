param(
 [Parameter(Mandatory=$true)][string]$Source,
 [int]$ImageSize=960,
 [string]$Output=''
)
$ErrorActionPreference='Stop'
$py=Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if(-not (Test-Path -LiteralPath $py)){throw 'Run setup.ps1 first'}
$options=@((Join-Path $PSScriptRoot 'track_video.py'),'--source',$Source,'--imgsz',"$ImageSize")
if($Output){$options+=@('--output',$Output)}
& $py -E -B -u @options
if($LASTEXITCODE -ne 0){throw 'Vehicle tracking validation failed'}
