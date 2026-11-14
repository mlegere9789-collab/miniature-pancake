# Dino 8 Plug-in Guide (for third parties)

This is the practical companion to [`PLUGIN_SDK.md`](PLUGIN_SDK.md) (which walks
through the C API with the four bundled example plug-ins). It answers: how do I
build a plug-in **outside the Dino 8 source tree**, what can break between
versions, how do users find and install it, and what review and trust there is
today - including what there is **not**.

Everything below that says "tested" is exercised by `tests/plugin_sdk_test.sh`
and `tests/plugin_site_test.sh`, which `tests/smoke.sh` runs (Linux/macOS).

## 1. Quick start (10 minutes, no Dino 8 source tree needed after step 1)

```sh
# 1. Install the SDK once. It is ONE header plus a CMake package.
cmake -S dino8-app/plugin-sdk -B sdk-build
cmake --install sdk-build --prefix ~/dino8-sdk
#    (from an app build tree instead:  cmake --build <build> --target dino8-plugin-sdk
#     stages it in <build>/dino8-plugin-sdk)

# 2. Generate a plug-in project from a template.
python3 dino8-app/plugin-templates/new_plugin.py command --out . \
    --set plugin_name="Hello Tools" --set plugin_slug=hello_tools --set command_name=HelloTools

# 3. Build it - the project depends on nothing but the SDK.
cmake -S hello_tools -B hello_tools/build -DCMAKE_PREFIX_PATH=~/dino8-sdk
cmake --build hello_tools/build

# 4. Load it: copy hello_tools/build/hello_tools.so (.dll / .dylib) into Dino 8's
#    <config>/plugins folder (see "Where plug-ins load from"), or in Dino 8 run
#      GrasshopperFolders add /path/to/hello_tools/build
#    then:  GrasshopperPluginList     and     HelloTools 1 2 3
```

If you use the real [`cookiecutter`](https://cookiecutter.readthedocs.io/) tool,
`cookiecutter dino8-app/plugin-templates/command` works too - `new_plugin.py` is
only a stdlib-only renderer of the same templates.

### The three templates

| Template | What it gives you | Honest scope |
|---|---|---|
| `command` | One command (`MyCommand [x y z]`) that adds a point and reports it. | The smallest useful plug-in. |
| `import-export` | `<Name>Import <file>` reads `x y z` lines and adds points; `<Name>Export <file>` writes the bounding-box centre of every selected object. | These are **commands**, not entries in File > Import/Export: the plug-in ABI has no file-format registration hook. Replace the text parsing with your format. |
| `panel` | `<Name>` prints a table about the selection (id, size, box volume, totals) in the command history, plus a Dino Flow node. | **Plug-in API v2 has no UI-widget/ImGui hook**, so there is no dockable panel. The command history is the only text surface a plug-in has; this is the supported way to present a panel-like report today. A real panel would be an ABI addition (section 3). |

Each generated project contains `src/plugin.c`, `CMakeLists.txt`,
`manifest.json` (Package Manager manifest), `index-entry.json` (a marketplace
index entry with `REPLACE-ME` placeholders that the site generator refuses to
publish) and a README.

## 2. What is in the SDK

`plugin-sdk/` (source) installs, with nothing else:

```
include/dino8-plugin-sdk/dino8_plugin.h            the ABI header (byte-identical to include/dino8_plugin.h)
share/cmake/Dino8PluginSdk/Dino8PluginSdkConfig.cmake
share/cmake/Dino8PluginSdk/Dino8PluginSdkConfigVersion.cmake
share/cmake/Dino8PluginSdk/Dino8PluginSdkMacros.cmake
```

Use it from CMake:

```cmake
find_package(Dino8PluginSdk 2 REQUIRED)       # sets Dino8PluginSdk_API_VERSION, defines Dino8::PluginSdk
dino8_add_plugin(my_plugin SOURCES src/plugin.c)   # SHARED, no "lib" prefix, .so/.dll/.dylib, hidden symbols
```

Without CMake: compile your sources as a shared library with `-I<prefix>/include/dino8-plugin-sdk`
and export `dino8_plugin_init` (the macro `DINO8_PLUGIN_EXPORT` does that). Any language
that can export a C symbol works.

The SDK version **is** the plug-in API version: this SDK is `2.0.0` because
`DINO8_PLUGIN_API_VERSION` is 2 (CMake reads it from the header, so the two cannot
drift). `find_package(Dino8PluginSdk 2)` is satisfied by SDK 2 or newer;
`find_package(Dino8PluginSdk 99)` fails (tested).

## 3. ABI versioning and compatibility rules

The contract, from `include/dino8_plugin.h`:

1. **The ABI is plain C**: function pointers in `Dino8PluginApi`, POD structs. No C++ types,
   no kernel types, no exceptions cross the boundary. Geometry crosses as opaque 64-bit
   handles valid only during one evaluator call.
2. **`DINO8_PLUGIN_API_VERSION` only ever increases, and changes are additive**: new
   callbacks are appended to `Dino8PluginApi` (consuming the `reserved[]` slots), new
   enum values are added, existing members never move, change meaning or disappear.
   Version 2 added the geometry kinds and helpers on top of version 1.
3. **The rule a plug-in follows**: a plug-in built against API version *N* works on any
   Dino 8 whose `api->api_version` is *>= N*. So in `dino8_plugin_init`:

   ```c
   if (!api) return 1;
   if (api->api_version < DINO8_PLUGIN_API_VERSION) return 2;   /* host too old: decline */
   ```

   Use `<`, not `!=` (the bundled `HelloDino` uses `!=`, which would also reject a *newer*
   host that is perfectly able to run it; do not copy that). The templates use `<`.
4. **Never touch a field beyond what your header declares**, and never call a callback
   pointer without checking it is non-NULL if you rely on something newer than the
   oldest host you support (the v2 geometry helpers are NULL on a v1 host).
5. **Declining is safe**: a non-zero return from `dino8_plugin_init` unloads the library
   and lists it as `failed: dino8_plugin_init returned N` in `GrasshopperPluginList`.
   Print a reason first through `api->print`.

Tested (`psdk_abi_*`): a plug-in built against a header claiming API v3 declines on this v2
host with exit code 2 and a visible message; a plug-in built against a v1 header loads and
runs on this v2 host.

What the **host** checks at load time: only that the library opens, exports
`dino8_plugin_init`, and that init returns 0. It does *not* itself compare API versions at
load time - the plug-in's own check above is what protects a host/plug-in mismatch for
hand-copied libraries. The **marketplace** does check (section 4).

Marketplace-level versioning, in the index entry: `api_version` (the API the library was
built against; Install refuses an entry newer than the running build), `min_app_version`
(dotted-numeric compare against the running Dino 8), `version` (the plug-in's own version,
compared numerically for `PluginMarketplaceCheckUpdates`/`UpdateAll`) and `dependencies`
(`"id"` or `"id@min_version"`). Details: [`../plugin-index/SCHEMA.md`](../plugin-index/SCHEMA.md).

**Compatibility promise for maintainers**: removing or re-ordering anything in
`Dino8PluginApi` or `Dino8FlowValue` requires a new *major* ABI and is not allowed under
the additive rule; if it is ever needed the new header ships under a new file name so v2
plug-ins keep building. Adding a panel/file-format hook later is a normal additive change
(append to the struct, bump `DINO8_PLUGIN_API_VERSION`, bump the SDK version, update the
templates' minimum).

## 4. Discovery and the marketplace

### Where plug-ins load from

Dino 8 loads every library whose suffix matches the platform (`.so`/`.dll`/`.dylib`) from:

1. `<config>/plugins` - the per-user folder (`~/.config/dino8/plugins` on Linux,
   `%APPDATA%\Dino8\plugins` on Windows, `~/Library/Application Support/Dino8/plugins` on macOS),
2. `plugins/` next to the executable (the four bundled examples live here),
3. any folder added for the session with `GrasshopperFolders add <folder>` (not persisted).

`GrasshopperPluginList` shows each plug-in, its version and how many commands and flow nodes
it registered (or why it failed). `GrasshopperIgnorePlugin <file>` skips one.
The **Package Manager** panel installs a folder containing `manifest.json`
(`name`, `version`, `author`, `description`) plus a library/`.lua`/`.dflow` by copying it into
`<config>/plugins`; `plugin-templates/package_plugin.py` writes exactly that layout.

### The marketplace = a JSON index you host

There is no central server and no registration. A marketplace is a JSON file in the format of
[`../plugin-index/SCHEMA.md`](../plugin-index/SCHEMA.md) (`schema_version: 1`). Anyone can host one;
users point Dino 8 at it:

```
PluginMarketplaceIndex https://plugins.example.org/plugin-index.json     (or a local path)
PluginMarketplaceList [filter]       PluginMarketplaceInstall <id>
PluginMarketplaceCheckUpdates        PluginMarketplaceUpdateAll
PluginMarketplaceVerify <id>         PluginMarketplaceUninstall <id>
```

`PluginMarketplaceIndex` with no argument loads the bundled reference index
(`data/plugin-index/index.json`: the four example plug-ins, which `bundled_path` resolves next to the
executable). `Window > Plug-in Marketplace` is the same thing with a table, filter box,
install/update/verify buttons, and a **local-only** ratings/reviews store
(`<config>/plugin_reviews.json`, no server - a rating reflects only that machine).

How a third-party listing works, end to end (all tested over a localhost HTTP server in
`psdk_package_*`/`psdk_market_*`):

1. Build and test your plug-in; `python3 plugin-templates/package_plugin.py --lib build/x.so --manifest manifest.json --url https://host/x-1.0.0.so --out dist`
   produces the package folder and an index entry carrying the library's real SHA-256.
2. Host the library at that URL and add the entry to your index's `plugins` array (or send it to a
   shared index, section 5).
3. Install downloads the file (via `curl`), refuses it if `sha256` is present and differs
   (`psdk_market_sha_refused`), refuses a `library_filename` that is not a plain file name
   (path traversal is blocked), copies it to `<config>/plugins`, loads it, resolves `dependencies`.
4. `PluginMarketplaceVerify <id>` re-hashes the installed copy on demand.

`sha256` is **optional** in the index. An entry without it installs with no integrity check;
`tools/site/build_site.py` warns about that, and the review checklist below requires it.

### Publishing a browsable site

`tools/site/build_site.py` turns an index into a static, self-hostable site (searchable list, one page per
plug-in with the exact install commands, a community page, and `plugin-index.json` to point
`PluginMarketplaceIndex` at). No external CDN, no tracker, no forms, strict
Content-Security-Policy, all index data HTML-escaped. See `tools/site/README.md` and
[`COMMUNITY.md`](COMMUNITY.md).

## 5. Review process

The app enforces **no** review: it will install anything an index lists. Trust is therefore a
property of the index you choose to load. For the shared/reference index the project proposes
the checklist in [`../CONTRIBUTING-PLUGINS.md`](../CONTRIBUTING-PLUGINS.md); a listing is a pull request
that adds one entry to `plugin-index/`-style JSON, and a maintainer verifies, in this order:

1. `python3 tools/site/build_site.py --index <index> --check-only` passes (schema, unique ids, http(s)
   download - https preferred, real `sha256`).
2. The source is public, builds from a clean directory with the SDK alone, and the library at
   `download_url` hashes to the published `sha256` (rebuild reproducibility is requested, not required).
3. `api_version`/`min_app_version` are truthful (declared version is what it was built against; the
   plug-in declines on an older host).
4. Behaviour matches the description; no network access, file writes outside the document/user
   folders, or process launches unless the description discloses them.
5. A licence is stated; the plug-in does not shadow built-in command names (the app refuses that at
   registration: `register_command` returns non-zero).

Honest limits: the review is human and the project does not currently operate a review team, a hosted
index, a CA, or sandboxing - plug-ins are native code running with the user's full permissions. Treat a
third-party index like any other software source.

## 6. Signing: what exists today

Read this before claiming more than it says:

* **Not implemented**: the app does **not** verify any signature when loading or installing a
  plug-in, and the marketplace has no signature field. A package-manifest signature is not checked
  by anything. Dino 8's own installers are also unsigned ([`CODE_SIGNING.md`](CODE_SIGNING.md)).
* **Integrity, not authenticity**: the index `sha256` proves the downloaded bytes equal what the index
  author listed. Whoever controls the index controls the hash.
* **What does exist**: the `DigitalSign <file>` and `VerifySignature <file>` commands
  (`src/io/DigitalSignature.h`) sign **any file** with a per-machine RSA-2048 / SHA-256 / PKCS#1 v1.5
  key (generated on first use in `<config>/signing_key.json`) and write a `<file>.sig` sidecar containing
  the public key and signature. `VerifySignature` checks a file against its sidecar and prints the signer's
  public-key **fingerprint**. There is no certificate authority: a valid signature only says "the holder of
  this key signed these exact bytes"; identity comes from comparing the fingerprint to one the author
  published somewhere you already trust.
* **Recommended practice with the current tools** (tested in `psdk_sign_*`): sign your `manifest.json`
  (and/or the library), ship the `.sig` next to it, and publish the fingerprint on your project page.
  A user runs `VerifySignature <manifest.json>` and compares the fingerprint; editing the file afterwards
  makes verification report `FAILED`. This is a manual, opt-in check - nothing enforces it.

Wiring signature checks into `Marketplace::InstallEntry` (a `signature`/`signer_fingerprint` field and a
trust list) would be a sensible next step; it is not claimed or built here.

## 7. Packaging layout

```
my_plugin-1.0.0/
  manifest.json          {"name","version","author","description"}   (Package Manager)
  my_plugin.so           (.dll / .dylib per platform; one folder per platform)
  manifest.json.sig      optional, from DigitalSign
my_plugin-1.0.0.index-entry.json     paste into your index's "plugins" array
```

Ship one library per platform/architecture (a `.so` does not load on Windows) and list each as its own
entry (for example ids `my_plugin-linux-x64`, `my_plugin-win-x64`); the index has no per-platform
selector today. Keep the plug-in's `dino8_plugin_name()` identical to the entry's `name` - that is how
the app matches an installed plug-in to its index entry for "Installed"/update checks.
