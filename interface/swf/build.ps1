#Requires -Version 5.1
# Builds CommonwealthMP_Menu.swf from src/CommonwealthMP_Menu.as.
#
# Toolchain (one-time, outside the repo):
#   Adobe Flex SDK 4.6 (extracted to _tools/adobe-flex by build bootstrap)
#   playerglobal.swc for Flash Player 32 (_tools/player/32.0/)
#   Java 8-21 on PATH (Java 25 may fail; set CMP_JAVA if needed)
#
# Override paths with environment variables:
#   $env:CMP_FLEX_SDK = "E:\tools\flex"
#   $env:CMP_PLAYERGLOBAL = "E:\tools\player\32.0\playerglobal.swc"
#   $env:CMP_JAVA = "C:\path\to\java.exe"

$ErrorActionPreference = "Stop"

$work = Split-Path -Parent $MyInvocation.MyCommand.Path
$defaultFlex = Join-Path $work "_tools\adobe-flex"
$defaultPg = Join-Path $work "_tools\player\32.0\playerglobal.swc"
$flex = if ($env:CMP_FLEX_SDK) { $env:CMP_FLEX_SDK } else { $defaultFlex }
$pg = if ($env:CMP_PLAYERGLOBAL) { $env:CMP_PLAYERGLOBAL } else { $defaultPg }

$java = if ($env:CMP_JAVA) { $env:CMP_JAVA } else { "java" }
$src = Join-Path $work "src\CommonwealthMP_Menu.as"
$out = Join-Path $work "CommonwealthMP_Menu.swf"
$modInterface = Join-Path (Split-Path -Parent (Split-Path -Parent $work)) "mod\CommonwealthMP\Interface"

if (-not (Test-Path -LiteralPath "$flex\lib\mxmlc.jar")) {
	throw "Flex SDK not found at $flex. Set CMP_FLEX_SDK or edit build.ps1."
}
if (-not (Test-Path -LiteralPath $pg)) {
	throw "playerglobal.swc not found at $pg. Set CMP_PLAYERGLOBAL or edit build.ps1."
}

& $java -jar "$flex\lib\mxmlc.jar" `
	+flexlib="$flex\frameworks" `
	"-load-config=" `
	"-external-library-path=$pg" `
	"-swf-version=32" `
	"-source-path=$work\src" `
	"-output=$out" `
	"$src"

if (-not (Test-Path -LiteralPath $out)) {
	throw "SWF build failed."
}

Write-Output ("Built: {0} ({1} bytes)" -f $out, (Get-Item -LiteralPath $out).Length)

New-Item -ItemType Directory -Force -Path $modInterface | Out-Null
Copy-Item -LiteralPath $out -Destination (Join-Path $modInterface "CommonwealthMP_Menu.swf") -Force
Write-Output ("Deployed to: {0}" -f $modInterface)
