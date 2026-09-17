# Code signing

Dino 8's installers and disk images are currently **unsigned**. This document
describes the CI wiring that is already in place for signing them, and what
the project owner needs to buy and configure to turn it on. Signing itself is
a business/legal step (buying a certificate, enrolling in Apple's Developer
Program under a real legal identity) that this codebase cannot complete on
its own — nothing here can act on the owner's behalf to purchase a
certificate or agree to Apple's or a CA's terms.

## Current state

`.github/workflows/dino8-app.yml` has signing and notarization steps for both
Windows and macOS. Every one of them is gated behind `if:` conditions that
check whether the relevant GitHub Actions secret is set:

- Windows: `if: ${{ secrets.WINDOWS_CERT_BASE64 != '' }}`
- macOS: `if: ${{ secrets.APPLE_CERT_BASE64 != '' }}`

Right now none of those secrets exist in the repository, so every signing
step is skipped — CI behaves exactly as it did before this document was
added, and every existing green build/artifact stays green and unsigned.
The moment the owner adds the secrets below, the same workflow runs, without
further code changes, will sign the Windows binaries and installer and
codesign+notarize the macOS app and disk image.

## What to buy

### Windows: an Authenticode code-signing certificate

- **What it is**: an X.509 code-signing certificate issued by a CA in
  Microsoft's trusted root program, used with `signtool sign` to
  Authenticode-sign `Dino8.exe` and `Dino8Setup-*.exe`. Without it, Windows
  SmartScreen shows an "Unknown publisher" warning on every install.
- **Two kinds**:
  - **OV (Organization Validation)**: identity-verified to the publisher
    organization; typically issued as a `.pfx`/`.p12` file (or via a
    cloud HSM). Roughly **$70–$400/year** depending on the CA
    (e.g. Sectigo, DigiCert, SSL.com) and reseller.
  - **EV (Extended Validation)**: stronger identity verification, and gets
    Microsoft SmartScreen reputation immediately instead of building it up
    over time; **must** be stored on a hardware token (USB HSM) or a
    cloud-HSM signing service — it cannot be exported as a plain `.pfx`, so
    it does not fit this workflow's "decode a base64 secret" model without
    a cloud-HSM signing service (e.g. SSL.com's eSigner, DigiCert KeyLocker,
    Azure Trusted Signing) that exposes a REST/CLI signing API instead of a
    local file. Roughly **$300–$700/year**.
  - For a from-scratch free project, an **OV certificate as a downloadable
    `.pfx`** is the one that matches the workflow step already wired up
    (`WINDOWS_CERT_BASE64` is a base64-encoded `.pfx`/`.p12` file).
- **Where to buy**: any CA or reseller in Microsoft's trusted list —
  DigiCert, Sectigo, SSL.com, GlobalSign, Certum are common choices.
  Certum in particular has historically offered a lower-cost open-source /
  individual-developer tier; prices and offerings change, so get a current
  quote rather than trusting the numbers above verbatim.
- **Identity requirement**: OV certificates require verifying a real legal
  entity or individual (business registration or government ID, a phone
  call to a listed number, etc.) — this cannot be done by an AI agent or
  automated process.

### macOS: an Apple Developer Program membership + Developer ID certificate

- **What it is**: Apple Developer Program membership (**$99/year**, paid to
  Apple, enrolled as an individual or organization) is a prerequisite for
  getting a **Developer ID Application** certificate from Apple's own CA.
  That certificate is used with `codesign` to sign `Dino8.app`, and Apple's
  **notarization** service (`xcrun notarytool`) then scans the signed,
  packaged build and staples a ticket to the `.dmg` so Gatekeeper will run it
  without a "can't be opened because Apple cannot check it for malicious
  software" block.
- **Where to enroll**: https://developer.apple.com/programs/ — requires an
  Apple ID, a real legal identity (individual or D-U-N-S-registered
  organization), and Apple's own review process; this is an account Apple
  issues to a real person or company, not something obtainable by a CI job.
  It cannot be pre-purchased or automated by this codebase.
- **Certificate**: created from within the enrolled Apple Developer account
  (Xcode or the Developer website), exported as a `.p12` from Keychain
  Access with a password.
- **Notarization credentials**: an **app-specific password** (generated at
  https://appleid.apple.com under Sign-In and Security → App-Specific
  Passwords) for the Apple ID used with `notarytool`, plus the account's
  Team ID (visible on the Developer account's Membership page).

## Exact GitHub secret names the workflow expects

Add these under the repository's **Settings → Secrets and variables →
Actions → New repository secret**. Names must match exactly — the workflow
reads them by these names.

| Secret | Used for | How to produce it |
|---|---|---|
| `WINDOWS_CERT_BASE64` | Windows `signtool sign` | `certutil -encode MyCert.pfx MyCert_base64.txt` (Windows) or `base64 -w0 MyCert.pfx` (Linux/macOS) on the `.pfx`/`.p12` file the CA issued, then paste the resulting text as the secret value |
| `WINDOWS_CERT_PASSWORD` | Password protecting that `.pfx` | Whatever password was set when the `.pfx` was exported/issued |
| `APPLE_CERT_BASE64` | macOS `codesign` (imported into a temporary CI keychain) | Export the Developer ID Application certificate + private key from Keychain Access as `.p12`, then `base64 -w0 Cert.p12` |
| `APPLE_CERT_PASSWORD` | Password protecting that `.p12` | Whatever password was set on export from Keychain Access |
| `APPLE_KEYCHAIN_PASSWORD` | Password for the throwaway keychain CI creates to hold the imported certificate for the run | Any strong random string you generate once and store as a secret — it never needs to match anything else, it just needs to exist for the temporary keychain's own unlock |
| `APPLE_SIGNING_IDENTITY` | Selects which identity `codesign --sign` uses | The certificate's common name, e.g. `Developer ID Application: Your Name or Org (TEAMID1234)` — find it with `security find-identity -v -p codesigning` after importing the cert locally |
| `APPLE_ID` | `notarytool` authentication | The Apple ID email enrolled in the Developer Program |
| `APPLE_TEAM_ID` | `notarytool` authentication | The 10-character Team ID from the Developer account's Membership page |
| `APPLE_APP_SPECIFIC_PASSWORD` | `notarytool` authentication | An app-specific password generated at appleid.apple.com for that Apple ID |

Once `WINDOWS_CERT_BASE64` and `APPLE_CERT_BASE64` are both set, every run of
the `Dino 8 App` workflow signs the Windows binaries and installer, and
codesigns + notarizes + staples the macOS `.dmg`, automatically. Until then,
CI is unaffected and every artifact remains unsigned, exactly as before.

## Why this can't be "finished" from here

Both certificates require an entity Apple/the CA verifies as a real business
or individual, payment with a real payment method, and (for Apple) agreeing
to Apple's Developer Program License Agreement — all things that only the
project owner can do. This document exists so that, once that business step
is done, wiring the result into CI is a five-minute "paste these secrets in"
job rather than a code change.

See `dino8-app/docs/INTEROP_LIMITATIONS.md` for the equivalent honest
accounting of why native DWG/Parasolid/ACIS support has the same
"prerequisite is a commercial license, not code" shape.
