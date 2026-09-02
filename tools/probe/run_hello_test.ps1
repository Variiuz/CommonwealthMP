param(
	[Parameter(Mandatory = $true)][string]$Server,
	[Parameter(Mandatory = $true)][string]$Probe,
	[int]$Port = 17777
)

$ErrorActionPreference = "Stop"
$session = Join-Path $env:TEMP ("cmp-hello-" + $Port)
if (Test-Path -LiteralPath $session) {
	Remove-Item -LiteralPath $session -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $session | Out-Null
$cfg = Join-Path $session "server.ini"
$log = Join-Path $session "server.log"
$proc = Start-Process -FilePath $Server -ArgumentList @("--config", $cfg, "--port", "$Port", "--log-file", $log) -PassThru -WindowStyle Hidden
try {
	Start-Sleep -Milliseconds 400
	& $Probe "127.0.0.1" "$Port"
	$code = $LASTEXITCODE
	if ($code -ne 0) {
		exit $code
	}
} finally {
	if ($proc -and -not $proc.HasExited) {
		Stop-Process -Id $proc.Id -Force
	}
}
exit 0
