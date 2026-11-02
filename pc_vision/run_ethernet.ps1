param([string]$Board='192.168.2.99',[string]$Bind='192.168.2.1',[switch]$Headless,[ValidateSet('precision','baseline')][string]$Tracker='precision',[switch]$PreviewDetail)
$ErrorActionPreference='Stop'
$python=Join-Path $PSScriptRoot '.venv\Scripts\python.exe'
if(!(Test-Path -LiteralPath $python)){throw 'Project Python is missing. See README_ETHERNET.md.'}
$trackingArgs=@('-E','-B',(Join-Path $PSScriptRoot 'ethernet_tracking.py'),'--board',$Board,'--bind',$Bind,'--tracker',$Tracker)
if($PreviewDetail){$trackingArgs+='--preview-detail'}
if(!$Headless){$trackingArgs+='--show'}
# Explicit future live entry. No flashing, NIC configuration or firewall changes.
& $python @trackingArgs
exit $LASTEXITCODE
