param(
    [Parameter(Mandatory = $false)]
    [string]$BuildDirectory = "build/windows-release",

    [Parameter(Mandatory = $false)]
    [string]$OutputDirectory = "artifacts/TCGPrint-Native-win64"
)

$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    throw "package-windows.ps1 must run on Windows."
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDirectory
$outputPath = Join-Path $repoRoot $OutputDirectory
$exePath = Join-Path $buildPath "tcgprint.exe"

if (-not (Test-Path $exePath)) {
    throw "TCGPrint executable was not found at: $exePath"
}

$deployTool = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
if (-not $deployTool) {
    throw "windeployqt.exe was not found on PATH."
}

if (Test-Path $outputPath) {
    Remove-Item $outputPath -Recurse -Force
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
Copy-Item $exePath (Join-Path $outputPath "tcgprint.exe")

& $deployTool.Source --release --qmldir (Join-Path $repoRoot "app/qml") --dir $outputPath (Join-Path $outputPath "tcgprint.exe")

if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}

$required = @(
    "tcgprint.exe",
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Qml.dll",
    "Qt6Quick.dll"
)

foreach ($file in $required) {
    if (-not (Test-Path (Join-Path $outputPath $file))) {
        throw "Packaged application is missing required runtime file: $file"
    }
}

Write-Host "Portable Windows package created at: $outputPath"
