#Requires -Version 5.1
param(
	[int]$Port = 7777,
	[switch]$NoFake,
	[string]$LogFile = "",
	[string]$Name = "",
	[string]$Config = "",
	[int]$MaxPlayers = 0
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "dist\server\CommonwealthMP.Server.exe"
if (-not (Test-Path -LiteralPath $exe)) {
	$exe = Join-Path $root "build\server\server\Release\CommonwealthMP.Server.exe"
}
if (-not (Test-Path -LiteralPath $exe)) {
	throw "CommonwealthMP.Server.exe not found. Run scripts\build-server.ps1 first."
}

$args = @("--port", "$Port")
if ($NoFake) { $args += "--no-fake" }
if ($LogFile) { $args += @("--log-file", $LogFile) }
if ($Name) { $args += @("--name", $Name) }
if ($Config) { $args += @("--config", $Config) }
if ($MaxPlayers -gt 0) { $args += @("--max-players", "$MaxPlayers") }

Write-Host "Starting $exe"
Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory (Split-Path $exe) -WindowStyle Normal
