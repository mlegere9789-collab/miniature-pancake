<#
.SYNOPSIS
  Negative / tamper test of scripts/verify-release.ps1 (Windows twin of
  scripts/test-verify-release.sh): no release, no network, no certificate.

.DESCRIPTION
  Builds a small fixture of fake artifacts with SHA256SUMS and <file>.sha256,
  then runs the real verify-release.ps1 in a child PowerShell:
    * positive control: the untouched fixture passes (-SkipCosign);
    * a modified file, a wrong SHA256SUMS entry, a wrong <file>.sha256 entry, a
      file missing from SHA256SUMS, no checksum file at all, and (without
      -SkipCosign) a missing signature bundle must each exit 1.
  Exit status: 0 all expectations held, 1 otherwise.
#>
$ErrorActionPreference = 'Stop'
$verify = Join-Path $PSScriptRoot 'verify-release.ps1'
$tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$work = Join-Path $tempRoot ('dino8-verify-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$pwshExe = (Get-Process -Id $PID).Path
$script:failures = 0
$script:checks = 0

function Get-Hash256([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }

# The fixture's two files are named .zip on purpose: verify-release.ps1 asks Windows to validate any .exe's Authenticode
# signature, which needs a real PE file; this test is about checksums and signature bundles, not Authenticode.
function New-Release([string]$Dir) {
  if (Test-Path -LiteralPath $Dir) { Remove-Item -Recurse -Force -LiteralPath $Dir }
  New-Item -ItemType Directory -Path $Dir | Out-Null
  $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
  $sums = @()
  foreach ($spec in @(@('Dino8Setup-0.0.0-test.zip', 20000), @('Dino8-0.0.0-test-win64.zip', 30000))) {
    $b = New-Object byte[] ([int]$spec[1])
    $rng.GetBytes($b)
    $p = Join-Path $Dir $spec[0]
    [IO.File]::WriteAllBytes($p, $b)
    $h = Get-Hash256 $p
    "$h  $($spec[0])" | Set-Content -LiteralPath "$p.sha256" -Encoding ascii
    $sums += "$h  $($spec[0])"
  }
  $rng.Dispose()
  $sums | Set-Content -LiteralPath (Join-Path $Dir 'SHA256SUMS') -Encoding ascii
}

# Runs verify-release.ps1 in a child process; returns @{ Code; Output }.
function Invoke-Verify([string[]]$VerifyArgs) {
  $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $out = & $pwshExe -NoProfile -NonInteractive -File $verify @VerifyArgs 2>&1 | Out-String
  $code = $LASTEXITCODE
  $ErrorActionPreference = $prev
  return @{ Code = $code; Output = $out }
}

function Expect-Fail([string]$Label, [string]$Text, [string[]]$VerifyArgs) {
  $script:checks++
  $r = Invoke-Verify $VerifyArgs
  if ($r.Code -ne 1) { Write-Host "  FAIL  ${Label}: expected exit 1, got $($r.Code)"; Write-Host $r.Output; $script:failures++; return }
  if ($Text -and -not $r.Output.Contains($Text)) { Write-Host "  FAIL  ${Label}: exit 1 but the output does not mention '$Text'"; Write-Host $r.Output; $script:failures++; return }
  if (-not $r.Output.Contains('NOT VERIFIED')) { Write-Host "  FAIL  ${Label}: exit 1 but no NOT VERIFIED verdict"; $script:failures++; return }
  Write-Host "  PASS  ${Label}: verifier exits 1 (NOT VERIFIED)"
}

try {
  $rel = Join-Path $work 'rel'
  $exe = Join-Path $rel 'Dino8Setup-0.0.0-test.zip'
  $zip = Join-Path $rel 'Dino8-0.0.0-test-win64.zip'

  Write-Host '== positive control (must PASS)'
  New-Release $rel
  $script:checks++
  $r = Invoke-Verify @('-SkipCosign', $rel)
  if ($r.Code -eq 0 -and $r.Output.Contains('all requested checks passed')) { Write-Host '  PASS  untouched fixture verifies (checksums only)' }
  else { Write-Host "  FAIL  untouched fixture did not verify (exit $($r.Code))"; Write-Host $r.Output; $script:failures++ }

  Write-Host '== tamper: modified file'
  New-Release $rel
  $b = [IO.File]::ReadAllBytes($zip); $b[15000] = [byte]($b[15000] -bxor 0xFF); [IO.File]::WriteAllBytes($zip, $b)
  Expect-Fail 'one flipped byte in the .zip' 'MISMATCH' @('-SkipCosign', $rel)
  New-Release $rel
  [IO.File]::WriteAllBytes($exe, [byte[]]@())
  Expect-Fail 'file truncated to zero bytes' 'MISMATCH' @('-SkipCosign', $exe)

  Write-Host '== tamper: wrong checksum entries'
  $bad = '0' * 64
  New-Release $rel
  Get-ChildItem -LiteralPath $rel -Filter '*.sha256' | Remove-Item -Force
  $lines = Get-Content -LiteralPath (Join-Path $rel 'SHA256SUMS') | ForEach-Object { if ($_ -match 'Dino8Setup') { "$bad  Dino8Setup-0.0.0-test.zip" } else { $_ } }
  $lines | Set-Content -LiteralPath (Join-Path $rel 'SHA256SUMS') -Encoding ascii
  Expect-Fail 'wrong entry in SHA256SUMS only' 'MISMATCH vs SHA256SUMS' @('-SkipCosign', $exe)

  New-Release $rel
  Remove-Item -Force -LiteralPath (Join-Path $rel 'SHA256SUMS')
  "$bad  Dino8Setup-0.0.0-test.zip" | Set-Content -LiteralPath "$exe.sha256" -Encoding ascii
  Expect-Fail 'wrong entry in <file>.sha256 only' 'MISMATCH vs Dino8Setup-0.0.0-test.zip.sha256' @('-SkipCosign', $exe)

  New-Release $rel
  Get-ChildItem -LiteralPath $rel -Filter '*.sha256' | Remove-Item -Force
  $kept = @(Get-Content -LiteralPath (Join-Path $rel 'SHA256SUMS') | Where-Object { $_ -notmatch 'Dino8Setup' })
  $kept | Set-Content -LiteralPath (Join-Path $rel 'SHA256SUMS') -Encoding ascii
  Expect-Fail 'file missing from SHA256SUMS' 'does not list' @('-SkipCosign', $exe)

  New-Release $rel
  Get-ChildItem -LiteralPath $rel -Filter '*.sha256' | Remove-Item -Force
  Remove-Item -Force -LiteralPath (Join-Path $rel 'SHA256SUMS')
  Expect-Fail 'no checksum file at all' 'no checksum file' @('-SkipCosign', $exe)

  Write-Host '== fails closed on the signature (checksums are intact here)'
  New-Release $rel
  if (Get-Command cosign -ErrorAction SilentlyContinue) {
    Expect-Fail 'no .sigstore.json bundle (no -SkipCosign)' 'no signature bundle' @($rel)
  } else {
    # Without cosign the verifier fails closed for a different reason ('cosign: not installed'); that is not the missing-bundle branch,
    # so it is not counted as a check of it (scripts/test-verify-release.sh covers the bundle branch on Linux with cosign installed).
    Write-Host '  SKIP  missing-bundle case: cosign is not installed on this runner'
    $script:checks++
    $r = Invoke-Verify @($rel)
    if ($r.Code -eq 1 -and $r.Output.Contains('cosign: not installed')) { Write-Host '  PASS  without cosign the verifier fails closed (exit 1, says cosign is not installed)' }
    else { Write-Host "  FAIL  without cosign the verifier did not fail closed (exit $($r.Code))"; Write-Host $r.Output; $script:failures++ }
  }
} finally {
  Remove-Item -Recurse -Force -LiteralPath $work -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "$($script:checks) checks, $($script:failures) failed"
if ($script:failures -gt 0) { Write-Host 'verify-release.ps1 negative test: FAILED'; exit 1 }
Write-Host 'verify-release.ps1 negative test: passed (the verifier rejects modified files, wrong checksums and missing signatures)'
exit 0
