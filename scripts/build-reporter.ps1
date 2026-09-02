#Requires -Version 5.1
<#
.SYNOPSIS
  Build tools/cmp-reporter

.DESCRIPTION
  Runs cargo build --release and copies cmp-reporter.exe to
  tools/cmp-reporter/dist/ for pack-mo2.ps1 to pick up.
#>
[CmdletBinding()]
param(
	[switch]$Force
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$proj = Join-Path $root "tools\cmp-reporter"
if (-not (Test-Path -LiteralPath (Join-Path $proj "Cargo.toml"))) {
	throw "missing $proj\Cargo.toml"
}

$cargo = (Get-Command cargo -ErrorAction SilentlyContinue).Source
if (-not $cargo) {
	throw "cargo not found on PATH (install Rust from https://rustup.rs/)"
}

Push-Location $proj
try {
	$env:CARGO_TARGET_DIR = Join-Path $proj "target"
	Write-Host "cargo: building cmp-reporter (release)"
	& $cargo build --release
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

	$exe = Join-Path $proj "target\release\cmp-reporter.exe"
	if (-not (Test-Path -LiteralPath $exe)) {
		throw "cmp-reporter.exe not found after cargo build ($exe)"
	}

	$dist = Join-Path $proj "dist"
	New-Item -ItemType Directory -Force -Path $dist | Out-Null
	Copy-Item -LiteralPath $exe -Destination (Join-Path $dist "cmp-reporter.exe") -Force
	$item = Get-Item -LiteralPath (Join-Path $dist "cmp-reporter.exe")
	Write-Host ("Built {0}  {1:N0} bytes  {2:yyyy-MM-dd HH:mm:ss}" -f $item.FullName, $item.Length, $item.LastWriteTime)
} finally {
	Pop-Location
}
exit 0
