#Requires -Version 5.1
<#
    Publishes MediaSuite.App as a self-contained win-x64 build, fetches all 14 bundled
    tools (see fetch-tools.ps1), then compiles the installer from both. Run from
    anywhere; paths below are relative to this script.

    Needs the .NET 8 SDK and Inno Setup (iscc.exe) on PATH. Inno Setup's own installer
    puts iscc.exe in "C:\Program Files (x86)\Inno Setup 6\" and does not add it to PATH
    by default — add that folder to PATH, or run the compiler from there directly if
    this script cannot find it. Fetching every tool also needs an MSVC + vcpkg toolchain
    (for LibRaw, compiled from source) and Chocolatey (for Calibre) — see fetch-tools.ps1
    for exactly which tool needs which, and its fail-soft behavior when one is missing.
#>

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$publishDir = Join-Path $root "publish\MediaSuite"
$appProject = Join-Path $root "src\MediaSuite.App\MediaSuite.App.csproj"
$issScript = Join-Path $PSScriptRoot "MediaSuite.iss"
$fetchToolsScript = Join-Path $PSScriptRoot "fetch-tools.ps1"

Write-Host "Publishing MediaSuite.App (self-contained win-x64) to $publishDir ..."
if (Test-Path $publishDir) {
    Remove-Item $publishDir -Recurse -Force
}

dotnet publish $appProject -c Release -r win-x64 --self-contained true -o $publishDir
if ($LASTEXITCODE -ne 0) {
    throw "dotnet publish failed with exit code $LASTEXITCODE"
}

Write-Host "Fetching bundled tools ..."
& $fetchToolsScript
if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
    throw "fetch-tools.ps1 failed with exit code $LASTEXITCODE"
}

$iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
if (-not $iscc) {
    $defaultIscc = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    if (Test-Path $defaultIscc) {
        $iscc = $defaultIscc
    }
    else {
        throw "iscc.exe not found on PATH or at the default Inno Setup 6 install location. " +
              "Install Inno Setup from https://jrsoftware.org/isinfo.php and try again."
    }
}
else {
    $iscc = $iscc.Source
}

Write-Host "Compiling installer with $iscc ..."
& $iscc $issScript
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compile failed with exit code $LASTEXITCODE"
}

Write-Host "Done — installer written to $root\dist\"
