#Requires -Version 5.1
<#
    Downloads real Windows binaries for the third-party tools MediaSuite shells out to,
    and stages them into ..\tools-staged\<folder>\ in the exact layout ToolLocator
    expects (<folder>\<exe> or <folder>\bin\<exe> — see tools\README.md). build.ps1 runs
    this before compiling the installer, so MediaSuite.iss can bundle tools-staged\*
    straight into {app}\tools and ship a self-contained app that needs no manual
    downloads.

    Every tool here is fetched from its own official release channel as a plain
    zip/7z archive or via a fully silent, no-EULA-prompt installer flag (7-Zip only, to
    harvest 7z.exe itself) — nothing interactive, so this runs unattended in CI. Tools
    that only ship as a GUI/MSI installer with no portable build (Ghostscript, MuPDF,
    libvips, rsvg-convert, LibreOffice, Calibre) aren't covered yet, and neither is
    LibRaw's dcraw_emu.exe — it has no official or actively-maintained prebuilt Windows
    binary at all; ImageMagick's own bundled LibRaw delegate is the practical fallback
    until that's resolved. See tools/README.md for the current per-tool status.
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
# NOT fetched via "latest" — the repo's newest release (v0.3.0) dropped the portable
# ncnn-vulkan Windows zip; v0.2.5.0 is the last (and still current/working) release that
# has it. A "latest" lookup here would need re-verifying by hand every time upstream
# tags a new release, same trap this pin avoids: confirmed by fetching the actual
# releases page rather than assuming "latest" means "has every asset every older
# release had."
$realesrganArchive = Get-ToArchive -Url "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip"
$realesrganExtract = Expand-ToTemp -ArchivePath $realesrganArchive
Copy-BinariesToStage -ExtractDir $realesrganExtract -Folder "realesrgan" -PrimaryExeNames @("realesrgan-ncnn-vulkan.exe")

Write-Host "== ImageMagick =="
# Ships as .7z, not .zip — Expand-Archive can't open that, so this uses the 7z.exe we
# just staged above rather than assuming the runner happens to have 7-Zip preinstalled.
$imageMagickArchive = Get-ToArchive -Url "https://github.com/ImageMagick/ImageMagick/releases/download/7.1.2-30/ImageMagick-7.1.2-30-portable-Q16-x64.7z"
$imageMagickExtract = Join-Path ([System.IO.Path]::GetTempPath()) "mediasuite-extract-$([Guid]::NewGuid())"
New-Item -ItemType Directory -Force -Path $imageMagickExtract | Out-Null
$sevenZipExe = Join-Path $sevenZipStage "7z.exe"
& $sevenZipExe "x" $imageMagickArchive "-o$imageMagickExtract" "-y" | Out-Null
if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
    throw "Extracting ImageMagick's .7z archive failed with exit code $LASTEXITCODE"
}
Remove-Item $imageMagickArchive -Force
Copy-BinariesToStage -ExtractDir $imageMagickExtract -Folder "imagemagick" -PrimaryExeNames @("magick.exe")

Write-Host "== Potrace =="
$potraceArchive = Get-ToArchive -Url "https://potrace.sourceforge.net/download/1.16/potrace-1.16.win64.zip"
$potraceExtract = Expand-ToTemp -ArchivePath $potraceArchive
Copy-BinariesToStage -ExtractDir $potraceExtract -Folder "potrace" -PrimaryExeNames @("potrace.exe")

Write-Host "== libvips =="
# Windows builds live in a separate repo from libvips/libvips itself.
$libvipsUrl = Get-LatestReleaseAssetUrl -Repo "libvips/build-win64-mxe" -NamePattern "vips-dev-x64-web-.*\.zip$"
$libvipsArchive = Get-ToArchive -Url $libvipsUrl
$libvipsExtract = Expand-ToTemp -ArchivePath $libvipsArchive
Copy-BinariesToStage -ExtractDir $libvipsExtract -Folder "libvips" -PrimaryExeNames @("vipsthumbnail.exe")

Write-Host "== LibRaw (dcraw_emu, compiled from source) =="
# No official or actively-maintained third-party prebuilt Windows binary exists for
# dcraw_emu.exe (checked directly — LibRaw's own GitHub releases are source-only). But
# windows-latest CI does have a real C++ toolchain and a preinstalled vcpkg, so this
# compiles it for real: vcpkg builds the libraw library itself, then dcraw_emu.cpp — a
# genuinely self-contained LibRaw sample verified against its own #include list, no
# dependency on any other file in LibRaw's samples directory — is compiled directly
# against it with cl.exe. This is meaningfully more speculative than every fetch above
# (a from-source compile, not a known-good download), so it is written to fail soft:
# if the MSVC dev environment isn't on PATH (see the CI workflow's own "Set up MSVC dev
# environment" step, which must run first) or the compile itself fails, this section
# warns and moves on rather than taking the whole fetch down — ImageMagick's own bundled
# LibRaw delegate remains the fallback for RAW files either way.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Warning "cl.exe not on PATH — skipping the LibRaw compile. tools-staged\libraw will not exist this run."
}
elseif (-not $env:VCPKG_INSTALLATION_ROOT) {
    Write-Warning "VCPKG_INSTALLATION_ROOT is not set — skipping the LibRaw compile."
}
else {
    try {
        $vcpkgExe = Join-Path $env:VCPKG_INSTALLATION_ROOT "vcpkg.exe"
        & $vcpkgExe install "libraw:x64-windows-static"
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            throw "vcpkg install libraw:x64-windows-static failed with exit code $LASTEXITCODE"
        }

        $installedDir = Join-Path $env:VCPKG_INSTALLATION_ROOT "installed\x64-windows-static"
        $libDir = Join-Path $installedDir "lib"
        $includeDir = Join-Path $installedDir "include"
        $libFiles = Get-ChildItem -Path $libDir -Filter "*.lib" -ErrorAction Stop
        if (-not $libFiles) {
            throw "No .lib files found under $libDir after vcpkg install."
        }

        $dcrawCppPath = Join-Path ([System.IO.Path]::GetTempPath()) "dcraw_emu.cpp"
        Invoke-WebRequest `
            -Uri "https://raw.githubusercontent.com/LibRaw/LibRaw/0.21.4/samples/dcraw_emu.cpp" `
            -OutFile $dcrawCppPath -Headers $webHeaders -UseBasicParsing

        $librawStage = Join-Path $ToolsDir "libraw"
        New-Item -ItemType Directory -Force -Path $librawStage | Out-Null
        $exePath = Join-Path $librawStage "dcraw_emu.exe"

        # /MT to match vcpkg's x64-windows-static triplet (static CRT) — a /MD-compiled
        # object linking against /MT-built static libs is a hard link error, not a warning.
        # Linking every .lib vcpkg produced for this triplet rather than guessing libraw's
        # exact transitive dependency list (jpeg, zlib, lcms2, ...) by name.
        & cl.exe /nologo /EHsc /O2 /MT "/I$includeDir" $dcrawCppPath "/Fe:$exePath" /link $libFiles.FullName
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            throw "Compiling dcraw_emu.cpp failed with exit code $LASTEXITCODE"
        }
        if (-not (Test-Path $exePath)) {
            throw "cl.exe reported success but $exePath does not exist."
        }
        Write-Host "  compiled libraw -> $librawStage (dcraw_emu.exe)"
    }
    catch {
        Write-Warning "LibRaw compile failed, continuing without it: $_"
    }
}

Write-Host ""
Write-Host "Tools staged under $ToolsDir :"
Get-ChildItem $ToolsDir -Directory | ForEach-Object { Write-Host "  $($_.Name)" }
