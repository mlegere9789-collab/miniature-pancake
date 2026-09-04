#Requires -Version 5.1
<#
    Downloads real Windows binaries for the third-party tools MediaSuite shells out to,
    and stages them into ..\tools-staged\<folder>\ in the exact layout ToolLocator
    expects (<folder>\<exe> or <folder>\bin\<exe> — see tools\README.md). build.ps1 runs
    this before compiling the installer, so MediaSuite.iss can bundle tools-staged\*
    straight into {app}\tools and ship a self-contained app that needs no manual
    downloads.

    Every tool here is fetched from its own official release channel — a plain zip/7z
    archive, a fully silent no-EULA-prompt installer flag (7-Zip, Calibre via
    Chocolatey), a silent MSI install harvested afterward (LibreOffice), an installer's
    payload extracted directly without running it (Ghostscript), or — for LibRaw and
    Face Enhance/GFPGAN, which have no official prebuilt Windows binary at all —
    compiled from source against vcpkg with the MSVC toolchain the CI workflow sets up.
    Nothing interactive, so this runs unattended in CI. The less certain fetches
    (anything not a known-good direct download — see tools/README.md for exactly which
    and why) fail soft rather than take the rest of the script down; the verification
    pass at the end of this script reports exactly what did and didn't make it into
    tools-staged\, distinguishing a real optional gap from a tool the app cannot do its
    core work without.
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

Write-Host "== Face Enhance (GFPGAN-ncnn, compiled from source) =="
# Same situation as LibRaw above, one step further out: no official or actively
# maintained prebuilt Windows binary exists anywhere for a GFPGAN face-restoration
# pipeline over ncnn. installer/native/face-enhance/ vendors real, working source for one
# (see its own README.md for exactly where from and why) — this compiles it for real
# with vcpkg-built opencv4 and ncnn, the same cl.exe-against-vcpkg-libs pattern as the
# LibRaw compile just above, just with more source files and two vcpkg packages instead
# of one. This is more speculative still than that LibRaw compile: opencv4 and ncnn are
# vcpkg ports this script has never exercised in CI before, so their exact port names or
# build success are confirmed here for the first time, not assumed. Written to fail soft
# for the same reason — the AI Photo Upscaler works exactly as it did before without
# this, faceEnhance just has nothing to run.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Warning "cl.exe not on PATH — skipping the Face Enhance compile."
}
elseif (-not $env:VCPKG_INSTALLATION_ROOT) {
    Write-Warning "VCPKG_INSTALLATION_ROOT is not set — skipping the Face Enhance compile."
}
else {
    try {
        $vcpkgExe = Join-Path $env:VCPKG_INSTALLATION_ROOT "vcpkg.exe"
        # opencv4's vcpkg port default-features go far past what this build actually needs:
        # dnn (pulls in protobuf + flatbuffers, ~25 min alone), quirc, tiff, webp, gapi, and
        # the Windows GUI backends (directml/dshow/msmf/win32ui) are all on by default and
        # were the actual cause of a 45+ minute run that still hadn't reached opencv4
        # itself. "core" as the first bracketed feature turns off every default feature;
        # jpeg/png are added back for imgcodecs (face_enhance.cpp's own imread/imwrite), and
        # calib3d/highgui are added back because face.h — vendored verbatim, see its own
        # header comment — hard-#includes both (a first attempt at scoping this dropped
        # them too, which compiled vcpkg's side fine but failed cl.exe with "Cannot open
        # include file: 'opencv2/opencv.hpp'" since that header's own transitive includes
        # need those two modules present).
        & $vcpkgExe install "opencv4[core,jpeg,png,calib3d,highgui]:x64-windows-static" "ncnn:x64-windows-static"
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            throw "vcpkg install opencv4/ncnn failed with exit code $LASTEXITCODE"
        }

        $installedDir = Join-Path $env:VCPKG_INSTALLATION_ROOT "installed\x64-windows-static"
        $libDir = Join-Path $installedDir "lib"
        $includeDir = Join-Path $installedDir "include"
        $libFiles = Get-ChildItem -Path $libDir -Filter "*.lib" -ErrorAction Stop
        if (-not $libFiles) {
            throw "No .lib files found under $libDir after vcpkg install."
        }

        $faceEnhanceSrcDir = Join-Path $PSScriptRoot "native\face-enhance"
        $faceEnhanceStage = Join-Path $ToolsDir "gfpgan"
        New-Item -ItemType Directory -Force -Path $faceEnhanceStage | Out-Null
        $exePath = Join-Path $faceEnhanceStage "face_enhance.exe"

        $sources = @("face_enhance.cpp", "face.cpp", "gfpgan.cpp") | ForEach-Object { Join-Path $faceEnhanceSrcDir $_ }

        # All four sources #include bare "net.h"/"opencv2/..." (no "ncnn/" or package
        # prefix), matching each library's standard install layout — ncnn under
        # include\ncnn\*.h, and OpenCV's opencv2\ either directly under include\ or under
        # include\opencv4\opencv2\ depending on vcpkg version/config (both are covered
        # below), so each candidate directory is added to the include path only if vcpkg
        # actually produced it.
        $includePaths = @("/I$includeDir")
        $ncnnIncludeDir = Join-Path $includeDir "ncnn"
        if (Test-Path $ncnnIncludeDir) {
            $includePaths += "/I$ncnnIncludeDir"
        }
        # A second attempt still couldn't resolve opencv2/core.hpp directly under include\
        # despite vcpkg reporting a clean opencv4 build with no errors — covering the other
        # real possibility, a versioned include\opencv4\opencv2\ layout (the convention some
        # OpenCV packaging uses to avoid clashing with a co-installed opencv2/opencv3), so
        # this run can actually succeed if that's what's really there instead of only
        # gathering diagnostics for a fourth attempt.
        $opencv4IncludeDir = Join-Path $includeDir "opencv4"
        if (Test-Path $opencv4IncludeDir) {
            $includePaths += "/I$opencv4IncludeDir"
        }
        # Logged plainly so a future compile failure is diagnosable straight from the CI
        # log, rather than needing to reconstruct these from vcpkg's own install output —
        # exactly what the first attempt at this compile was missing. A second attempt
        # still failed on "Cannot open include file: 'opencv2/core.hpp'" even though vcpkg
        # reported building opencv4 cleanly with no errors — so this round also dumps the
        # real installed\...\include layout instead of guessing at it a third time: either
        # opencv2\ sits somewhere other than directly under include\ (e.g. an
        # include\opencv4\opencv2\ versioned layout), or opencv4's own libs/headers never
        # actually landed under $installedDir despite vcpkg's own success report.
        Write-Host "  VCPKG_INSTALLATION_ROOT: $env:VCPKG_INSTALLATION_ROOT"
        Write-Host "  installedDir: $installedDir (exists: $(Test-Path $installedDir))"
        Write-Host "  includeDir: $includeDir (exists: $(Test-Path $includeDir))"
        if (Test-Path $includeDir) {
            $topLevel = Get-ChildItem -Path $includeDir | Select-Object -ExpandProperty Name
            Write-Host "  includeDir top-level entries: $($topLevel -join ', ')"
            foreach ($dir in @('opencv2', 'opencv4')) {
                $candidate = Join-Path $includeDir $dir
                if (Test-Path $candidate) {
                    $nested = Get-ChildItem -Path $candidate | Select-Object -ExpandProperty Name
                    Write-Host "  includeDir\$dir entries: $($nested -join ', ')"
                }
            }
        }
        $opencvLibs = $libFiles | Where-Object { $_.Name -like '*opencv*' } | Select-Object -ExpandProperty Name
        Write-Host "  opencv-related .lib files found ($($opencvLibs.Count)): $($opencvLibs -join ', ')"
        Write-Host "  opencv2/core.hpp present: $(Test-Path (Join-Path $includeDir 'opencv2\core.hpp'))"
        Write-Host "  opencv2/highgui/highgui.hpp present: $(Test-Path (Join-Path $includeDir 'opencv2\highgui\highgui.hpp'))"
        Write-Host "  opencv4/opencv2/core.hpp present: $(Test-Path (Join-Path $includeDir 'opencv4\opencv2\core.hpp'))"

        # /MT to match vcpkg's x64-windows-static triplet (static CRT), same reasoning as
        # the LibRaw compile above. Linking every .lib vcpkg produced for this triplet
        # rather than guessing opencv4's and ncnn's combined transitive dependency list
        # (protobuf, zlib, libpng, libjpeg-turbo, ...) by name. /DNOMINMAX because face.cpp
        # calls std::max/std::min (e.g. "std::max(std::min(x0, ...), 0.f)"), and something
        # in the ncnn/opencv4 header chain pulls in <windows.h> transitively, whose own
        # max/min macros (on by default) rewrite "std::max(" into invalid syntax right
        # after "::" — a well-known, well-documented MSVC/Windows.h gotcha, fixed by this
        # one flag rather than touching the vendored source at all.
        Write-Host "  cl.exe /nologo /EHsc /O2 /MT /DNOMINMAX $($includePaths -join ' ') $($sources -join ' ') /Fe:$exePath /link <$($libFiles.Count) .lib files>"
        & cl.exe /nologo /EHsc /O2 /MT /DNOMINMAX $includePaths $sources "/Fe:$exePath" /link $libFiles.FullName
        if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
            throw "Compiling face_enhance.cpp failed with exit code $LASTEXITCODE"
        }
        if (-not (Test-Path $exePath)) {
            throw "cl.exe reported success but $exePath does not exist."
        }
        Write-Host "  compiled face-enhance -> $faceEnhanceStage (face_enhance.exe)"

        # Model weights: same repository the source came from (see the README.md next to
        # it), too large (>100 MB) for a GitHub release so hosted on Google Drive instead.
        # A file this size trips Google Drive's "can't scan for viruses" interstitial page
        # rather than streaming directly; drive.usercontent.google.com's download endpoint
        # with confirm=t bypasses that for an anonymous request without needing to scrape
        # a per-request confirm token out of a cookie the way the classic
        # drive.google.com/uc endpoint requires.
        $modelsZip = Join-Path ([System.IO.Path]::GetTempPath()) "mediasuite-dl-$([Guid]::NewGuid())-gfpgan-models.zip"
        $modelsFileId = "1Lfs2fBU1ecaIKiQtMTaZW4q099PgPpA9"
        # TimeoutSec is explicit here (unlike the rest of this script's downloads): the
        # workflow-level step timeout would eventually catch a hang too, but that burns up
        # to 45 minutes of billed CI time to find out. This fails fast and specifically at
        # the one call in the whole script most likely to actually hang.
        Invoke-WebRequest `
            -Uri "https://drive.usercontent.google.com/download?id=$modelsFileId&export=download&confirm=t" `
            -OutFile $modelsZip -Headers $webHeaders -UseBasicParsing -TimeoutSec 300

        $modelsBytes = [System.IO.File]::ReadAllBytes($modelsZip)
        $looksLikeZip = $modelsBytes.Length -gt 2 -and $modelsBytes[0] -eq 0x50 -and $modelsBytes[1] -eq 0x4B
        if (-not $looksLikeZip) {
            $previewLength = [Math]::Min(200, $modelsBytes.Length)
            $preview = [System.Text.Encoding]::ASCII.GetString($modelsBytes[0..($previewLength - 1)])
            Remove-Item $modelsZip -Force
            throw "Google Drive did not return a zip file for the GFPGAN models ($($modelsBytes.Length) bytes). Response started with: $preview"
        }

        $modelsExtract = Expand-ToTemp -ArchivePath $modelsZip
        $modelsStage = Join-Path $faceEnhanceStage "models"
        New-Item -ItemType Directory -Force -Path $modelsStage | Out-Null

        # Only the face detector and GFPGAN itself — not the archive's own real_esrgan
        # model pair, which face_enhance.exe never loads: it restores faces onto whatever
        # background image it is handed, already upscaled by this app's own Real-ESRGAN
        # pass before face_enhance.exe ever runs.
        $neededModels = @("yolov5-blazeface.param", "yolov5-blazeface.bin", "encoder.param", "encoder.bin", "style.bin")
        foreach ($modelFile in $neededModels) {
            $found = Get-ChildItem -Path $modelsExtract -Recurse -File -Filter $modelFile | Select-Object -First 1
            if (-not $found) {
                throw "Could not find $modelFile anywhere under $modelsExtract after extracting the GFPGAN models archive."
            }
            Copy-Item -Path $found.FullName -Destination (Join-Path $modelsStage $modelFile) -Force
        }
        Remove-Item $modelsExtract -Recurse -Force
        Write-Host "  staged gfpgan models -> $modelsStage"
    }
    catch {
        Write-Warning "Face Enhance build failed, continuing without it: $_"
    }
}

Write-Host "== Ghostscript =="
# Ghostscript only ships a GUI installer, and Artifex deliberately removed its silent
# install flag in 10.01.0+ as a security decision — /S today just launches the GUI
# instead of installing quietly. The installer itself is still a plain archive under the
# hood (7-Zip's NSIS/Inno codecs can open it directly), so this extracts its payload with
# the 7z.exe already staged above rather than running it as an installer at all. This is
# unofficial — Artifex never blessed extracting the exe this way — so it fails soft like
# LibRaw rather than taking the rest of the fetch down if a future installer format
# change breaks it.
try {
    $ghostscriptArchive = Get-ToArchive -Url "https://github.com/ArtifexSoftware/ghostpdl-downloads/releases/download/gs10071/gs10071w64.exe"
    $ghostscriptExtract = Join-Path ([System.IO.Path]::GetTempPath()) "mediasuite-extract-$([Guid]::NewGuid())"
    New-Item -ItemType Directory -Force -Path $ghostscriptExtract | Out-Null
    & $sevenZipExe "x" $ghostscriptArchive "-o$ghostscriptExtract" "-y" | Out-Null
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
        throw "Extracting Ghostscript's installer payload failed with exit code $LASTEXITCODE"
    }
    Remove-Item $ghostscriptArchive -Force
    Copy-BinariesToStage -ExtractDir $ghostscriptExtract -Folder "ghostscript" -PrimaryExeNames @("gswin64c.exe")
}
catch {
    Write-Warning "Ghostscript extraction failed, continuing without it: $_"
}

Write-Host "== MuPDF =="
# mupdf.com is unreachable from the environment this script was written in, so the exact
# current version at mupdf.com/downloads/archive/ could only be confirmed via web search,
# not a direct HTTP check. The first attempt at this (1.27.2, the version a search engine
# summary called "current stable") 404'd in real CI — that filename evidently doesn't
# exist. 1.26.2 is pinned instead because a search actually surfaced that exact filename
# being referenced elsewhere (a malware-scan mirror site's page title), which only
# happens for a file that was actually seen published, not an inferred "latest" guess.
# Still fails soft, since even this is one step short of a direct HTTP confirmation.
try {
    $mupdfArchive = Get-ToArchive -Url "https://mupdf.com/downloads/archive/mupdf-1.26.2-windows.zip"
    $mupdfExtract = Expand-ToTemp -ArchivePath $mupdfArchive
    Copy-BinariesToStage -ExtractDir $mupdfExtract -Folder "mupdf" -PrimaryExeNames @("mutool.exe")
}
catch {
    Write-Warning "MuPDF fetch failed, continuing without it: $_"
}

Write-Host "== rsvg-convert =="
# librsvg has no official standalone Windows build; wingtk/gvsbuild (GNOME's own
# Windows/GTK build team) publishes a full GTK4 bundle that includes librsvg as a
# dependency, and a librsvg build normally produces rsvg-convert.exe as an ordinary CLI
# tool alongside the library — but that inclusion was not directly confirmed (could not
# browse the zip's contents from the environment this was written in), only inferred.
# Fails soft for that reason; Copy-BinariesToStage already throws cleanly if
# rsvg-convert.exe isn't actually in there, which the catch below turns into a warning
# instead of a broken build.
try {
    $rsvgUrl = Get-LatestReleaseAssetUrl -Repo "wingtk/gvsbuild" -NamePattern "GTK4_Gvsbuild_.*_x64\.zip$"
    $rsvgArchive = Get-ToArchive -Url $rsvgUrl
    $rsvgExtract = Expand-ToTemp -ArchivePath $rsvgArchive
    Copy-BinariesToStage -ExtractDir $rsvgExtract -Folder "rsvg" -PrimaryExeNames @("rsvg-convert.exe")
}
catch {
    Write-Warning "rsvg-convert fetch failed, continuing without it: $_"
}

Write-Host "== LibreOffice =="
# LibreOffice only ships as a full MSI installer — no portable build. This installs it
# silently (into this ephemeral CI machine, which gets discarded after the job either
# way) and harvests the resulting program\ folder rather than trying to extract the MSI
# without installing it. www.libreoffice.org and documentfoundation.org are both
# unreachable from the environment this was written in, so the version pinned below is a
# best guess from web search, not a direct HTTP check — meaningfully more likely to be
# stale than most fetches above, on top of this being the largest, slowest step in the
# whole script (a full office suite install). Fails soft for both reasons.
try {
    $libreOfficeMsi = Get-ToArchive -Url "https://download.documentfoundation.org/libreoffice/stable/26.8.0/win/x86_64/LibreOffice_26.8.0_Win_x86-64.msi"
    $libreOfficeInstallLog = Join-Path ([System.IO.Path]::GetTempPath()) "libreoffice-install.log"
    $msiArgs = @("/i", $libreOfficeMsi, "/qn", "/norestart", "/log", $libreOfficeInstallLog)
    $msiProcess = Start-Process -FilePath "msiexec.exe" -ArgumentList $msiArgs -Wait -PassThru
    if ($msiProcess.ExitCode -ne 0) {
        throw "msiexec install of LibreOffice failed with exit code $($msiProcess.ExitCode) — see $libreOfficeInstallLog"
    }
    Remove-Item $libreOfficeMsi -Force

    $installedProgramDir = "C:\Program Files\LibreOffice\program"
    if (-not (Test-Path (Join-Path $installedProgramDir "soffice.exe"))) {
        throw "msiexec reported success but soffice.exe is not at $installedProgramDir."
    }
    $libreOfficeStage = Join-Path $ToolsDir "libreoffice"
    New-Item -ItemType Directory -Force -Path $libreOfficeStage | Out-Null
    Copy-Item -Path (Join-Path $installedProgramDir "*") -Destination $libreOfficeStage -Recurse -Force
    Write-Host "  staged libreoffice -> $libreOfficeStage (soffice.exe)"
}
catch {
    Write-Warning "LibreOffice install failed, continuing without it: $_"
}

Write-Host "== Calibre =="
# calibre-ebook.com is unreachable from the environment this was written in, and its
# "portable" installer's exact command-line behavior (does passing a path argument alone
# really suppress every prompt, silently?) could not be confirmed directly — the weakest
# link in every other approach considered for this tool. Chocolatey sidesteps all of that:
# it's already proven working in this exact pipeline (the "Install Inno Setup" CI step
# uses it), and its Calibre package wraps the same silent-install problem with
# already-solved, community-maintained flags. This installs via choco and then searches
# Program Files for the result rather than hardcoding "Calibre2" vs "calibre" — Calibre's
# default folder name has changed between versions historically.
try {
    choco install calibre -y --no-progress | Out-Null
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) {
        throw "choco install calibre failed with exit code $LASTEXITCODE"
    }
    $ebookConvert = Get-ChildItem -Path "C:\Program Files" -Recurse -File -Filter "ebook-convert.exe" -ErrorAction Stop | Select-Object -First 1
    if (-not $ebookConvert) {
        throw "choco install calibre reported success but ebook-convert.exe was not found under C:\Program Files."
    }
    $calibreStage = Join-Path $ToolsDir "calibre"
    New-Item -ItemType Directory -Force -Path $calibreStage | Out-Null
    Copy-Item -Path (Join-Path $ebookConvert.DirectoryName "*") -Destination $calibreStage -Recurse -Force
    Write-Host "  staged calibre -> $calibreStage (ebook-convert.exe)"
}
catch {
    Write-Warning "Calibre install failed, continuing without it: $_"
}

Write-Host ""
Write-Host "== Verification =="
# Every fail-soft block above (LibRaw, Ghostscript, MuPDF, rsvg, LibreOffice, Calibre,
# Face Enhance) creates its stage folder before it has actually confirmed the real exe
# landed inside — Face Enhance's own three-round debugging session (see git history on
# this file) was exactly this: a real cl.exe failure caught by try/catch left an empty
# "gfpgan" folder behind, Write-Warning scrolled past unnoticed in a huge log, and the
# CI *step* still reported green, because nothing ever checked the folder's contents
# against what ToolLocator/ToolManifest.cs actually expects to find in it. Listing
# directory names (the old version of this summary) cannot catch that: an empty folder
# and a correctly staged one look identical to `Get-ChildItem -Directory`. This checks
# the real file instead, for every tool this script attempts, printed here as one clear
# table instead of a warning buried mid-log next to a dozen other Write-Host lines.
#
# Keep this table's folder/exe pairs in sync with src/MediaSuite.Core/Tooling/ToolManifest.cs
# by hand — there is no automated link between a PowerShell script and a C# assembly for
# a project this size, the same tradeoff already noted in tools/README.md.
#
# Gate is deliberately NOT just "IsRequired from ToolManifest.cs copied verbatim" — that
# flag means "the app shows a warning banner without this," a different question from
# "should CI fail the whole installer build over it missing." Three tiers instead:
#   hard-fail  fetched with no try/catch above (ffmpeg/ffprobe/imagemagick) -- if any of
#              these were actually missing, the script would already have thrown and
#              stopped well before reaching this point, so this is pure defense in depth,
#              not a new failure mode.
#   loud-warn  ToolManifest.cs marks IsRequired, but this script fetches it fail-soft on
#              purpose (LibRaw: a from-source compile with no official binary, documented
#              fallback is ImageMagick's own bundled RAW delegate) -- missing it is a real
#              problem worth a human's attention, but turning that into a hard CI failure
#              would silently contradict that existing, deliberate fail-soft design.
#   optional   genuinely fine to be missing; the feature it backs just has nothing to run.
$expectedTools = @(
    @{ Folder = "ffmpeg"; Exe = "ffmpeg.exe"; Gate = "hard-fail" }
    @{ Folder = "ffmpeg"; Exe = "ffprobe.exe"; Gate = "hard-fail" }
    @{ Folder = "imagemagick"; Exe = "magick.exe"; Gate = "hard-fail" }
    @{ Folder = "libvips"; Exe = "vipsthumbnail.exe"; Gate = "optional" }
    @{ Folder = "libraw"; Exe = "dcraw_emu.exe"; Gate = "loud-warn" }
    @{ Folder = "mupdf"; Exe = "mutool.exe"; Gate = "optional" }
    @{ Folder = "qpdf"; Exe = "qpdf.exe"; Gate = "optional" }
    @{ Folder = "ghostscript"; Exe = "gswin64c.exe"; Gate = "optional" }
    @{ Folder = "pandoc"; Exe = "pandoc.exe"; Gate = "optional" }
    @{ Folder = "libreoffice"; Exe = "soffice.exe"; Gate = "optional" }
    @{ Folder = "calibre"; Exe = "ebook-convert.exe"; Gate = "optional" }
    @{ Folder = "7zip"; Exe = "7z.exe"; Gate = "optional" }
    @{ Folder = "realesrgan"; Exe = "realesrgan-ncnn-vulkan.exe"; Gate = "optional" }
    @{ Folder = "rsvg"; Exe = "rsvg-convert.exe"; Gate = "optional" }
    @{ Folder = "potrace"; Exe = "potrace.exe"; Gate = "optional" }
    @{ Folder = "gfpgan"; Exe = "face_enhance.exe"; Gate = "optional" }
)

$missingHardFail = @()
foreach ($tool in $expectedTools) {
    $exePath = Join-Path (Join-Path $ToolsDir $tool.Folder) $tool.Exe
    $present = Test-Path $exePath
    if ($present) {
        Write-Host "  [ok]      $($tool.Folder)\$($tool.Exe) ($($tool.Gate))"
    }
    elseif ($tool.Gate -eq "hard-fail") {
        Write-Host "  [MISSING] $($tool.Folder)\$($tool.Exe) (hard-fail) <-- should have already stopped the build above; something is wrong with this check itself if it didn't"
        $missingHardFail += "$($tool.Folder)\$($tool.Exe)"
    }
    elseif ($tool.Gate -eq "loud-warn") {
        Write-Host "  [MISSING] $($tool.Folder)\$($tool.Exe) (loud-warn) <-- ToolManifest.cs marks this IsRequired; fell back / attempt failed, see the fail-soft warning above. ImageMagick's own bundled RAW delegate remains the fallback."
    }
    else {
        Write-Host "  [missing] $($tool.Folder)\$($tool.Exe) (optional) -- fell back / attempt failed, see the fail-soft warning above"
    }
}

# Every unexpected folder is also worth a look — a stale leftover, or a tool this table
# hasn't been kept in sync with.
$expectedFolders = $expectedTools | ForEach-Object { $_.Folder } | Select-Object -Unique
$unexpectedFolders = Get-ChildItem $ToolsDir -Directory | Where-Object { $expectedFolders -notcontains $_.Name }
foreach ($folder in $unexpectedFolders) {
    Write-Host "  [??]      $($folder.Name)\ exists but isn't in this script's own expected-tools table"
}

if ($missingHardFail.Count -gt 0) {
    throw "Tool(s) fetched with no fail-soft handling are still missing after the fetch: $($missingHardFail -join ', '). This should be unreachable -- the earlier fetch step for each of these throws on its own if it fails, so reaching this check with one of them missing means this verification itself has a bug, not just the fetch."
}
Write-Host ""
Write-Host "All hard-required tools staged successfully; see [ok]/[MISSING]/[missing] above for the rest."
