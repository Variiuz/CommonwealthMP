#Requires -Version 5.1
<#
.SYNOPSIS
  Build a Mod Organizer 2 archive you can drag onto MO2 (or copy into mods).

.DESCRIPTION
  Stages mod/CommonwealthMP and writes dist/CommonwealthMP-<version>-mo2.zip.
  Zip root is Data (ESP + F4SE) plus fomod/info.xml so MO2 shows the real version
  without a download .meta sidecar.

.EXAMPLE
  .\scripts\pack-mo2.ps1
  .\scripts\pack-mo2.ps1 -Build
  .\scripts\pack-mo2.ps1 -Build -ForceRebuild
  .\scripts\pack-mo2.ps1 -Build -Esp -Esm D:\Steam\steamapps\common\Fallout 4\Data\Fallout4.esm
  .\scripts\pack-mo2.ps1 -Mo2 -Mo2Path D:\Modding\MO2
  .\scripts\pack-mo2.ps1 -Mo2 -NoZip -Mo2Path D:\Modding\MO2
  .\scripts\pack-mo2.ps1 -Out D:\cmp-mo2.zip
#>
[CmdletBinding()]
param(
	[switch]$Build,
	[switch]$ForceRebuild,
	[switch]$Esp,
	[switch]$Mo2,
	[switch]$NoZip,
	[string]$Mo2Path = "",
	[string]$Mo2Mods = "",
	[string]$Dll = "",
	[string]$Esm = "",
	[string]$Out = "",
	[string]$Version = ""
)

$ErrorActionPreference = "Stop"

function Get-CmpVersion([string]$RepoRoot, [string]$Override) {
	if ($Override) { return $Override }
	$lua = Join-Path $RepoRoot "plugin\xmake.lua"
	if (Test-Path -LiteralPath $lua) {
		foreach ($line in Get-Content -LiteralPath $lua) {
			if ($line -match 'set_version\("([^"]+)"\)') {
				return $Matches[1]
			}
		}
	}
	throw "Could not read set_version from plugin\xmake.lua. Pass -Version."
}

function Get-Mo2IniValue([string]$IniPath, [string]$Key) {
	if (-not (Test-Path -LiteralPath $IniPath)) { return "" }
	foreach ($line in Get-Content -LiteralPath $IniPath) {
		if ($line -match ("^" + [regex]::Escape($Key) + "=(.+)$")) {
			$value = $Matches[1].Trim()
			if ($value -match '^@ByteArray\((.+)\)$') {
				return $Matches[1]
			}
			return $value
		}
	}
	return ""
}

function Get-Mo2ModsPath([string]$InstallDir, [string]$Override) {
	if ($Override) { return $Override }
	$base = Get-Mo2IniValue (Join-Path $InstallDir "ModOrganizer.ini") "base_directory"
	if ($base) { return (Join-Path $base "mods") }
	return (Join-Path $InstallDir "mods")
}

function Write-Utf8([string]$Path, [string]$Text) {
	$dir = Split-Path -Parent $Path
	if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
	$utf8 = New-Object System.Text.UTF8Encoding $false
	[System.IO.File]::WriteAllText($Path, $Text, $utf8)
}

function Write-Fomod([string]$PackRoot, [string]$Ver, [bool]$HasEsp, [bool]$HasInterface) {
	$info = @"
<?xml version="1.0" encoding="UTF-8"?>
<fomod>
  <Name>CommonwealthMP</Name>
  <Author>CommonwealthMP</Author>
  <Version MachineVersion="$Ver">$Ver</Version>
  <Description>Fallout 4 coop over a dedicated server.</Description>
  <Groups>
    <element>Gameplay</element>
  </Groups>
</fomod>
"@
	$espLine = ""
	if ($HasEsp) {
		$espLine = "    <file source=`"CommonwealthMP.esp`" destination=`"CommonwealthMP.esp`" />`n"
	}
	$ifaceLine = ""
	if ($HasInterface) {
		$ifaceLine = "    <folder source=`"Interface`" destination=`"Interface`" />`n"
	}
	$config = @"
<?xml version="1.0" encoding="UTF-8"?>
<config xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://qconsulting.ca/fo3/ModConfig5.0.xsd">
  <moduleName>CommonwealthMP</moduleName>
  <requiredInstallFiles>
$espLine    <folder source="F4SE" destination="F4SE" />
$ifaceLine  </requiredInstallFiles>
</config>
"@
	Write-Utf8 (Join-Path $PackRoot "fomod\info.xml") $info
	Write-Utf8 (Join-Path $PackRoot "fomod\ModuleConfig.xml") $config
}

function Find-PluginDll([string]$RepoRoot, [string]$Override) {
	if ($Override) { return $Override }
	$candidates = @(
		(Join-Path $RepoRoot "plugin\build\windows\x64\releasedbg\CommonwealthMP.dll"),
		(Join-Path $RepoRoot "plugin\build\windows\x64\release\CommonwealthMP.dll")
	) | Where-Object { Test-Path -LiteralPath $_ }
	if (-not $candidates -or $candidates.Count -eq 0) { return $null }
	# Prefer the newest build artifact (releasedbg vs release can both exist).
	return @(
		$candidates | ForEach-Object { Get-Item -LiteralPath $_ } |
			Sort-Object LastWriteTime -Descending |
			Select-Object -First 1 -ExpandProperty FullName
	)[0]
}

function Test-PluginSourcesNewer([string]$RepoRoot, [string]$DllPath) {
	if (-not (Test-Path -LiteralPath $DllPath)) { return $true }
	$dllTime = (Get-Item -LiteralPath $DllPath).LastWriteTimeUtc
	$inputs = @()
	$srcRoot = Join-Path $RepoRoot "plugin\src"
	if (Test-Path -LiteralPath $srcRoot) {
		$inputs += @(
			Get-ChildItem -LiteralPath $srcRoot -Recurse -File |
				Where-Object { $_.Extension -match '\.(cpp|h|hpp)$' }
		)
	}
	$xmakeLua = Join-Path $RepoRoot "plugin\xmake.lua"
	if (Test-Path -LiteralPath $xmakeLua) {
		$inputs += Get-Item -LiteralPath $xmakeLua
	}
	$protocol = Join-Path $RepoRoot "protocol"
	if (Test-Path -LiteralPath $protocol) {
		$inputs += @(
			Get-ChildItem -LiteralPath $protocol -File |
				Where-Object { $_.Extension -match '\.(h|hpp)$' }
		)
	}
	$newer = $inputs | Where-Object { $_.LastWriteTimeUtc -gt $dllTime } | Select-Object -First 1
	return [bool]$newer
}

$root = Split-Path -Parent $PSScriptRoot
$stage = Join-Path $root "mod\CommonwealthMP"
$ver = Get-CmpVersion $root $Version

if ($Build -or $ForceRebuild) {
	$buildArgs = @()
	if ($ForceRebuild) { $buildArgs += "-ForceRebuild" }
	& (Join-Path $PSScriptRoot "build-plugin.ps1") @buildArgs
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$dllPath = Find-PluginDll $root $Dll
if (-not $dllPath -or -not (Test-Path -LiteralPath $dllPath)) {
	throw "CommonwealthMP.dll not built. Run with -Build, or scripts\build-plugin.ps1 first."
}

if (-not $Build -and -not $ForceRebuild -and (Test-PluginSourcesNewer $root $dllPath)) {
	$dllItemWarn = Get-Item -LiteralPath $dllPath
	Write-Warning ("Plugin sources are newer than DLL ({0:yyyy-MM-dd HH:mm:ss}). Deploying a stale build. Re-run pack-mo2.ps1 -Build." -f $dllItemWarn.LastWriteTime)
}

$ini = Join-Path $root "data\F4SE\Plugins\CommonwealthMP.ini"
if (-not (Test-Path -LiteralPath $ini)) {
	throw "Missing $ini"
}

$espSrc = Join-Path $root "data\CommonwealthMP.esp"
if ($Esp) {
	if (-not $Esm) {
		throw "Pass -Esm with the path to this install's Fallout4.esm."
	}
	$gen = Join-Path $PSScriptRoot "gen_esp.py"
	$pyArgs = @("-3", $gen, "--esm", $Esm)
	Write-Host "Generating CommonwealthMP.esp"
	& py @pyArgs
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
	if (-not (Test-Path -LiteralPath $espSrc)) {
		throw "CommonwealthMP.esp missing after -Esp (needs Fallout4.esm)."
	}
}
$hasEsp = Test-Path -LiteralPath $espSrc

$plugins = Join-Path $stage "F4SE\Plugins"
New-Item -ItemType Directory -Force -Path $plugins | Out-Null
Copy-Item -LiteralPath $dllPath -Destination (Join-Path $plugins "CommonwealthMP.dll") -Force
Copy-Item -LiteralPath $ini -Destination (Join-Path $plugins "CommonwealthMP.ini") -Force
$reporterCandidates = @(
	(Join-Path $root "tools\cmp-reporter\dist\cmp-reporter.exe"),
	(Join-Path $root "tools\cmp-reporter\target\release\cmp-reporter.exe")
)
$reporterExe = $reporterCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$hasReporter = $false
if ($reporterExe) {
	Copy-Item -LiteralPath $reporterExe -Destination (Join-Path $plugins "cmp-reporter.exe") -Force
	$hasReporter = $true
	Write-Host "  F4SE\Plugins\cmp-reporter.exe staged"
} else {
	Write-Host "  cmp-reporter.exe skipped (run scripts\build-reporter.ps1 to add crash GUI)"
}
	if ($hasEsp) {
		Copy-Item -LiteralPath $espSrc -Destination (Join-Path $stage "CommonwealthMP.esp") -Force
	}

$interfaceSwfBuilt = Join-Path $root "interface\swf\CommonwealthMP_Menu.swf"
$interfaceSwfMod = Join-Path $root "mod\CommonwealthMP\Interface\CommonwealthMP_Menu.swf"
$interfaceSwf = if (Test-Path -LiteralPath $interfaceSwfBuilt) {
	$interfaceSwfBuilt
} elseif (Test-Path -LiteralPath $interfaceSwfMod) {
	$interfaceSwfMod
} else {
	$null
}
$hasInterface = $false
if ($interfaceSwf) {
	$ifaceDir = Join-Path $stage "Interface"
	New-Item -ItemType Directory -Force -Path $ifaceDir | Out-Null
	$ifaceDest = Join-Path $ifaceDir "CommonwealthMP_Menu.swf"
	$srcFull = [System.IO.Path]::GetFullPath($interfaceSwf)
	$dstFull = [System.IO.Path]::GetFullPath($ifaceDest)
	if ($srcFull -ne $dstFull) {
		Copy-Item -LiteralPath $interfaceSwf -Destination $ifaceDest -Force
	}
	$hasInterface = $true
	Write-Host "  Interface\CommonwealthMP_Menu.swf staged"
} else {
	Write-Host "  Interface SWF skipped (run interface\swf\build.ps1 to add pause-menu rows)"
}

$meta = Join-Path $stage "meta.ini"
@(
	"[General]",
	"gameName=Fallout4",
	"modid=0",
	"version=$ver",
	"newestVersion=$ver",
	"category=`"38,`"",
	"nexusDescription=",
	"url=",
	"hasCustomURL=false",
	"installationFile=",
	"repository=",
	"comments=Fallout 4 Multiplayer Mod",
	"notes=",
	"converted=false",
	"validated=false",
	"endorsed=0",
	"tracked=0"
) | Set-Content -LiteralPath $meta -Encoding UTF8

$dllItem = Get-Item -LiteralPath (Join-Path $plugins "CommonwealthMP.dll")
Write-Host "Staged $stage"
Write-Host ("  from {0}" -f $dllPath)
Write-Host ("  DLL {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}" -f $dllItem.Length, $dllItem.LastWriteTime)
if ($hasEsp) {
	$espItem = Get-Item -LiteralPath (Join-Path $stage "CommonwealthMP.esp")
	Write-Host ("  ESP {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}" -f $espItem.Length, $espItem.LastWriteTime)
} else {
	Write-Host "  ESP skipped (optional; pass -Esp to generate)"
}
if ($hasReporter) {
	$repItem = Get-Item -LiteralPath (Join-Path $plugins "cmp-reporter.exe")
	Write-Host ("  Reporter {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}" -f $repItem.Length, $repItem.LastWriteTime)
}

if (-not $NoZip) {
	if (-not $Out) {
		$Out = Join-Path $root "dist\CommonwealthMP-$ver-mo2.zip"
	}
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
	if (Test-Path -LiteralPath $Out) {
		Remove-Item -LiteralPath $Out -Force
	}

	$zipStage = Join-Path $env:TEMP ("cmp-mo2-" + [guid]::NewGuid().ToString("N"))
	try {
		# Flat Data root + fomod/. No wrapper folder. FOMOD Version stops MO2 using the zip date.
		$pack = Join-Path $zipStage "pack"
		New-Item -ItemType Directory -Force -Path (Join-Path $pack "F4SE\Plugins") | Out-Null
		if ($hasEsp) {
			Copy-Item -LiteralPath (Join-Path $stage "CommonwealthMP.esp") -Destination (Join-Path $pack "CommonwealthMP.esp") -Force
		}
		Copy-Item -LiteralPath (Join-Path $plugins "CommonwealthMP.dll") -Destination (Join-Path $pack "F4SE\Plugins\CommonwealthMP.dll") -Force
		Copy-Item -LiteralPath (Join-Path $plugins "CommonwealthMP.ini") -Destination (Join-Path $pack "F4SE\Plugins\CommonwealthMP.ini") -Force
		if ($hasReporter) {
			Copy-Item -LiteralPath (Join-Path $plugins "cmp-reporter.exe") -Destination (Join-Path $pack "F4SE\Plugins\cmp-reporter.exe") -Force
		}
		if ($hasInterface) {
			New-Item -ItemType Directory -Force -Path (Join-Path $pack "Interface") | Out-Null
			Copy-Item -LiteralPath (Join-Path $stage "Interface\CommonwealthMP_Menu.swf") -Destination (Join-Path $pack "Interface\CommonwealthMP_Menu.swf") -Force
		}
		Write-Fomod $pack $ver $hasEsp $hasInterface
		Compress-Archive -Path (Join-Path $pack "*") -DestinationPath $Out -CompressionLevel Optimal
	} finally {
		if (Test-Path -LiteralPath $zipStage) {
			Remove-Item -LiteralPath $zipStage -Recurse -Force
		}
	}

	$sidecar = $Out + ".meta"
	if (Test-Path -LiteralPath $sidecar) {
		Remove-Item -LiteralPath $sidecar -Force
	}
	$zipItem = Get-Item -LiteralPath $Out
	Write-Host ("Ready to add in MO2: {0}  ({1:N0} bytes, version {2})" -f $zipItem.FullName, $zipItem.Length, $ver)
	Write-Host "Send only that zip. MO2 reads version from fomod/info.xml inside it."
	Write-Host "Drag onto Mod Organizer (or Install Mod from archive), name it CommonwealthMP, enable the mod + ESP."
}

if (-not $Mo2) {
	exit 0
}

if (-not $Mo2Path -and -not $Mo2Mods) {
	throw "Pass -Mo2Path or -Mo2Mods."
}

$mods = Get-Mo2ModsPath $Mo2Path $Mo2Mods
if (-not (Test-Path -LiteralPath $mods)) {
	throw "MO2 mods folder not found: $mods"
}

$locks = @(Get-Process -Name Fallout4,Fallout4VR,f4se_loader -ErrorAction SilentlyContinue)
if ($locks.Count -gt 0) {
	$names = ($locks | ForEach-Object { $_.Name } | Sort-Object -Unique) -join ", "
	throw "Close Fallout 4 before replacing the DLL (still running: $names)."
}

$dest = Join-Path $mods "CommonwealthMP"
New-Item -ItemType Directory -Force -Path (Join-Path $dest "F4SE\Plugins") | Out-Null
$destDll = Join-Path $dest "F4SE\Plugins\CommonwealthMP.dll"
$destIni = Join-Path $dest "F4SE\Plugins\CommonwealthMP.ini"
$destEsp = Join-Path $dest "CommonwealthMP.esp"
try {
	Copy-Item -LiteralPath (Join-Path $plugins "CommonwealthMP.dll") -Destination $destDll -Force
} catch {
	throw "Failed to copy DLL into MO2 (is Fallout 4 still locking it?): $destDll`n$_"
}
Copy-Item -LiteralPath (Join-Path $plugins "CommonwealthMP.ini") -Destination $destIni -Force
if ($hasReporter) {
	Copy-Item -LiteralPath (Join-Path $plugins "cmp-reporter.exe") -Destination (Join-Path $dest "F4SE\Plugins\cmp-reporter.exe") -Force
}
if ($hasEsp) {
	Copy-Item -LiteralPath (Join-Path $stage "CommonwealthMP.esp") -Destination $destEsp -Force
}
if ($hasInterface) {
	$destIface = Join-Path $dest "Interface"
	New-Item -ItemType Directory -Force -Path $destIface | Out-Null
	Copy-Item -LiteralPath (Join-Path $stage "Interface\CommonwealthMP_Menu.swf") -Destination (Join-Path $destIface "CommonwealthMP_Menu.swf") -Force
}
Copy-Item -LiteralPath $meta -Destination (Join-Path $dest "meta.ini") -Force

$srcHash = (Get-FileHash -LiteralPath (Join-Path $plugins "CommonwealthMP.dll") -Algorithm SHA256).Hash
$dstHash = (Get-FileHash -LiteralPath $destDll -Algorithm SHA256).Hash
if ($srcHash -ne $dstHash) {
	throw "MO2 DLL hash mismatch after copy. Close the game, then re-run."
}
$destDllItem = Get-Item -LiteralPath $destDll
$destIniItem = Get-Item -LiteralPath $destIni
Write-Host ("Replaced files in {0} (version {1})" -f $dest, $ver)
Write-Host ("  DLL {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}  sha256={2}..." -f $destDllItem.Length, $destDllItem.LastWriteTime, $dstHash.Substring(0, 12))
Write-Host ("  INI {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}" -f $destIniItem.Length, $destIniItem.LastWriteTime)
if ($hasEsp -and (Test-Path -LiteralPath $destEsp)) {
	$destEspItem = Get-Item -LiteralPath $destEsp
	Write-Host ("  ESP {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}" -f $destEspItem.Length, $destEspItem.LastWriteTime)
}
if ($hasInterface) {
	$destSwf = Join-Path $dest "Interface\CommonwealthMP_Menu.swf"
	if (Test-Path -LiteralPath $destSwf) {
		$destSwfItem = Get-Item -LiteralPath $destSwf
		Write-Host ("  SWF {0:N0} bytes  {1:yyyy-MM-dd HH:mm:ss}" -f $destSwfItem.Length, $destSwfItem.LastWriteTime)
	}
}
Write-Host "Did not touch plugins.txt / modlist.txt. Enable the mod and ESP in MO2 if needed."
