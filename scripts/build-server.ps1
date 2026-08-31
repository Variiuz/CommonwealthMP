#Requires -Version 5.1
param(
	[switch]$SkipTests
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
	$fallback = "C:\Program Files\CMake\bin\cmake.exe"
	if (Test-Path -LiteralPath $fallback) {
		$cmake = $fallback
	} else {
		throw "cmake not found on PATH."
	}
}

$build = Join-Path $root "build\server"
& $cmake -B $build -S $root -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($SkipTests) {
	exit 0
}
& $cmake --build $build --target RUN_TESTS --config Release
exit $LASTEXITCODE
