<#
.SYNOPSIS
  Authenticode-sign (or just verify) Windows binaries in CI. Called by the
  local composite action .github/actions/sign-windows; never runs unless a
  signing secret is configured. See docs/CODE_SIGNING.md.

.PARAMETER Method
  pfx    sign with a PFX/P12 certificate (WINDOWS_CERT_BASE64 / _PASSWORD)
  azure  sign with Azure Trusted Signing (signtool + Azure.CodeSigning.Dlib)
  verify do not sign; only verify that every file carries a valid, timestamped
         Authenticode signature (used after SignPath returned signed files)
  test-pfx  CI SELF-TEST ONLY. Same code path as 'pfx' (decode the base64 PFX
         from WINDOWS_CERT_BASE64 / WINDOWS_CERT_PASSWORD, run signtool sign with
         SHA-256, remove the decoded file) but the certificate is a THROWAWAY
         self-signed certificate generated inside the CI job by
         test-signing.ps1, so: no timestamp server is contacted, and the result
         is checked for "signature PRESENT and made by the expected thumbprint"
         (DINO8_TEST_CERT_THUMBPRINT) instead of "chain trusted". The output is
         NOT trusted-signed and must never be published; the script refuses to
         run unless every file sits in a directory holding the marker file
         .dino8-test-signing-dir (test-signing.ps1 creates it in a scratch copy
         directory).
.PARAMETER Files
  Files to sign / verify.

Inputs come from environment variables rather than parameters (signtool itself
has no way to take the PFX password except on its command line, which is
tolerable on an ephemeral runner but not something to widen):
  pfx   : WINDOWS_CERT_BASE64, WINDOWS_CERT_PASSWORD
  azure : AZURE_TENANT_ID, AZURE_CLIENT_ID, AZURE_CLIENT_SECRET,
          AZURE_TRUSTED_SIGNING_ENDPOINT, AZURE_TRUSTED_SIGNING_ACCOUNT,
          AZURE_TRUSTED_SIGNING_PROFILE
#>
param(
  [Parameter(Mandatory = $true)][ValidateSet('pfx', 'azure', 'verify', 'test-pfx')][string]$Method,
  [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)][string[]]$Files
)
$ErrorActionPreference = 'Stop'

function Find-SignTool {
  $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "$env:ProgramFiles\Windows Kits\10\bin")
  foreach ($arch in 'x64', 'arm64') {
    foreach ($root in $roots) {
      if (-not (Test-Path $root)) { continue }
      $hit = Get-ChildItem -Path $root -Directory |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName "$arch\signtool.exe" } |
        Where-Object { Test-Path $_ } | Select-Object -First 1
      if ($hit) { return $hit }
    }
  }
  throw 'signtool.exe not found (Windows SDK is not installed on this runner)'
}

function Invoke-Retry([scriptblock]$Action, [string]$What) {
  # Timestamp servers are third-party and occasionally flaky; a few retries
  # are cheaper than a failed release.
  for ($i = 1; $i -le 3; $i++) {
    & $Action
    if ($LASTEXITCODE -eq 0) { return }
    Write-Warning "$What failed (exit $LASTEXITCODE), attempt $i of 3"
    Start-Sleep -Seconds (5 * $i)
  }
  throw "$What failed after 3 attempts"
}

function Test-Signed([string]$signtool, [string]$file) {
  # signtool verify /pa = full Authenticode chain validation with the default
  # authentication policy, exactly what Windows does; /tw makes a missing
  # timestamp a warning we surface below.
  & $signtool verify /pa /v $file
  if ($LASTEXITCODE -ne 0) { throw "Authenticode verification FAILED for $file" }
  $sig = Get-AuthenticodeSignature -LiteralPath $file
  if ($sig.Status -ne 'Valid') { throw "Get-AuthenticodeSignature reports '$($sig.Status)' for ${file}: $($sig.StatusMessage)" }
  if (-not $sig.TimeStamperCertificate) { throw "$file is signed but has no trusted timestamp" }
  Write-Host "OK  $file  signer: $($sig.SignerCertificate.Subject)  timestamp: $($sig.TimeStamperCertificate.Subject)"
}

function Test-TestSigned([string]$file) {
  # Test-certificate result check: the signature must be PRESENT and made by the
  # throwaway certificate. It is deliberately NOT required to be trusted - a
  # self-signed certificate is not (Status UnknownError / NotTrusted).
  $expected = $env:DINO8_TEST_CERT_THUMBPRINT
  if (-not $expected) { throw 'DINO8_TEST_CERT_THUMBPRINT is empty' }
  $sig = Get-AuthenticodeSignature -LiteralPath $file
  if (-not $sig.SignerCertificate) { throw "TEST signing left no signature on $file (status $($sig.Status))" }
  if ($sig.SignerCertificate.Thumbprint -ne $expected) { throw "$file is signed by $($sig.SignerCertificate.Thumbprint), expected the throwaway certificate $expected" }
  Write-Host "TEST-SIGNED (NOT TRUSTED)  $file  status: $($sig.Status)  thumbprint: $($sig.SignerCertificate.Thumbprint)"
}

$signtool = Find-SignTool
Write-Host "signtool: $signtool"
$resolved = $Files | ForEach-Object { (Resolve-Path -LiteralPath $_).Path }

$isTest = ($Method -eq 'test-pfx')
if ($isTest) {
  Write-Host '*** TEST CERTIFICATE MODE: signing SCRATCH COPIES with a throwaway self-signed certificate. ***'
  Write-Host '*** The result is NOT trusted-signed and is never published. The released binaries stay as built. ***'
  foreach ($f in $resolved) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $f) '.dino8-test-signing-dir'))) {
      throw "refusing test-pfx signing of $f : its directory has no .dino8-test-signing-dir marker (test mode only signs scratch copies)"
    }
  }
}

switch ($Method) {
  'verify' { }
  { $_ -in 'pfx', 'test-pfx' } {
    if (-not $env:WINDOWS_CERT_BASE64) { throw 'WINDOWS_CERT_BASE64 is empty' }
    $pfx = Join-Path $env:RUNNER_TEMP 'dino8-signing.pfx'
    try {
      [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($env:WINDOWS_CERT_BASE64))
      if ($isTest) {
        # Throwaway certificate: no timestamp server (nothing to gain from a
        # third party timestamping a signature nobody will trust).
        foreach ($f in $resolved) {
          & $signtool sign /f $pfx /p $env:WINDOWS_CERT_PASSWORD /fd SHA256 /d 'Dino 8 (TEST CERTIFICATE - NOT TRUSTED)' $f
          if ($LASTEXITCODE -ne 0) { throw "signtool sign (test certificate) failed for $f (exit $LASTEXITCODE)" }
        }
      } else {
        $servers = 'http://timestamp.digicert.com', 'http://timestamp.sectigo.com', 'http://timestamp.globalsign.com/tsa/r6advanced1'
        foreach ($f in $resolved) {
          $done = $false
          foreach ($ts in $servers) {
            & $signtool sign /f $pfx /p $env:WINDOWS_CERT_PASSWORD /fd SHA256 /tr $ts /td SHA256 /d 'Dino 8' $f
            if ($LASTEXITCODE -eq 0) { $done = $true; break }
            Write-Warning "signtool sign with timestamp server $ts failed (exit $LASTEXITCODE)"
          }
          if (-not $done) { throw "could not sign $f with any timestamp server" }
        }
      }
    } finally {
      if (Test-Path $pfx) { Remove-Item -Force $pfx }
    }
  }
  'azure' {
    foreach ($v in 'AZURE_TENANT_ID', 'AZURE_CLIENT_ID', 'AZURE_CLIENT_SECRET', 'AZURE_TRUSTED_SIGNING_ENDPOINT', 'AZURE_TRUSTED_SIGNING_ACCOUNT', 'AZURE_TRUSTED_SIGNING_PROFILE') {
      if (-not (Get-Item "env:$v" -ErrorAction SilentlyContinue).Value) { throw "$v is empty" }
    }
    # The Azure client library (DefaultAzureCredential) reads AZURE_TENANT_ID /
    # AZURE_CLIENT_ID / AZURE_CLIENT_SECRET from the environment itself.
    $pkgDir = Join-Path $env:RUNNER_TEMP 'trusted-signing-client'
    Invoke-Retry { nuget install Microsoft.Trusted.Signing.Client -OutputDirectory $pkgDir -NonInteractive | Out-Host } 'nuget install Microsoft.Trusted.Signing.Client'
    $dlib = Get-ChildItem -Path $pkgDir -Recurse -Filter 'Azure.CodeSigning.Dlib.dll' |
      Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1
    if (-not $dlib) { throw 'Azure.CodeSigning.Dlib.dll (x64) not found in the Microsoft.Trusted.Signing.Client package' }
    $meta = Join-Path $env:RUNNER_TEMP 'trusted-signing-metadata.json'
    @{
      Endpoint               = $env:AZURE_TRUSTED_SIGNING_ENDPOINT
      CodeSigningAccountName = $env:AZURE_TRUSTED_SIGNING_ACCOUNT
      CertificateProfileName = $env:AZURE_TRUSTED_SIGNING_PROFILE
    } | ConvertTo-Json | Set-Content -Path $meta -Encoding ascii
    try {
      foreach ($f in $resolved) {
        Invoke-Retry { & $signtool sign /v /fd SHA256 /tr 'http://timestamp.acs.microsoft.com' /td SHA256 /dlib $dlib.FullName /dmdf $meta $f } "Azure Trusted Signing of $f"
      }
    } finally {
      if (Test-Path $meta) { Remove-Item -Force $meta }
    }
  }
}

if ($isTest) {
  foreach ($f in $resolved) { Test-TestSigned $f }
} else {
  foreach ($f in $resolved) { Test-Signed $signtool $f }
}
