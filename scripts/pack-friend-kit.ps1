#Requires -Version 5.1
param(
	[string]$OutZip = "",
	[string]$Dll = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $OutZip) {
	$OutZip = Join-Path $root "dist\CommonwealthMP-friend.zip"
}
if (-not $Dll) {
	$candidates = @(
		(Join-Path $root "plugin\build\windows\x64\releasedbg\CommonwealthMP.dll"),
		(Join-Path $root "plugin\build\windows\x64\release\CommonwealthMP.dll")
	)
	$Dll = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

New-Item -ItemType Directory -Force -Path (Split-Path $OutZip) | Out-Null
$stage = Join-Path $env:TEMP ("cmp-friend-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $stage | Out-Null
try {
	if ($Dll -and (Test-Path $Dll)) {
		Copy-Item $Dll (Join-Path $stage "CommonwealthMP.dll")
	} else {
		Write-Host "WARNING: CommonwealthMP.dll not built yet. Zip will be docs-only."
	}
	Copy-Item (Join-Path $root "data\F4SE\Plugins\CommonwealthMP.ini") $stage
	Copy-Item (Join-Path $root "docs\FRIEND.md") (Join-Path $stage "README.txt")
	$esp = Join-Path $root "data\CommonwealthMP.esp"
	if (Test-Path $esp) {
		Copy-Item $esp $stage
	}
	if (Test-Path $OutZip) { Remove-Item $OutZip -Force }
	Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $OutZip
	Write-Host "Friend kit: $OutZip"
} finally {
	Remove-Item $stage -Recurse -Force
}
