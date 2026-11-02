# Code signing and release verification

Dino 8's release pipeline (`.github/workflows/dino8-app.yml`) signs and attests
everything it can **without buying anything**, and has every certificate-based
path wired up so it switches on the moment the matching GitHub secret exists.

This page says plainly what each signature proves, what it does not, and what a
Windows or macOS user will still see until a trusted certificate is installed.
Buying or enrolling for a certificate is a legal/financial step that only the
project owner can do; nothing in this repository can do it on their behalf.

## 1. What is signed today (no certificate, no secret)

Every push that publishes a beta pre-release (`release` job, push and
`workflow_dispatch` only, never pull requests) adds, for **every** installer
and package (`.exe`, `.zip`, `.deb`, `.tar.gz`, `.dmg`):

| Added file / record | What it is | What it proves | What it does **not** prove |
|---|---|---|---|
| `<file>.sha256`, `SHA256SUMS` | SHA-256 of each published file | The file you downloaded is byte-for-byte what was published (no corruption or truncation). | Who published it. Whoever can replace the file can replace the checksum. Only meaningful together with the signature below. |
| `<file>.sigstore.json`, `SHA256SUMS.sigstore.json` | Keyless [Sigstore](https://www.sigstore.dev/) signature made with `cosign sign-blob` using the workflow run's GitHub OIDC identity (short-lived certificate from Fulcio, entry in the Rekor transparency log). No signing key exists to steal. | The file was signed by a run of **this repository's** `dino8-app.yml` workflow and has not been modified since. Check with `cosign verify-blob --certificate-identity-regexp ... --certificate-oidc-issuer https://token.actions.githubusercontent.com`. | That the code is safe, or that a real-world person or company stands behind it. The "identity" is a workflow path in a GitHub repository, not a legal entity. |
| GitHub build-provenance attestation (`actions/attest-build-provenance`) | A signed statement, stored by GitHub, linking each file's digest to the workflow run, commit and repository. Check with `gh attestation verify <file> --repo <owner>/<repo>`. | Which repository, workflow, commit and run produced the file. | Same limits as above. Note it is created in the publish job over the files that job downloaded from the build jobs, so it attests "these bytes left this workflow run", not an independently reproducible build. |

Neither Windows nor macOS (nor any Linux package manager) looks at any of
these. They protect people who choose to run the verification script; they do
**not** remove SmartScreen or Gatekeeper warnings (section 5).

The publish job also runs `scripts/verify-release.sh` on the files it is about
to upload, so a release whose signatures do not verify is not published.

### Verifying a download

```sh
# Linux / macOS (needs cosign: https://docs.sigstore.dev/cosign/system_config/installation/)
scripts/verify-release.sh ~/Downloads/dino8-release/

# Windows (PowerShell)
.\scripts\verify-release.ps1 $HOME\Downloads\dino8-release
```

Put the installer, its `.sha256`, its `.sigstore.json` and `SHA256SUMS` in one
folder. The script checks, per file:

1. the SHA-256 against `<file>.sha256` and `SHA256SUMS` (integrity);
2. `cosign verify-blob --bundle <file>.sigstore.json --certificate-identity-regexp '^https://github\.com/<owner>/<repo>/\.github/workflows/dino8-app\.yml@refs/(heads|tags)/.+$' --certificate-oidc-issuer https://token.actions.githubusercontent.com <file>` (origin: signed by this repository's release workflow);
3. for `.exe`: the Authenticode signature if there is one (the PowerShell script
   asks Windows to validate the chain; the shell script uses `osslsigncode` if
   installed and otherwise says it skipped). An unsigned `.exe` is reported as
   information, not an error, unless `--require-authenticode` /
   `-RequireAuthenticode` is given;
4. with `--attestation` / `-Attestation`: `gh attestation verify`.

It fails closed (exit 1, "NOT VERIFIED") when a checksum, bundle or cosign
itself is missing; `--skip-cosign` / `-SkipCosign` downgrades to a
checksum-only check and says so. Use `--repo OWNER/NAME` for a fork, or
`--identity-regexp` to pin to `refs/heads/main` or a release tag only. Verifying
a Sigstore bundle needs network access (transparency log and trust root).

If the repository is private, GitHub attestations need GitHub Enterprise Cloud,
and keyless Sigstore signing records the repository name in the public Rekor
log. Both are fine for this public project.

## 2. What each certificate-based path adds

| Path | Turns on when these secrets all exist | Adds |
|---|---|---|
| Windows: SignPath | `SIGNPATH_API_TOKEN`, `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG` | Authenticode signature (timestamped) on `Dino8.exe`, `Dino8Setup-*.exe`, and the Windows-on-ARM `Dino8.exe` (hence the portable zips). |
| Windows: Azure Trusted Signing | `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`, `AZURE_TRUSTED_SIGNING_ENDPOINT`, `AZURE_TRUSTED_SIGNING_ACCOUNT`, `AZURE_TRUSTED_SIGNING_PROFILE` | Same, via `signtool` + the Azure signing dlib. |
| Windows: PFX file | `WINDOWS_CERT_BASE64`, `WINDOWS_CERT_PASSWORD` | Same, via `signtool` with a certificate file. |
| macOS: sign | `APPLE_CERT_BASE64`, `APPLE_CERT_PASSWORD`, `APPLE_KEYCHAIN_PASSWORD`, `APPLE_SIGNING_IDENTITY` | Developer ID signature, hardened runtime, entitlements (`dino8-app/packaging/macos/entitlements.plist`) on the app inside the `.dmg`, and on the `.dmg`. |
| macOS: notarize | the four above **plus** `APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_APP_SPECIFIC_PASSWORD` | Submission to Apple's notary service (`notarytool`), the ticket stapled to the `.dmg`, then `stapler validate` and `spctl --assess`. |

If more than one Windows method is configured, precedence is
SignPath, then Azure Trusted Signing, then PFX. Each method verifies its own
result afterwards (`signtool verify /pa` plus `Get-AuthenticodeSignature`, and a
check that a trusted timestamp is present) and fails the build if the
signature is not valid. No signing step ever runs for `pull_request` events,
and every secret is read through `env`/`with` indirection (GitHub does not allow
`secrets` inside `if:`; the job-level `WINDOWS_SIGN_METHOD`, `HAS_APPLE_CERT`
and `HAS_APPLE_NOTARY` variables carry the decision).

Add secrets under **Settings -> Secrets and variables -> Actions -> New
repository secret**. Names must match exactly. Never commit a certificate, key
or password to the repository.

### 2a. Windows via SignPath Foundation (free for open-source projects)

SignPath Foundation offers free Authenticode signing to qualifying open-source
projects. The certificate belongs to SignPath Foundation, so Windows shows
**"SignPath Foundation"** as the publisher, not "Dino 8 Project". Eligibility
(an OSI-approved license, a public repository, released and maintained
software, builds on GitHub-hosted runners, a published code-signing policy and
privacy statement, MFA for the people involved) and approval are decided by
SignPath; check the current terms at <https://signpath.org/> before relying on
this. The repository's `LICENSE` is MIT.

One-time steps (all done by the project owner, in SignPath's and GitHub's web
UIs):

1. Apply to the SignPath Foundation program and wait for approval.
2. In SignPath, create the project and a signing policy (for example
   `release-signing`), and an **artifact configuration** that signs the `.exe`
   files at the top level of the uploaded zip, along the lines of
   `<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1"><zip-file><pe-file-set><include path="*.exe"/><authenticode-sign/></pe-file-set></zip-file></artifact-configuration>`
   (check the exact syntax in SignPath's documentation).
3. Connect the GitHub repository as a trusted build system in SignPath, and
   create an API token for a CI user with permission to submit to that policy.
4. Add the four secrets: `SIGNPATH_API_TOKEN`, `SIGNPATH_ORGANIZATION_ID`,
   `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`.

The workflow uploads the unsigned files as a temporary workflow artifact
(`signpath-unsigned-*`, never published), has SignPath's official action
submit and wait for the signed result, copies the signed files back, and
verifies them.

### 2b. Windows via Azure Trusted Signing

Microsoft's cloud signing service issues short-lived certificates to a verified
identity and keeps the key in its HSM, so there is no file to leak. It is paid
(roughly ten US dollars a month at the time of writing; check current pricing)
and identity validation is restricted by country and entity type, which has
changed over time; the service has also been renamed ("Artifact Signing"), so
follow Microsoft's current documentation. One-time steps:

1. Create an Azure subscription, then a Trusted Signing account in a region.
2. Complete identity validation, then create a **Public Trust** certificate
   profile.
3. Create a Microsoft Entra app registration (service principal) with a client
   secret, and give it the role **Trusted Signing Certificate Profile Signer**
   on the signing account.
4. Add the six secrets. `AZURE_TRUSTED_SIGNING_ENDPOINT` is the regional URL
   such as `https://eus.codesigning.azure.net/`; `..._ACCOUNT` and `..._PROFILE`
   are the account and certificate-profile names; the other three come from the
   app registration.

Because the certificates are short-lived, the **timestamp** is what keeps old
signatures valid; the workflow always timestamps (`timestamp.acs.microsoft.com`)
and the verification step fails if no trusted timestamp is present.

### 2c. Windows via a PFX certificate (legacy route)

`WINDOWS_CERT_BASE64` is a base64-encoded `.pfx`, `WINDOWS_CERT_PASSWORD` its
password (`base64 -w0 cert.pfx`, or `certutil -encode` on Windows). Be aware
that since 2023 the CA/Browser Forum rules require the private key of a publicly
trusted code-signing certificate to live on a hardware token or cloud HSM, so
most newly issued certificates **cannot** be exported as a `.pfx`; this route
fits an existing exportable certificate or a CA that still offers one. Prefer
2a or 2b. The step signs with SHA-256, timestamps against DigiCert, Sectigo or
GlobalSign (first one that answers), removes the decoded file afterwards and
verifies the result. Typical commercial prices are in the low hundreds of US
dollars a year; get a current quote.

`osslsigncode` is not used in CI (the Windows runners have `signtool`); it is a
reasonable way to sign or inspect `.exe` files from Linux by hand.

### 2d. macOS: Developer ID signing and notarization

Needs an Apple Developer Program membership (99 US dollars a year, enrolled as
an individual or organization at <https://developer.apple.com/programs/>).
One-time steps:

1. In the Developer account create a **Developer ID Application** certificate;
   export it with its private key from Keychain Access as a `.p12` with a
   password.
2. `APPLE_CERT_BASE64` = `base64 -w0 cert.p12` (or `base64 -i cert.p12`);
   `APPLE_CERT_PASSWORD` = that password; `APPLE_KEYCHAIN_PASSWORD` = any random
   string (it only protects the throwaway keychain the job creates and deletes);
   `APPLE_SIGNING_IDENTITY` = the certificate's name as shown by
   `security find-identity -v -p codesigning`, e.g.
   `Developer ID Application: Your Name (TEAMID1234)`.
3. For notarization also: `APPLE_ID` (the account's Apple ID email),
   `APPLE_TEAM_ID` (10 characters, Membership page) and
   `APPLE_APP_SPECIFIC_PASSWORD` (generated at <https://appleid.apple.com> under
   Sign-In and Security).

The app is signed **inside** the `.dmg` after `cpack`: CPack stages its own copy
of the app, so signing the build-tree `Dino8.app` beforehand (as an earlier
version of this workflow did) would never have signed what ships. The script
`dino8-app/packaging/macos/sign-dmg.sh` signs nested code first, then the
bundle with `--options runtime --timestamp` and the entitlements file, checks
the hardened-runtime flag, rebuilds the image under the same name, signs it,
and (notarize mode) reads the notary verdict from `notarytool`'s JSON output
rather than trusting its exit code.

If only the first four macOS secrets are set, the image is signed but **not
notarized**, and the job prints a warning: Gatekeeper still blocks it on current
macOS.

## 3. What the pipeline changed in the existing workflow

- Top-level `permissions` is now `contents: read`; only the `release` job gets
  `contents: write`, `id-token: write`, `attestations: write`.
- The Windows signing steps now call the local composite action
  `.github/actions/sign-windows` (which wraps `dino8-app/packaging/windows/sign.ps1`
  and, for SignPath, the official SignPath action) and also sign the Windows-on-ARM exe.
- The macOS signing moved from the build-tree app to the app inside the `.dmg`
  (see above); the certificate keychain is deleted at the end of the job.
- All signing steps additionally require `github.event_name != 'pull_request'`.
- The release job stages files with `scripts/stage-release.sh`, publishes
  `release/*`, and only downloads artifacts named `Dino8-*`.

## 4. Linux

The `.deb` and `.tar.gz` are covered by the checksum, cosign signature and
attestation only. There is no apt repository and no `dpkg-sig`/GPG signature,
and Linux desktops do not gate on signatures for a manually installed package.

## 5. What users will and will not see

**Today (nothing below is signed by a publisher certificate):**

- **Windows.** Running `Dino8Setup-*.exe` or `Dino8.exe` from a download shows
  the blue SmartScreen dialog "Windows protected your PC" with publisher
  **"Unknown publisher"**; the user must click *More info* and then *Run
  anyway*. The installer's UAC prompt also says "Unknown publisher". Browsers
  may add their own "not commonly downloaded" warning. On Windows 11 with
  **Smart App Control** turned on, an unsigned app can be blocked outright with
  no *Run anyway* option. The portable `.zip` has the same warning on the
  extracted `.exe`. Antivirus products are more suspicious of unsigned
  binaries.
- **macOS.** Gatekeeper refuses to open the app from a downloaded `.dmg`
  ("Apple cannot check it for malicious software", or "damaged and can't be
  opened" for quarantined downloads). On current macOS the Control-click ->
  Open shortcut no longer works; the user must try to open it once, then use
  **System Settings -> Privacy & Security -> Open Anyway**. Apple Silicon
  executables carry the linker's ad-hoc signature, which lets them run once
  allowed but gives Gatekeeper no identity to trust.
- **Linux.** No operating-system warning.
- **Everyone** can still verify the download with the script above and get
  "signed by this repository's release workflow". The OS does not know about
  that and does not show it.

**After a trusted certificate is configured:**

- **Windows with any valid Authenticode signature.** The publisher name appears
  (the certificate holder: your organization, or "SignPath Foundation") instead
  of "Unknown publisher", and the file can no longer be silently modified.
  SmartScreen is **reputation based**, so the warning does not necessarily go
  away immediately: new certificates and new files can still show "Windows
  protected your PC" until enough users have run them. Extended Validation
  certificates used to bypass this at once; Microsoft changed that in 2024, so
  do not assume any certificate type removes the dialog on day one. Azure
  Trusted Signing and SignPath-signed files build reputation like any other.
- **macOS signed but not notarized.** Still blocked by Gatekeeper on current
  macOS, with a slightly different message.
- **macOS signed, notarized and stapled.** The `.dmg` and app open after the
  normal one-time "downloaded from the Internet, are you sure?" prompt, with the
  developer named; no Open Anyway detour, and it works offline because the
  ticket is stapled.

Status: the checksum, cosign and attestation steps are the only ones that will
run today. The Authenticode, SignPath, Azure and Apple steps have never run
against a real certificate or account (none exists), so expect to fix small
details on the first real run, and treat the entitlements file in particular as
a starting point.
