#Requires -Version 5.1
<#
.SYNOPSIS
  Configure and build CommonwealthMP.dll (releasedbg), incrementally by default.

.DESCRIPTION
  Skips the compile when the DLL is already newer than plugin inputs.
  Otherwise runs xmake without -r so only dirty translation units rebuild.
  Pass -ForceRebuild for a full CommonwealthMP target rebuild.
#>
[CmdletBinding()]
param(
	[switch]$ForceRebuild
)

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

function Get-PluginInputFiles([string]$RepoRoot) {
	$files = @()
	$srcRoot = Join-Path $RepoRoot "plugin\src"
	if (Test-Path -LiteralPath $srcRoot) {
		$files += @(
			Get-ChildItem -LiteralPath $srcRoot -Recurse -File |
				Where-Object { $_.Extension -match '\.(cpp|h|hpp)$' }
		)
	}
	$xmakeLua = Join-Path $RepoRoot "plugin\xmake.lua"
	if (Test-Path -LiteralPath $xmakeLua) {
		$files += Get-Item -LiteralPath $xmakeLua
	}
	$protocol = Join-Path $RepoRoot "protocol"
	if (Test-Path -LiteralPath $protocol) {
		$files += @(
			Get-ChildItem -LiteralPath $protocol -File |
				Where-Object { $_.Extension -match '\.(h|hpp)$' }
		)
	}
	return $files
}

function Get-NewerPluginSources([string]$RepoRoot, [string]$DllPath) {
	if (-not $DllPath -or -not (Test-Path -LiteralPath $DllPath)) { return @("missing DLL") }
	$dllTime = (Get-Item -LiteralPath $DllPath).LastWriteTimeUtc
	return @(
		Get-PluginInputFiles $RepoRoot |
			Where-Object { $_.LastWriteTimeUtc -gt $dllTime } |
			Sort-Object LastWriteTimeUtc -Descending |
			Select-Object -First 5 -ExpandProperty Name
	)
}

function Test-XmakeConfigured([string]$PluginRoot) {
	$config = Join-Path $PluginRoot ".xmake\windows\x64\cache\config"
	if (-not (Test-Path -LiteralPath $config)) { return $false }
	$text = Get-Content -LiteralPath $config -Raw -ErrorAction SilentlyContinue
	if (-not $text) { return $false }
	return ($text -match 'mode\s*=\s*"releasedbg"')
}

function Test-NeedXmakeConfigure([string]$RepoRoot, [string]$PluginRoot, [bool]$Force) {
	if ($Force) { return $true }
	if (-not (Test-XmakeConfigured $PluginRoot)) { return $true }
	$xmakeLua = Join-Path $RepoRoot "plugin\xmake.lua"
	if (-not (Test-Path -LiteralPath $xmakeLua)) { return $true }
	$luaTime = (Get-Item -LiteralPath $xmakeLua).LastWriteTimeUtc
	$cache = Join-Path $PluginRoot ".xmake\windows\x64\cache\config"
	$cacheTime = (Get-Item -LiteralPath $cache).LastWriteTimeUtc
	return ($luaTime -gt $cacheTime)
}

$dllPath = Find-PluginDll $root
if (-not $ForceRebuild -and $dllPath) {
	$newer = Get-NewerPluginSources $root $dllPath
	if ($newer.Count -eq 0) {
		$dllItem = Get-Item -LiteralPath $dllPath
		Write-Host ("Up to date: {0}  {1:N0} bytes  {2:yyyy-MM-dd HH:mm:ss}" -f $dllItem.FullName, $dllItem.Length, $dllItem.LastWriteTime)
		exit 0
	}
	Write-Host ("Incremental build (newer: {0})" -f ($newer -join ", "))
} elseif (-not $ForceRebuild) {
	Write-Host "Incremental build (no DLL yet)"
} else {
	Write-Host "Force rebuild CommonwealthMP"
}

$pluginRoot = Join-Path $root "plugin"
Push-Location $pluginRoot
try {
	if (Test-NeedXmakeConfigure $root $pluginRoot $ForceRebuild.IsPresent) {
		Write-Host "xmake: configuring releasedbg x64"
		& $xmake f -m releasedbg -a x64 -y
		if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
	}

	if ($ForceRebuild) {
		Write-Host "xmake: rebuilding CommonwealthMP (-r)"
		& $xmake build -r CommonwealthMP
	} else {
		Write-Host "xmake: building CommonwealthMP"
		& $xmake build CommonwealthMP
	}
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
