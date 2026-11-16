# One-time setup (run as Administrator) so the board and the phone can reach
# the cloud service on this PC over Wi-Fi:
#   1. Gives the Mosquitto Windows service the project config, which listens
#      on all network interfaces (port 1883) instead of localhost only.
#   2. Opens inbound TCP 1883 (MQTT) and 5000 (web API) in Windows Firewall.
#
# Usage: right-click PowerShell > Run as administrator, then
#     powershell -ExecutionPolicy Bypass -File "<repo>\scripts\setup_network_admin.ps1"
#
# To undo after the lab:
#     Remove-NetFirewallRule -DisplayName "Lab3 MQTT 1883","Lab3 web API 5000"
#     and restore C:\Program Files\mosquitto\mosquitto.conf.bak
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$conf = "C:\Program Files\mosquitto\mosquitto.conf"

if (-not (Test-Path "$conf.bak")) { Copy-Item $conf "$conf.bak" }
Copy-Item (Join-Path $root "cloud\mosquitto.conf") $conf -Force
Restart-Service mosquitto
Write-Host "Mosquitto restarted with the project config."

foreach ($rule in @(@{Name = "Lab3 MQTT 1883"; Port = 1883}, @{Name = "Lab3 web API 5000"; Port = 5000})) {
    if (-not (Get-NetFirewallRule -DisplayName $rule.Name -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $rule.Name -Direction Inbound -Protocol TCP `
            -LocalPort $rule.Port -Action Allow -Profile Private, Public | Out-Null
    }
    Write-Host "Firewall rule ready: $($rule.Name)"
}
Get-NetTCPConnection -LocalPort 1883 -State Listen | Select-Object LocalAddress, LocalPort
