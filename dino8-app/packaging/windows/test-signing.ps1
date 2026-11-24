<#
.SYNOPSIS
  CI self-test of the Windows Authenticode signing code path WITHOUT any real
  certificate or secret. Used by the "windows" job of
  .github/workflows/dino8-app.yml in three steps (prepare / verify / cleanup);
  the signing itself in between is done by the real composite action
  .github/actions/sign-windows (method test-pfx), i.e. the real sign.ps1.

.DESCRIPTION
  *** TEST CERTIFICATE. NOTHING THIS SCRIPT PRODUCES IS TRUSTED-SIGNED. ***

  prepare  generates a THROWAWAY self-signed code-signing certificate in the
           job's current-user store (random password generated here, never
           stored anywhere; the PFX is read into memory, handed to the next step
           through the job environment with the secret values masked, and the
           PFX file is deleted at once), then copies the given built binaries
           into a scratch directory. ONLY those copies are signed; the files
           that get published (Dino8.exe in the install tree, the installer) are
           never touched, and verify asserts that.
  verify   after the signing step, asserts for every signed copy:
             1. an Authenticode signature is PRESENT (Get-AuthenticodeSignature
                has a SignerCertificate) and its thumbprint equals the throwaway
                certificate's;
             2. the status is UnknownError / NotTrusted (chain ends in an
                untrusted self-signed root) - NOT Valid, NotSigned or
                HashMismatch: the signature exists but nothing trusts it;
             3. the signed file's SHA-256 differs from the unsigned copy's;
             4. a TAMPERED copy (one byte flipped inside the signed range after
                signing) reports HashMismatch;
             5. the original published binary is byte-identical to what was
                built, carries no signature from the throwaway certificate and,
                when no real signing method is configured, is NotSigned.
  cleanup  removes the throwaway certificate from the store and the scratch
           directory (run with `if: always()`).

  This proves that the signing code runs and its output is checked. It does NOT
  make any installer trusted: that needs a certificate issued by a public
  authority, which this repository does not have (docs/CODE_SIGNING.md).

.PARAMETER Mode
  prepare | verify | cleanup
.PARAMETER Source
  prepare only: the built files to copy and sign (paths or wildcards).
.PARAMETER WorkDir
  Scratch directory (default $RUNNER_TEMP\dino8-signtest).
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('prepare', 'verify', 'cleanup')][string]$Mode,
  [string[]]$Source = @(),
  [string]$WorkDir = ''
)
$ErrorActionPreference = 'Stop'

$tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
if (-not $WorkDir) { $WorkDir = Join-Path $tempRoot 'dino8-signtest' }
$signedDir = Join-Path $WorkDir 'signed'
$unsignedDir = Join-Path $WorkDir 'unsigned'
$tamperDir = Join-Path $WorkDir 'tampered'
$manifestPath = Join-Path $WorkDir 'manifest.json'

function Write-Banner([string]$text) {
  Write-Host ''
  Write-Host '################################################################'
  Write-Host "# $text"
  Write-Host '################################################################'
}

function Set-JobEnv([string]$Name, [string]$Value) {
  # Visible to the later steps of this job only; the process copy covers local runs.
  [Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
  if ($env:GITHUB_ENV) { "$Name=$Value" | Out-File -FilePath $env:GITHUB_ENV -Append -Encoding utf8 }
}

function Get-Sha256([string]$Path) {
  (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# File offset of the Authenticode certificate table of a PE file (0 when the
# file is not signed). Everything before it is covered by the signature.
function Get-PeCertTableOffset([byte[]]$Bytes) {
  $pe = [BitConverter]::ToInt32($Bytes, 0x3C)
  if ([BitConverter]::ToUInt32($Bytes, $pe) -ne 0x00004550) { throw 'not a PE file (no PE signature)' }
  $magic = [BitConverter]::ToUInt16($Bytes, $pe + 24)
  if ($magic -eq 0x10b) { $dd = $pe + 24 + 96 }
  elseif ($magic -eq 0x20b) { $dd = $pe + 24 + 112 }
  else { throw ('unknown PE optional header magic 0x{0:x}' -f $magic) }
  # Data directory index 4 = security (certificate) table; unlike the others its
  # "virtual address" field is a plain file offset.
  return [int][BitConverter]::ToUInt32($Bytes, $dd + 4 * 8)
}

switch ($Mode) {
  'prepare' {
    Write-Banner 'TEST CERTIFICATE MODE - a throwaway self-signed certificate is created; nothing signed with it is trusted or published'
    Write-Host '::notice title=Authenticode self-test::Throwaway TEST certificate. The released installer is NOT trusted-signed; only scratch copies are signed to prove the signing code path runs.'

    if (-not (Get-Command New-SelfSignedCertificate -ErrorAction SilentlyContinue)) {
      # PowerShell 7 exposes the Windows PowerShell PKI module through the compatibility layer.
      Import-Module PKI -UseWindowsPowerShell -ErrorAction Stop | Out-Null
    }

    if (Test-Path -LiteralPath $WorkDir) { Remove-Item -Recurse -Force -LiteralPath $WorkDir }
    New-Item -ItemType Directory -Path $signedDir, $unsignedDir, $tamperDir | Out-Null
    # sign.ps1 -Method test-pfx refuses to sign anything outside a directory with this marker.
    Set-Content -LiteralPath (Join-Path $signedDir '.dino8-test-signing-dir') -Value 'scratch copies signed with a throwaway TEST certificate; never publish' -Encoding ascii

    $sources = @()
    foreach ($s in $Source) {
      $hit = @(Resolve-Path -Path $s -ErrorAction Stop | ForEach-Object { $_.Path })
      if ($hit.Count -eq 0) { throw "nothing matches $s" }
      $sources += $hit
    }
    if ($sources.Count -eq 0) { throw 'prepare needs at least one -Source file' }

    # Random password, generated here, never written to disk or to the log.
    $rnd = New-Object byte[] 24
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    $rng.GetBytes($rnd)
    $rng.Dispose()
    $password = [Convert]::ToBase64String($rnd)
    Write-Host "::add-mask::$password"

    $cert = New-SelfSignedCertificate -Type CodeSigningCert `
      -Subject 'CN=Dino8 TEST ONLY - NOT A TRUSTED CERTIFICATE' `
      -KeyAlgorithm RSA -KeyLength 2048 -HashAlgorithm SHA256 `
      -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(2)
    $thumb = $cert.Thumbprint
    Write-Host "Throwaway TEST certificate: $($cert.Subject)  thumbprint $thumb  (self-signed, valid 2 days, not in any trusted root store)"

    $pfx = Join-Path $tempRoot 'dino8-test-throwaway.pfx'
    try {
      Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString -String $password -AsPlainText -Force) | Out-Null
      $b64 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($pfx))
    } finally {
      if (Test-Path -LiteralPath $pfx) { Remove-Item -Force -LiteralPath $pfx }
    }
    Write-Host "::add-mask::$b64"
    Set-JobEnv 'DINO8_TEST_PFX_BASE64' $b64
    Set-JobEnv 'DINO8_TEST_PFX_PASSWORD' $password
    Set-JobEnv 'DINO8_TEST_CERT_THUMBPRINT' $thumb

    $entries = @()
    foreach ($src in $sources) {
      $name = Split-Path -Leaf $src
      if (Test-Path -LiteralPath (Join-Path $signedDir $name)) { throw "two files to test-sign share the name $name" }
      $origSig = Get-AuthenticodeSignature -LiteralPath $src
      Copy-Item -LiteralPath $src -Destination (Join-Path $signedDir $name)
      Copy-Item -LiteralPath $src -Destination (Join-Path $unsignedDir $name)
      $entries += [pscustomobject]@{
        name           = $name
        source         = $src
        sourceSha256   = Get-Sha256 $src
        unsignedSha256 = Get-Sha256 (Join-Path $unsignedDir $name)
        sourceStatus   = [string]$origSig.Status
      }
      Write-Host "staged scratch copy of $src  (original authenticode status: $($origSig.Status))"
    }
    [pscustomobject]@{ thumbprint = $thumb; entries = $entries } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Write-Host "scratch directory to sign: $signedDir"
  }

  'verify' {
    Write-Banner 'TEST CERTIFICATE CHECKS - these binaries are signed with a throwaway certificate and are NOT trusted-signed'
    $m = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $thumb = $m.thumbprint
    $failures = New-Object System.Collections.Generic.List[string]
    $summary = New-Object System.Collections.Generic.List[string]
    function Assert-That([bool]$Condition, [string]$Pass, [string]$Fail) {
      if ($Condition) { Write-Host "  PASS  $Pass" }
      else { Write-Host "  FAIL  $Fail"; $failures.Add($Fail) }
    }
    Assert-That ($env:DINO8_TEST_CERT_THUMBPRINT -eq $thumb) 'job environment names the same throwaway thumbprint as the manifest' 'DINO8_TEST_CERT_THUMBPRINT does not match the manifest'

    foreach ($e in @($m.entries)) {
      $name = $e.name
      Write-Host ''
      Write-Host "== $name"
      $signed = Join-Path $signedDir $name
      $unsigned = Join-Path $unsignedDir $name
      $sig = Get-AuthenticodeSignature -LiteralPath $signed
      Write-Host "  signed copy: status=$($sig.Status)  message: $($sig.StatusMessage)"

      # 1. signature PRESENT and made by the throwaway certificate
      $hasSig = [bool]$sig.SignerCertificate
      Assert-That $hasSig "$name carries an Authenticode signature" "$name has NO Authenticode signature after test signing (status $($sig.Status))"
      if ($hasSig) {
        Assert-That ($sig.SignerCertificate.Thumbprint -eq $thumb) "$name signature thumbprint equals the throwaway certificate ($thumb)" "$name was signed by $($sig.SignerCertificate.Thumbprint), not by the throwaway certificate $thumb"
      }
      # 2. present but untrusted
      $untrusted = @('UnknownError', 'NotTrusted') -contains [string]$sig.Status
      Assert-That $untrusted "$name status is $($sig.Status): signature present but NOT trusted (self-signed root), as expected" "$name status is '$($sig.Status)', expected UnknownError/NotTrusted (a self-signed test certificate must neither be Valid nor missing nor HashMismatch)"
      # 3. bytes changed by signing
      $hSigned = Get-Sha256 $signed
      Assert-That ($hSigned -ne $e.unsignedSha256) "$name signed hash differs from the unsigned hash" "$name signed hash equals the unsigned hash - nothing was signed"
      Assert-That ((Get-Sha256 $unsigned) -eq $e.unsignedSha256) "$name unsigned reference copy untouched" "$name unsigned reference copy changed"

      # 4. tamper: flip one byte inside the signed range, expect HashMismatch
      $bytes = [IO.File]::ReadAllBytes($signed)
      $certOff = Get-PeCertTableOffset $bytes
      $pos = [int][Math]::Floor($certOff / 2)
      Assert-That ($certOff -gt 8192 -and $pos -gt 4096) "$name certificate table at file offset $certOff; flipping byte $pos (inside the signed range)" "${name}: cannot choose a tamper offset (certificate table offset $certOff)"
      $tStatus = 'not-run'
      if ($certOff -gt 8192 -and $pos -gt 4096) {
        $bytes[$pos] = [byte]($bytes[$pos] -bxor 0xFF)
        $tampered = Join-Path $tamperDir $name
        [IO.File]::WriteAllBytes($tampered, $bytes)
        $tSig = Get-AuthenticodeSignature -LiteralPath $tampered
        $tStatus = [string]$tSig.Status
        Write-Host "  tampered copy: status=$tStatus  message: $($tSig.StatusMessage)"
        Assert-That ((Get-Sha256 $tampered) -ne $hSigned) "$name tampered copy differs from the signed copy" "$name tampered copy is identical to the signed copy"
        Assert-That ($tStatus -eq 'HashMismatch') "$name tampered copy reports HashMismatch" "$name tampered copy reports '$tStatus', expected HashMismatch"
      }

      # 5. the file that gets published is untouched and unsigned by the test certificate
      $src = $e.source
      Assert-That ((Get-Sha256 $src) -eq $e.sourceSha256) "$name original ($src) is byte-identical to what was built" "$name original ($src) was modified by the self-test"
      $oSig = Get-AuthenticodeSignature -LiteralPath $src
      $oThumb = if ($oSig.SignerCertificate) { $oSig.SignerCertificate.Thumbprint } else { '' }
      Assert-That ($oThumb -ne $thumb) "$name original is not signed by the throwaway certificate" "$name ORIGINAL IS SIGNED BY THE THROWAWAY TEST CERTIFICATE"
      if ($env:WINDOWS_SIGN_METHOD -eq 'none' -or -not $env:WINDOWS_SIGN_METHOD) {
        Assert-That ([string]$oSig.Status -eq 'NotSigned') "$name original stays unsigned (NotSigned) - no real signing method is configured" "$name original reports '$($oSig.Status)', expected NotSigned with no signing method configured"
      } else {
        Write-Host "  note: real signing method '$($env:WINDOWS_SIGN_METHOD)' is configured; original status $($oSig.Status) is the production path's business"
      }
      $summary.Add("| $name | $($sig.Status) | $(if ($hasSig) { 'yes' } else { 'NO' }) | $tStatus | $($oSig.Status) |")
    }

    if ($env:GITHUB_STEP_SUMMARY) {
      $md = @('### Authenticode self-test (throwaway TEST certificate, NOT trusted)', '',
        'The signing code path (`sign-windows` action -> `sign.ps1`) ran against scratch copies with a self-signed certificate generated in this job. The published installer is **not** trusted-signed.', '',
        '| file | signed-copy status | signature present | tampered-copy status | published original status |', '|---|---|---|---|---|') + $summary
      $md | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
    }
    Write-Host ''
    if ($failures.Count -gt 0) {
      Write-Host "::error title=Authenticode self-test::$($failures.Count) check(s) failed"
      throw "Authenticode self-test failed:`n - $($failures -join "`n - ")"
    }
    Write-Host 'Authenticode self-test passed: the signing code path ran and was checked (TEST certificate, NOT trusted; the installer to be published is unsigned).'
  }

  'cleanup' {
    $thumb = $env:DINO8_TEST_CERT_THUMBPRINT
    if ($thumb) {
      Get-ChildItem -Path 'Cert:\CurrentUser\My' | Where-Object { $_.Thumbprint -eq $thumb } | Remove-Item -Force -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $WorkDir) { Remove-Item -Recurse -Force -LiteralPath $WorkDir -ErrorAction SilentlyContinue }
    $stray = Join-Path $tempRoot 'dino8-test-throwaway.pfx'
    if (Test-Path -LiteralPath $stray) { Remove-Item -Force -LiteralPath $stray }
    Write-Host 'throwaway TEST certificate and scratch copies removed'
  }
}
