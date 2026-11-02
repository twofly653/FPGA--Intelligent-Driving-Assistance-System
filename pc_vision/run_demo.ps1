param(
 [string]$Source=(Join-Path $PSScriptRoot '..\verification\bilibili_tracking_20261030\BV11x411R7o6_p1_car.mp4'),
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
