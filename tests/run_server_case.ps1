param(
	[Parameter(Mandatory = $true)][string]$Server,
	[Parameter(Mandatory = $true)][string]$Cases,
	[Parameter(Mandatory = $true)][string]$Case,
	[int]$Port = 17778,
	[string]$ExtraArgs = ""
)

$ErrorActionPreference = "Stop"
$session = Join-Path $env:TEMP ("cmp-ctest-" + $Port)
if (Test-Path -LiteralPath $session) {
	Remove-Item -LiteralPath $session -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $session | Out-Null
$log = Join-Path $session "server.log"
$cfg = Join-Path $session "server.ini"

$serverArgs = @(
	"--config", $cfg,
	"--port", "$Port",
	"--reset-session",
	"--session-dir", $session,
	"--log-file", $log
)
if ($ExtraArgs) {
	$serverArgs += ($ExtraArgs -split '\s+')
}

$env:CMP_CONHOST = "1"
$proc = Start-Process -FilePath $Server -ArgumentList $serverArgs -PassThru -WindowStyle Hidden
try {
	Start-Sleep -Milliseconds 450
	if ($proc.HasExited) {
		Write-Error "server exited before the case ran. log: $log"
		exit 3
	}
	& $Cases --case $Case --host "127.0.0.1" --port "$Port" --session-dir $session
	$code = $LASTEXITCODE
	if ($code -ne 0) {
		if (Test-Path -LiteralPath $log) {
			Write-Host "--- server log ---"
			Get-Content -LiteralPath $log -Tail 40
		}
		exit $code
	}
} finally {
	if ($proc -and -not $proc.HasExited) {
		Stop-Process -Id $proc.Id -Force
	}
}
exit 0
