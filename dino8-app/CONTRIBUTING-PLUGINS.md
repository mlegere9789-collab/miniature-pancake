# Contributing plug-ins to Dino 8

Two ways to share a plug-in. Neither needs permission from this project:

1. **Host your own index** (recommended to start). Package the plug-in, host the library and an index
   JSON anywhere, and tell users to run `PluginMarketplaceIndex <your url>`. See
   [`docs/PLUGIN_GUIDE.md`](docs/PLUGIN_GUIDE.md) section 4 and `tools/site/` if you want a web page for it.
2. **Ask to be listed in a shared index** (the project's reference index or a community-run one) by opening
   a *Plug-in listing request* issue (form in `.github/ISSUE_TEMPLATE/`) or a pull request that adds one entry.

## Build and package

```sh
cmake -S dino8-app/plugin-sdk -B sdk-build && cmake --install sdk-build --prefix ~/dino8-sdk
python3 dino8-app/plugin-templates/new_plugin.py command --out .      # or import-export / panel
cmake -S <slug> -B <slug>/build -DCMAKE_PREFIX_PATH=~/dino8-sdk && cmake --build <slug>/build
python3 dino8-app/plugin-templates/package_plugin.py --lib <slug>/build/<slug>.so \
    --manifest <slug>/manifest.json --url https://your.host/<slug>-1.0.0.so --out dist
python3 dino8-app/tools/site/build_site.py --index my-index.json --check-only
```

## Review checklist for a listing

A maintainer (or you, before asking) checks each item:

- [ ] The entry validates: `build_site.py --index <index> --check-only` exits 0 with no `no sha256` warning.
- [ ] `sha256` equals the SHA-256 of the file at `download_url` (`sha256sum`), and the URL is https.
- [ ] Source is public and builds from a clean directory with the SDK alone.
- [ ] `api_version` is the `DINO8_PLUGIN_API_VERSION` of the header it was built with; `dino8_plugin_init`
      declines (`api->api_version < DINO8_PLUGIN_API_VERSION`) rather than assuming.
- [ ] `name` in the entry equals `dino8_plugin_name()` (that is how the app matches installed copies).
- [ ] The plug-in does what the description says, and discloses any network access, files written outside
      the document or its own folder, or processes launched.
- [ ] A licence is stated and allows redistribution of the library at the URL.
- [ ] No built-in command name is registered (the app refuses it anyway; a refused command means a broken plug-in).
- [ ] One entry per platform/architecture; ids unique and `[A-Za-z0-9._-]` only.

Signing is optional and manual today (`DigitalSign` / `VerifySignature`, see the guide's section 6);
if you sign, publish the key fingerprint with the release. The app does not enforce signatures.

## What the project does and does not promise

Listed does not mean audited. Plug-ins are native code with the user's full permissions, and this project
does not currently run a review team, a hosted index or a sandbox. A listing request can be declined or a
listing removed (for a failed checklist item, a security report, or an abandoned plug-in) by opening an issue
or pull request that removes the entry.

## Questions

Use the *Plugin development* Discussions category once Discussions is enabled ([`docs/COMMUNITY.md`](docs/COMMUNITY.md)),
otherwise an issue.
