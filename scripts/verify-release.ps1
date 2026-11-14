<#
.SYNOPSIS
  Verify a downloaded Dino 8 release artifact (Windows twin of verify-release.sh).

.DESCRIPTION
  For every installer/package (.exe .zip .deb .tar.gz .dmg) given (or found in
  a given folder) this checks, in order:

    1. SHA-256   the file matches <file>.sha256 and/or SHA256SUMS next to it.
                 Proves the bytes are the bytes that were published. On its own
                 it does NOT prove who published them.
    2. cosign    <file>.sigstore.json is a valid keyless Sigstore signature made
                 by this repository's dino8-app.yml workflow run through
                 GitHub's OIDC identity. Proves the file came out of that
                 workflow and was not altered afterwards. It does NOT prove the
                 source is safe or that a real-world legal entity stands behind
                 it.
    3. Authenticode (.exe) asks Windows to validate the file's code-signing
                 signature and certificate chain. An unsigned .exe is reported
                 as informational (expected until a certificate is configured)
                 unless -RequireAuthenticode is given.
    4. attestation (only with -Attestation) `gh attestation verify`.

  Fails closed: a missing checksum, bundle or cosign binary is a FAILURE unless
  you pass -SkipCosign.

.PARAMETER Path
  Files and/or folders. Defaults to the current folder.
.PARAMETER Repo
  OWNER/NAME of the repository whose workflow must have signed the file.
  Default: $env:DINO8_REPO or mlegere9789-collab/miniature-pancake.
.PARAMETER IdentityRegexp
  Override the whole certificate-identity regexp (e.g. pin to refs/heads/main).
.PARAMETER SkipCosign
  Check checksums and Authenticode only.
.PARAMETER Attestation
  Also run gh attestation verify.
.PARAMETER RequireAuthenticode
  Treat an unsigned .exe as a failure.

.NOTES
  Exit code: 0 all requested checks passed, 1 at least one failed, 2 usage.
#>
[CmdletBinding()]
param(
  [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
  [string[]]$Path = @('.'),
  [string]$Repo = $(if ($env:DINO8_REPO) { $env:DINO8_REPO } else { 'mlegere9789-collab/miniature-pancake' }),
  [string]$IdentityRegexp = '',
  [switch]$SkipCosign,
  [switch]$Attestation,
  [switch]$RequireAuthenticode
)

$ErrorActionPreference = 'Stop'
$issuer = 'https://token.actions.githubusercontent.com'
if (-not $IdentityRegexp) {
  $IdentityRegexp = '^https://github\.com/' + [regex]::Escape($Repo) + '/\.github/workflows/dino8-app\.yml@refs/(heads|tags)/.+$'
}

$script:nOk = 0; $script:nFail = 0; $script:nInfo = 0
function Write-Ok($m)   { Write-Host '  [ OK ] ' -ForegroundColor Green -NoNewline; Write-Host $m; $script:nOk++ }
function Write-Bad($m)  { Write-Host '  [FAIL] ' -ForegroundColor Red -NoNewline; Write-Host $m; $script:nFail++ }
function Write-Note($m) { Write-Host '  [INFO] ' -ForegroundColor Yellow -NoNewline; Write-Host $m; $script:nInfo++ }

# Expected hash for $Name from a "<hash>  <name>" / "<hash> *<name>" file.
function Get-Expected([string]$Name, [string]$SumFile) {
  foreach ($line in Get-Content -LiteralPath $SumFile) {
    if ($line -match '^\s*([0-9A-Fa-f]{64})\s+\*?(.+?)\s*$' -and $Matches[2] -eq $Name) {
      return $Matches[1].ToLowerInvariant()
    }
  }
  return $null
}

function Test-Checksums([string]$File) {
  $dir = Split-Path -Parent $File
  $name = Split-Path -Leaf $File
  $actual = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()
  $found = $false
  $side = "$File.sha256"
  if (Test-Path -LiteralPath $side) {
    $found = $true
    $exp = Get-Expected $name $side
    if (-not $exp) { Write-Bad "$name.sha256 does not list $name" }
    elseif ($exp -eq $actual) { Write-Ok "SHA-256 matches $name.sha256 ($actual)" }
    else { Write-Bad "SHA-256 MISMATCH vs $name.sha256: expected $exp, file is $actual" }
  }
  $sums = Join-Path $dir 'SHA256SUMS'
  if (Test-Path -LiteralPath $sums) {
    $exp = Get-Expected $name $sums
    if (-not $exp) {
      if (-not $found) { Write-Bad "SHA256SUMS does not list $name" }
    } else {
      $found = $true
      if ($exp -eq $actual) { Write-Ok "SHA-256 matches SHA256SUMS ($actual)" }
      else { Write-Bad "SHA-256 MISMATCH vs SHA256SUMS: expected $exp, file is $actual" }
    }
  }
  if (-not $found) { Write-Bad "no checksum file for $name (need $name.sha256 or SHA256SUMS in the same folder)" }
}

function Test-Cosign([string]$File, [string]$Label) {
  $name = Split-Path -Leaf $File
  $bundle = "$File.sigstore.json"
  if (-not (Test-Path -LiteralPath $bundle)) { Write-Bad "${Label}: no signature bundle $name.sigstore.json next to the file"; return }
  # cosign writes progress to stderr; do not let that turn into a terminating error.
  $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $nbf = $script:NewBundleFlag
  $out = & cosign verify-blob @nbf --bundle $bundle --certificate-identity-regexp $IdentityRegexp --certificate-oidc-issuer $issuer $File 2>&1
  $code = $LASTEXITCODE
  $ErrorActionPreference = $prev
  if ($code -eq 0) { Write-Ok "${Label}: cosign signature valid; signed by a $Repo dino8-app.yml workflow run (issuer $issuer)" }
  else {
    Write-Bad "${Label}: cosign verification FAILED"
    ($out | Out-String).Trim().Split("`n") | ForEach-Object { Write-Host "           $_" }
  }
}

function Test-Authenticode([string]$File) {
  $name = Split-Path -Leaf $File
  $sig = Get-AuthenticodeSignature -LiteralPath $File
  switch ($sig.Status) {
    'Valid' {
      Write-Ok "Authenticode: valid signature by '$($sig.SignerCertificate.Subject)'"
      if ($sig.TimeStamperCertificate) { Write-Ok "Authenticode: countersigned by timestamp authority '$($sig.TimeStamperCertificate.Subject)'" }
      else { Write-Note 'Authenticode: no trusted timestamp (the signature stops validating when the certificate expires)' }
    }
    'NotSigned' {
      if ($RequireAuthenticode) { Write-Bad "Authenticode: $name is not Authenticode-signed" }
      else { Write-Note "Authenticode: $name is not Authenticode-signed (expected until a code-signing certificate is configured; see docs/CODE_SIGNING.md). Windows SmartScreen will show 'Unknown publisher'." }
    }
    default { Write-Bad "Authenticode: status $($sig.Status) - $($sig.StatusMessage)" }
  }
}

function Test-Attestation([string]$File) {
  if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Write-Bad 'attestation: gh CLI not installed'; return }
  $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $out = & gh attestation verify $File --repo $Repo 2>&1
  $code = $LASTEXITCODE
  $ErrorActionPreference = $prev
  if ($code -eq 0) { Write-Ok "attestation: GitHub build-provenance attestation verified for $Repo" }
  else {
    Write-Bad 'attestation: gh attestation verify FAILED (also fails if gh is not logged in)'
    ($out | Out-String).Trim().Split("`n") | Select-Object -Last 5 | ForEach-Object { Write-Host "           $_" }
  }
}

# Collect artifacts.
$exts = @('.exe', '.zip', '.deb', '.dmg')
$files = New-Object System.Collections.Generic.List[string]
foreach ($p in $Path) {
  if (Test-Path -LiteralPath $p -PathType Container) {
    Get-ChildItem -LiteralPath $p -File | Where-Object { $exts -contains $_.Extension.ToLowerInvariant() -or $_.Name -like '*.tar.gz' } |
      Sort-Object Name | ForEach-Object { $files.Add($_.FullName) }
  } elseif (Test-Path -LiteralPath $p -PathType Leaf) {
    $files.Add((Resolve-Path -LiteralPath $p).Path)
  } else {
    [Console]::Error.WriteLine("no such file or directory: $p"); exit 2
  }
}
if ($files.Count -eq 0) { [Console]::Error.WriteLine("no release artifacts (.exe .zip .deb .tar.gz .dmg) found in: $($Path -join ' ')"); exit 2 }

Write-Host 'Dino 8 release verification'
Write-Host "  repository : $Repo"
if (-not $SkipCosign) { Write-Host "  identity   : $IdentityRegexp" }

$cosignReady = $false
# The release workflow writes the new-style Sigstore bundle: cosign 2.4.x needs
# --new-bundle-format to read it, cosign 3.x reads it by default (and may no
# longer list the flag), so only pass the flag when cosign knows it.
$script:NewBundleFlag = @()
if (-not $SkipCosign) {
  $cosignReady = [bool](Get-Command cosign -ErrorAction SilentlyContinue)
  if ($cosignReady) {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $help = (& cosign verify-blob --help 2>&1 | Out-String)
    $ErrorActionPreference = $prev
    if ($help -match '--new-bundle-format') { $script:NewBundleFlag = @('--new-bundle-format') }
  }
}
$seenDirs = @{}

foreach ($f in $files) {
  Write-Host ''
  Write-Host $f
  Test-Checksums $f
  if ($SkipCosign) {
    Write-Note 'cosign: skipped (-SkipCosign); checksums alone do not prove who published the file'
  } elseif ($cosignReady) {
    Test-Cosign $f 'cosign'
    $d = Split-Path -Parent $f
    if (-not $seenDirs.ContainsKey($d)) {
      $seenDirs[$d] = $true
      $sums = Join-Path $d 'SHA256SUMS'
      if ((Test-Path -LiteralPath $sums) -and (Test-Path -LiteralPath "$sums.sigstore.json")) { Test-Cosign $sums 'cosign (SHA256SUMS)' }
    }
  } else {
    Write-Bad 'cosign: not installed - cannot check the signature (https://docs.sigstore.dev/cosign/system_config/installation/ ; or pass -SkipCosign)'
  }
  if ($f.ToLowerInvariant().EndsWith('.exe')) { Test-Authenticode $f }
  if ($Attestation) { Test-Attestation $f }
}

Write-Host ''
Write-Host "Summary: $($script:nOk) passed, $($script:nFail) failed, $($script:nInfo) informational"
if ($script:nFail -gt 0) {
  Write-Host 'RESULT: NOT VERIFIED - do not install this file.' -ForegroundColor Red
  exit 1
}
Write-Host 'RESULT: all requested checks passed.' -ForegroundColor Green
if ($SkipCosign) { Write-Host '(Signature check was skipped: this confirms integrity only, not origin.)' }
exit 0
