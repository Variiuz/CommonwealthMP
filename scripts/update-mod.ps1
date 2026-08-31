#Requires -Version 5.1
<#
.SYNOPSIS
  Rebuild CommonwealthMP.dll and replace the files in your MO2 mod folder.

.DESCRIPTION
  Copies DLL, INI, ESP, and meta.ini into mods\CommonwealthMP. Does not touch
  plugins.txt, modlist.txt, or loadorder.txt. Enable the mod and ESP in MO2 yourself.

  Builds first by default so you do not keep a stale DLL. Pass -NoBuild to copy only.

.EXAMPLE
  .\scripts\update-mod.ps1
  .\scripts\update-mod.ps1 -NoBuild
#>
[CmdletBinding()]
param(
	[switch]$Build,
	[switch]$NoBuild,
	[string]$Mo2Path = "C:\Modding\MO2",
	[string]$Mo2Mods = "",
	[string]$Dll = ""
)

$ErrorActionPreference = "Stop"
if ($NoBuild -and $Build) {
	throw "Use either -Build or -NoBuild, not both."
}
$doBuild = -not $NoBuild
if ($doBuild) {
	Write-Host "Building plugin, then replacing MO2 files (DLL / INI / ESP)."
} else {
	Write-Host "Copying existing plugin files into MO2 (no build)."
}
$forward = @{
	Build = $doBuild
	Mo2 = $true
	NoZip = $true
	Mo2Path = $Mo2Path
	Mo2Mods = $Mo2Mods
}
if ($Dll) { $forward.Dll = $Dll }
& (Join-Path $PSScriptRoot "pack-mo2.ps1") @forward
exit $LASTEXITCODE
