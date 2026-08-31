#Requires -Version 5.1
<#
.SYNOPSIS
  Configure and rebuild CommonwealthMP.dll (releasedbg).

.DESCRIPTION
  Always rebuilds the CommonwealthMP target so a copy into MO2 cannot keep a
  stale DLL after source edits. CommonLibF4 objects are reused.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$xmake = (Get-Command xmake -ErrorAction SilentlyContinue).Source
if (-not $xmake) {
	$portable = Join-Path $env:LOCALAPPDATA "xmake-portable\xmake\xmake.exe"
	if (Test-Path -LiteralPath $portable) {
		$xmake = $portable
	} else {
		throw "xmake.exe not found on PATH or at $portable"
	}
}

function Find-PluginDll([string]$RepoRoot) {
	$candidates = @(
		(Join-Path $RepoRoot "plugin\build\windows\x64\releasedbg\CommonwealthMP.dll"),
		(Join-Path $RepoRoot "plugin\build\windows\x64\release\CommonwealthMP.dll")
	) | Where-Object { Test-Path -LiteralPath $_ }
	if (-not $candidates -or $candidates.Count -eq 0) { return $null }
	return @(
		$candidates | ForEach-Object { Get-Item -LiteralPath $_ } |
			Sort-Object LastWriteTime -Descending |
			Select-Object -First 1 -ExpandProperty FullName
	)[0]
}

function Get-NewerPluginSources([string]$RepoRoot, [string]$DllPath) {
	if (-not $DllPath -or -not (Test-Path -LiteralPath $DllPath)) { return @("missing DLL") }
	$dllTime = (Get-Item -LiteralPath $DllPath).LastWriteTimeUtc
	$srcRoot = Join-Path $RepoRoot "plugin\src"
	if (-not (Test-Path -LiteralPath $srcRoot)) { return @() }
	return @(
		Get-ChildItem -LiteralPath $srcRoot -Recurse -File |
			Where-Object { $_.Extension -match '\.(cpp|h|hpp)$' -and $_.LastWriteTimeUtc -gt $dllTime } |
			Sort-Object LastWriteTimeUtc -Descending |
			Select-Object -First 5 -ExpandProperty Name
	)
}

Push-Location (Join-Path $root "plugin")
try {
	& $xmake f -m releasedbg -a x64 -y
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

	Write-Host "xmake: rebuilding CommonwealthMP"
	& $xmake build -r CommonwealthMP
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

	$dllPath = Find-PluginDll $root
	if (-not $dllPath) {
		throw "xmake finished but CommonwealthMP.dll was not found."
	}
	$stale = Get-NewerPluginSources $root $dllPath
	if ($stale.Count -gt 0) {
		throw ("CommonwealthMP.dll is still older than {0}. Close anything locking the build output and retry." -f ($stale -join ", "))
	}
	$dllItem = Get-Item -LiteralPath $dllPath
	Write-Host ("Built {0}  {1:N0} bytes  {2:yyyy-MM-dd HH:mm:ss}" -f $dllItem.FullName, $dllItem.Length, $dllItem.LastWriteTime)
} finally {
	Pop-Location
}
exit 0
