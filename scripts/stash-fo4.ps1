#Requires -Version 5.1
<#
.SYNOPSIS
  Copy the 1.11.169 Steam FO4 install and Vortex data to a stash folder.
  Does NOT Purge Vortex. Does NOT tell Steam to update.
#>
[CmdletBinding()]
param(
	[string]$GamePath = "U:\SteamLibrary\steamapps\common\Fallout 4",
	[string]$StashRoot = "U:\FO4-Stash-1.11.169",
	[switch]$SkipPurgeCheck,
	[switch]$Force
)

$ErrorActionPreference = "Stop"

function Get-FileVersion([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) { return $null }
	return [System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path).FileVersion
}

function Copy-Tree([string]$From, [string]$To, [string]$Label) {
	if (-not (Test-Path -LiteralPath $From)) {
		Write-Host "SKIP $Label (missing): $From"
		return
	}
	New-Item -ItemType Directory -Force -Path $To | Out-Null
	Write-Host "COPY $Label"
	Write-Host "  $From"
	Write-Host "  -> $To"
	& robocopy $From $To /E /COPY:DAT /R:1 /W:1 /MT:8 /NFL /NDL /NP /XJ | Out-Null
	$rc = $LASTEXITCODE
	if ($rc -ge 8) {
		throw "robocopy failed ($rc) for $Label"
	}
}

Write-Host "CommonwealthMP stash (copy only, no Steam update, no Vortex Purge)"
Write-Host "Game:  $GamePath"
Write-Host "Stash: $StashRoot"

if (-not (Test-Path -LiteralPath (Join-Path $GamePath "Fallout4.exe"))) {
	throw "Fallout4.exe not found at $GamePath"
}

if ((Test-Path -LiteralPath $StashRoot) -and -not $Force) {
	throw "Stash already exists: $StashRoot (pass -Force to merge/overwrite)"
}

$liveVer = Get-FileVersion (Join-Path $GamePath "Fallout4.exe")
Write-Host "Live Fallout4.exe FileVersion: $liveVer"
if ($liveVer -and $liveVer -notmatch "1\.11\.169") {
	Write-Host "WARNING: expected 1.11.169.x before stash. You may already have updated Steam."
}

$pluginDir = Join-Path $GamePath "Data\F4SE\Plugins"
if (-not $SkipPurgeCheck -and (Test-Path -LiteralPath $pluginDir)) {
	$extra = Get-ChildItem -LiteralPath $pluginDir -File -ErrorAction SilentlyContinue |
		Where-Object { $_.Name -notmatch '^(f4se|version|steam_api)' }
	if ($extra.Count -gt 8) {
		throw "Data\F4SE\Plugins still looks deployed ($($extra.Count) extra files). Vortex Purge first, or pass -SkipPurgeCheck."
	}
}

Copy-Tree $GamePath $StashRoot "game folder"

$vortexFo4 = Join-Path $env:APPDATA "Vortex\fallout4"
Copy-Tree $vortexFo4 (Join-Path $StashRoot "_vortex\fallout4") "Vortex fallout4 appdata"

$localFo4 = Join-Path $env:LOCALAPPDATA "Fallout4"
Copy-Tree $localFo4 (Join-Path $StashRoot "_localappdata\Fallout4") "LocalAppData Fallout4"

$docs = Join-Path $env:USERPROFILE "Documents\My Games\Fallout4"
Copy-Tree $docs (Join-Path $StashRoot "_documents\Fallout4") "Documents saves/INI (if present)"

$vortexSettings = Join-Path $env:APPDATA "Vortex\settings.json"
if (Test-Path -LiteralPath $vortexSettings) {
	Copy-Item -LiteralPath $vortexSettings -Destination (Join-Path $StashRoot "_vortex\settings.json") -Force
	Write-Host "Copied Vortex settings.json. Also copy Mods staging + downloads from Vortex Settings if they live elsewhere."
}

$stashExe = Join-Path $StashRoot "Fallout4.exe"
$stashVer = Get-FileVersion $stashExe
Write-Host "Stash Fallout4.exe FileVersion: $stashVer"
if (-not $stashVer) {
	throw "Stash copy missing Fallout4.exe"
}

Write-Host ""
Write-Host "Stash copy finished. Next (you, not this script):"
Write-Host "  1. Confirm stash version is still 1.11.169"
Write-Host "  2. Steam-update the LIVE folder only"
Write-Host "  3. Install F4SE 0.7.9 + Address Library on the live folder"
Write-Host "  4. Do not Enable/Deploy the old Vortex 1.11.169 list on the new exe"
Write-Host "Old mods later: $StashRoot\f4se_loader.exe (Steam running, not the Play button)"
