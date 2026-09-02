#Requires -Version 5.1
<#
    Downloads real Windows binaries for the third-party tools MediaSuite shells out to,
    and stages them into ..\tools-staged\<folder>\ in the exact layout ToolLocator
    expects (<folder>\<exe> or <folder>\bin\<exe> — see tools\README.md). build.ps1 runs
    this before compiling the installer, so MediaSuite.iss can bundle tools-staged\*
    straight into {app}\tools and ship a self-contained app that needs no manual
    downloads.

    Every tool here is fetched from its own official release channel as a plain
    zip/archive (never an interactive installer with a EULA click-through), so this can
    run unattended in CI. Tools that only ship as a GUI installer (Ghostscript,
    LibreOffice, Calibre, ImageMagick) are handled separately — see fetch-tools-installed.ps1
    once that lands; this first pass covers the tools with a genuinely portable build.
#>

param(
    [string]$ToolsDir = (Join-Path $PSScriptRoot "..\tools-staged")
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"  # Invoke-WebRequest is dramatically faster with the progress bar off.

if (Test-Path $ToolsDir) {
    Remove-Item $ToolsDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null

$webHeaders = @{ "User-Agent" = "MediaSuite-CI" }
if ($env:GITHUB_TOKEN) {
    $webHeaders["Authorization"] = "Bearer $($env:GITHUB_TOKEN)"
}

function Get-LatestReleaseAssetUrl {
    param([Parameter(Mandatory)][string]$Repo, [Parameter(Mandatory)][string]$NamePattern)
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers $webHeaders
    $asset = $release.assets | Where-Object { $_.name -match $NamePattern } | Select-Object -First 1
    if (-not $asset) {
        throw "No release asset matching '$NamePattern' found in the latest release of $Repo. " +
              "Assets were: $($release.assets.name -join ', ')"
    }
    return $asset.browser_download_url
}

function Get-ToArchive {
    param([Parameter(Mandatory)][string]$Url)
    $fileName = [System.IO.Path]::GetFileName(([Uri]$Url).AbsolutePath)
    $archivePath = Join-Path ([System.IO.Path]::GetTempPath()) "mediasuite-dl-$([Guid]::NewGuid())-$fileName"
    Write-Host "  downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $archivePath -Headers $webHeaders -UseBasicParsing
    return $archivePath
}

function Expand-ToTemp {
    param([Parameter(Mandatory)][string]$ArchivePath)
    $extractDir = Join-Path ([System.IO.Path]::GetTempPath()) "mediasuite-extract-$([Guid]::NewGuid())"
    New-Item -ItemType Directory -Force -Path $extractDir | Out-Null
    Expand-Archive -Path $ArchivePath -DestinationPath $extractDir -Force
    Remove-Item $ArchivePath -Force
    return $extractDir
}

function Copy-BinariesToStage {
    <#
        Finds each named file anywhere under $ExtractDir (release zips nest things
        under an arbitrary top-level folder) and copies it, and everything else in the
        same directory (a tool's exe is often useless without its sibling DLLs), into
        $StageDir\<Folder>.
    #>
    param(
        [Parameter(Mandatory)][string]$ExtractDir,
        [Parameter(Mandatory)][string]$Folder,
        [Parameter(Mandatory)][string[]]$PrimaryExeNames
    )
    $stageDir = Join-Path $ToolsDir $Folder
    New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

    $primary = Get-ChildItem -Path $ExtractDir -Recurse -File -Filter $PrimaryExeNames[0] | Select-Object -First 1
    if (-not $primary) {
        throw "Could not find $($PrimaryExeNames[0]) anywhere under $ExtractDir after extracting $Folder."
    }
    $sourceDir = $primary.DirectoryName
    Copy-Item -Path (Join-Path $sourceDir "*") -Destination $stageDir -Recurse -Force

    foreach ($exe in $PrimaryExeNames) {
        if (-not (Test-Path (Join-Path $stageDir $exe))) {
            throw "Expected $exe in $stageDir after staging $Folder, but it is missing."
        }
    }
    Write-Host "  staged $Folder -> $stageDir ($($PrimaryExeNames -join ', '))"
    Remove-Item $ExtractDir -Recurse -Force
}

Write-Host "== FFmpeg =="
# BtbN's GitHub Actions auto-builds: a stable "latest" tag always points at a fresh
# static GPL build. win64-gpl is the full build (includes every codec we need); no EULA.
$ffmpegArchive = Get-ToArchive -Url "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip"
$ffmpegExtract = Expand-ToTemp -ArchivePath $ffmpegArchive
Copy-BinariesToStage -ExtractDir $ffmpegExtract -Folder "ffmpeg" -PrimaryExeNames @("ffmpeg.exe", "ffprobe.exe")

Write-Host "== 7-Zip =="
# 7-Zip's own installer is NSIS-based and supports a fully silent, no-EULA-prompt
# install to an arbitrary directory. We run it into a scratch folder here purely to
# harvest 7z.exe/7z.dll, then throw the installed copy away — nothing about this touches
# the CI machine's real Program Files or registry in a way that matters afterward.
$sevenZipInstallerUrl = "https://www.7-zip.org/a/7z2500-x64.exe"
$sevenZipInstaller = Get-ToArchive -Url $sevenZipInstallerUrl
$sevenZipInstallDir = Join-Path ([System.IO.Path]::GetTempPath()) "mediasuite-7zip-$([Guid]::NewGuid())"
& $sevenZipInstaller "/S" "/D=$sevenZipInstallDir" | Out-Null
# A native exe's own exit code is not a PowerShell terminating error and would not stop
# this script under $ErrorActionPreference alone — checked explicitly, same reasoning as
# the CI workflow's own native-process calls.
if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
    throw "7-Zip silent install failed with exit code $LASTEXITCODE"
}
Start-Sleep -Seconds 2  # The installer exits before its background copy finishes on some runners.
$sevenZipStage = Join-Path $ToolsDir "7zip"
New-Item -ItemType Directory -Force -Path $sevenZipStage | Out-Null
Copy-Item -Path (Join-Path $sevenZipInstallDir "7z.exe") -Destination $sevenZipStage
Copy-Item -Path (Join-Path $sevenZipInstallDir "7z.dll") -Destination $sevenZipStage
Remove-Item $sevenZipInstaller -Force
Remove-Item $sevenZipInstallDir -Recurse -Force
Write-Host "  staged 7zip -> $sevenZipStage"

Write-Host "== QPDF =="
$qpdfUrl = Get-LatestReleaseAssetUrl -Repo "qpdf/qpdf" -NamePattern "qpdf-.*-mingw64\.zip$"
$qpdfArchive = Get-ToArchive -Url $qpdfUrl
$qpdfExtract = Expand-ToTemp -ArchivePath $qpdfArchive
Copy-BinariesToStage -ExtractDir $qpdfExtract -Folder "qpdf" -PrimaryExeNames @("qpdf.exe")

Write-Host "== Pandoc =="
$pandocUrl = Get-LatestReleaseAssetUrl -Repo "jgm/pandoc" -NamePattern "windows-x86_64\.zip$"
$pandocArchive = Get-ToArchive -Url $pandocUrl
$pandocExtract = Expand-ToTemp -ArchivePath $pandocArchive
Copy-BinariesToStage -ExtractDir $pandocExtract -Folder "pandoc" -PrimaryExeNames @("pandoc.exe")

Write-Host "== Real-ESRGAN (ncnn-vulkan) =="
$realesrganUrl = Get-LatestReleaseAssetUrl -Repo "xinntao/Real-ESRGAN" -NamePattern "realesrgan-ncnn-vulkan-.*-windows\.zip$"
$realesrganArchive = Get-ToArchive -Url $realesrganUrl
$realesrganExtract = Expand-ToTemp -ArchivePath $realesrganArchive
Copy-BinariesToStage -ExtractDir $realesrganExtract -Folder "realesrgan" -PrimaryExeNames @("realesrgan-ncnn-vulkan.exe")

Write-Host ""
Write-Host "Tools staged under $ToolsDir :"
Get-ChildItem $ToolsDir -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
