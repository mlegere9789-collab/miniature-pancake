# Code signing

This page moved. The current, complete description of what Dino 8's release
pipeline signs today, the exact GitHub secret names and one-time steps for
each certificate-based path (SignPath Foundation, Azure Trusted Signing, PFX,
Apple Developer ID and notarization), the `scripts/verify-release.sh` /
`.ps1` verification scripts, and what Windows SmartScreen and macOS Gatekeeper
users will and will not see, is in [`docs/CODE_SIGNING.md`](../../docs/CODE_SIGNING.md)
at the repository root.

Summary: installers are not yet signed with a publisher certificate. Every
published file does carry a SHA-256 checksum, a keyless Sigstore (cosign)
signature and a GitHub build-provenance attestation. Those prove the file came
from this repository's release workflow unmodified; they do not satisfy
SmartScreen or Gatekeeper.
