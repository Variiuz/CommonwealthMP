<#
.SYNOPSIS
  Extract Fallout 4 animation-related scripts/assets for local CMP research.

.DESCRIPTION
  Locates the FO4 install, extracts filtered Papyrus (.pex) from Scripts.ba2 and
  matching Character mesh/behavior paths when possible. Optionally decompiles with
  Champollion. Always writes a CMP puppet graph-var cross-ref from the repo.

  Extracted Bethesda assets stay under research/fo4-anim/ (gitignored).
#>
[CmdletBinding()]
param(
	[string]$GameRoot = "",
	[string]$OutDir = "",
	[string]$ArchiveTool = "",
	[string]$Champollion = "",
	[switch]$DryRun,
	[switch]$Help
)

$ErrorActionPreference = "Stop"

if ($Help) {
	Write-Host @"
extract-fo4-anim-research.ps1

  -GameRoot PATH       Fallout 4 install root (contains Fallout4.exe / Data)
  -OutDir PATH         Output folder (default: <repo>/research/fo4-anim)
  -ArchiveTool PATH    Archive2.exe, BAE, or bsarch
  -Champollion PATH    Champollion.exe for .pex -> .psc
  -DryRun              Print plan only; no extract
  -Help                This text

Without -GameRoot, tries Steam registry + common Steam library paths.
"@
	exit 0
}

function Find-RepoRoot {
	$here = $PSScriptRoot
	if (-not $here) { $here = Get-Location }
	return (Resolve-Path (Join-Path $here "..")).Path
}

function Find-GameRoot {
	param([string]$Hint)
	if ($Hint -and (Test-Path -LiteralPath (Join-Path $Hint "Fallout4.exe"))) {
		return (Resolve-Path -LiteralPath $Hint).Path
	}
	$candidates = @()
	try {
		$steam = Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue
		if ($steam -and $steam.SteamPath) {
			$candidates += (Join-Path $steam.SteamPath "steamapps\common\Fallout 4")
		}
	} catch {}
	$candidates += @(
		"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4",
		"D:\Steam\steamapps\common\Fallout 4",
		"E:\Steam\steamapps\common\Fallout 4"
	)
	foreach ($c in $candidates) {
		if ($c -and (Test-Path -LiteralPath (Join-Path $c "Fallout4.exe"))) {
			return (Resolve-Path -LiteralPath $c).Path
		}
	}
	return $null
}

function Find-Archive2 {
	param([string]$Hint)
	if ($Hint -and (Test-Path -LiteralPath $Hint)) {
		return (Resolve-Path -LiteralPath $Hint).Path
	}
	foreach ($name in @("Archive2.exe", "BAE.exe", "bsarch.exe")) {
		$cmd = Get-Command $name -ErrorAction SilentlyContinue
		if ($cmd) { return $cmd.Source }
	}
	$ck = @(
		"${env:ProgramFiles(x86)}\Bethesda.net Launcher\games\Fallout4\Tools\Archive2\Archive2.exe",
		"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Tools\Archive2\Archive2.exe"
	)
	foreach ($p in $ck) {
		if (Test-Path -LiteralPath $p) { return $p }
	}
	return $null
}

function Find-Champollion {
	param([string]$Hint)
	if ($Hint -and (Test-Path -LiteralPath $Hint)) {
		return (Resolve-Path -LiteralPath $Hint).Path
	}
	$cmd = Get-Command "Champollion.exe" -ErrorAction SilentlyContinue
	if ($cmd) { return $cmd.Source }
	return $null
}

function Test-AnimScriptName {
	param([string]$Name)
	$n = $Name.ToLowerInvariant()
	$keys = @(
		"anim", "idle", "weapon", "locomotion", "sneak", "sprint", "jump",
		"reload", "attack", "draw", "sheathe", "gun", "combat", "actor",
		"player", "furniture", "pipboy", "holster"
	)
	foreach ($k in $keys) {
		if ($n.Contains($k)) { return $true }
	}
	return $false
}

function Write-CmpPuppetCrossRef {
	param([string]$RepoRoot, [string]$DestFile)
	$apply = Join-Path $RepoRoot "plugin\src\puppet\apply.cpp"
	$vars = New-Object System.Collections.Generic.List[string]
	$events = New-Object System.Collections.Generic.List[string]
	if (Test-Path -LiteralPath $apply) {
		$content = Get-Content -LiteralPath $apply -Raw
		foreach ($m in [regex]::Matches($content, 'Set(?:Bool|Int|Float)Var\([^,]+,\s*"([^"]+)"')) {
			if (-not $vars.Contains($m.Groups[1].Value)) { $vars.Add($m.Groups[1].Value) }
		}
		foreach ($m in [regex]::Matches($content, 'FireEvent\([^,]+,\s*"([^"]+)"')) {
			if (-not $events.Contains($m.Groups[1].Value)) { $events.Add($m.Groups[1].Value) }
		}
	}
	$sb = New-Object System.Text.StringBuilder
	[void]$sb.AppendLine("# CMP puppet graph cross-ref")
	[void]$sb.AppendLine("")
	[void]$sb.AppendLine("Generated from ``plugin/src/puppet/apply.cpp``. Live FormID / race behaviorGraph dump: in-game ``cmp_dump``.")
	[void]$sb.AppendLine("")
	[void]$sb.AppendLine("## Graph variables")
	[void]$sb.AppendLine("")
	foreach ($v in ($vars | Sort-Object)) { [void]$sb.AppendLine("- ``$v``") }
	[void]$sb.AppendLine("")
	[void]$sb.AppendLine("## NotifyAnimationGraph events")
	[void]$sb.AppendLine("")
	foreach ($e in ($events | Sort-Object)) { [void]$sb.AppendLine("- ``$e``") }
	[void]$sb.AppendLine("")
	$dir = Split-Path -Parent $DestFile
	New-Item -ItemType Directory -Force -Path $dir | Out-Null
	Set-Content -LiteralPath $DestFile -Value $sb.ToString() -Encoding UTF8
}

$repo = Find-RepoRoot
if (-not $OutDir) {
	$OutDir = Join-Path $repo "research\fo4-anim"
}
$game = Find-GameRoot -Hint $GameRoot
$arch = Find-Archive2 -Hint $ArchiveTool
$champ = Find-Champollion -Hint $Champollion

Write-Host "repo:     $repo"
Write-Host "out:      $OutDir"
Write-Host "game:     $(if ($game) { $game } else { '(not found)' })"
Write-Host "archive:  $(if ($arch) { $arch } else { '(not found)' })"
Write-Host "champ:    $(if ($champ) { $champ } else { '(optional, not found)' })"

Write-CmpPuppetCrossRef -RepoRoot $repo -DestFile (Join-Path $OutDir "cmp-puppet-vars.md")
Write-Host "wrote cmp-puppet-vars.md"

if ($DryRun) {
	Write-Host "DryRun: skipping BA2 extract."
	exit 0
}

if (-not $game) {
	Write-Warning "Fallout 4 not found. Pass -GameRoot. Cross-ref still written."
	exit 0
}
if (-not $arch) {
	Write-Error "No BA2 tool found. Install Creation Kit Archive2, or pass -ArchiveTool to BAE/bsarch."
}

$data = Join-Path $game "Data"
$scriptsBa2 = Join-Path $data "Fallout4 - Scripts.ba2"
$meshesBa2 = Join-Path $data "Fallout4 - Meshes.ba2"
$animBa2 = Join-Path $data "Fallout4 - Animations.ba2"

New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "pex") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "psc") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "meshes") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $OutDir "_tmp") | Out-Null

$manifest = New-Object System.Collections.Generic.List[string]
$manifest.Add("game=$game")
$manifest.Add("archive=$arch")
$manifest.Add("champollion=$(if ($champ) { $champ } else { 'none' })")

$toolLeaf = [IO.Path]::GetFileName($arch).ToLowerInvariant()

function Invoke-Ba2Extract {
	param([string]$Ba2, [string]$Dest)
	if (-not (Test-Path -LiteralPath $Ba2)) {
		Write-Warning "missing archive: $Ba2"
		return $false
	}
	New-Item -ItemType Directory -Force -Path $Dest | Out-Null
	if ($toolLeaf -eq "archive2.exe") {
		& $arch $Ba2 "-e=$Dest" | Out-Null
		return $LASTEXITCODE -eq 0
	}
	if ($toolLeaf -eq "bae.exe") {
		& $arch -e $Ba2 $Dest | Out-Null
		return $true
	}
	if ($toolLeaf -eq "bsarch.exe") {
		& $arch unpack $Ba2 $Dest | Out-Null
		return $LASTEXITCODE -eq 0
	}
	Write-Error "Unsupported archive tool: $arch"
	return $false
}

$tmpScripts = Join-Path $OutDir "_tmp\scripts"
if (Test-Path -LiteralPath $scriptsBa2) {
	Write-Host "extracting Scripts.ba2 ..."
	Invoke-Ba2Extract -Ba2 $scriptsBa2 -Dest $tmpScripts | Out-Null
	$pexOut = Join-Path $OutDir "pex"
	Get-ChildItem -LiteralPath $tmpScripts -Recurse -Filter *.pex -ErrorAction SilentlyContinue | ForEach-Object {
		if (Test-AnimScriptName -Name $_.BaseName) {
			Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $pexOut $_.Name) -Force
			$manifest.Add("pex=$($_.Name)")
		}
	}
} else {
	Write-Warning "Scripts.ba2 not found at $scriptsBa2"
}

foreach ($ba2 in @($meshesBa2, $animBa2)) {
	if (-not (Test-Path -LiteralPath $ba2)) { continue }
	$tmp = Join-Path $OutDir ("_tmp\" + [IO.Path]::GetFileNameWithoutExtension($ba2))
	Write-Host "extracting $([IO.Path]::GetFileName($ba2)) (Character filter) ..."
	Invoke-Ba2Extract -Ba2 $ba2 -Dest $tmp | Out-Null
	$charRoot = Join-Path $tmp "Meshes\Actors\Character"
	if (Test-Path -LiteralPath $charRoot) {
		$destChar = Join-Path $OutDir "meshes\Actors\Character"
		New-Item -ItemType Directory -Force -Path $destChar | Out-Null
		Copy-Item -LiteralPath $charRoot -Destination $destChar -Recurse -Force
		$manifest.Add("meshes=Actors/Character from $([IO.Path]::GetFileName($ba2))")
	}
}

if ($champ) {
	$pexDir = Join-Path $OutDir "pex"
	$pscDir = Join-Path $OutDir "psc"
	Get-ChildItem -LiteralPath $pexDir -Filter *.pex -ErrorAction SilentlyContinue | ForEach-Object {
		& $champ $_.FullName "-p$pscDir" | Out-Null
		$manifest.Add("psc=$($_.BaseName).psc")
	}
}

Set-Content -LiteralPath (Join-Path $OutDir "manifest.txt") -Value ($manifest -join "`n") -Encoding UTF8
Remove-Item -LiteralPath (Join-Path $OutDir "_tmp") -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "done: $OutDir"
