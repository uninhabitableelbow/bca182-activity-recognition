# Starts the Lab 3 cloud service on this PC: classifier service and web API,
# each in its own window. The Mosquitto broker runs as a Windows service.
#
# Usage (normal PowerShell, no admin needed):
#     powershell -ExecutionPolicy Bypass -File scripts\start_cloud.ps1
$ErrorActionPreference = "Stop"
$root  = Split-Path -Parent $PSScriptRoot
$cloud = Join-Path $root "cloud"

$svc = Get-Service mosquitto -ErrorAction SilentlyContinue
if (-not $svc) { throw "Mosquitto is not installed (see README)." }
if ($svc.Status -ne "Running") { throw "The mosquitto service is not running. Start it in the Services app." }

Start-Process powershell -WorkingDirectory $cloud -ArgumentList "-NoExit", "-Command", "`$host.UI.RawUI.WindowTitle='Lab3 classifier'; python classifier_service.py"
Start-Process powershell -WorkingDirectory $cloud -ArgumentList "-NoExit", "-Command", "`$host.UI.RawUI.WindowTitle='Lab3 web API'; python api.py"

$ips = Get-NetIPAddress -AddressFamily IPv4 |
       Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
       Select-Object -ExpandProperty IPAddress
Write-Host "Classifier and web API started."
Write-Host "Dashboard on this PC:  http://localhost:5000/"
foreach ($ip in $ips) {
    Write-Host "From the board/phone: MQTT broker $ip`:1883   web API http://$ip`:5000/"
}
