param(
	[Parameter(Mandatory = $true)][string]$Server,
	[Parameter(Mandatory = $true)][string]$Probe,
	[int]$Port = 17777
)

$ErrorActionPreference = "Stop"
$proc = Start-Process -FilePath $Server -ArgumentList @("--port", "$Port") -PassThru -WindowStyle Hidden
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
