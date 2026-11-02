# Plug-in index schema (version 1)

A plug-in index is one JSON file describing a set of installable Dino 8
plug-ins (see [`../docs/PLUGIN_SDK.md`](../docs/PLUGIN_SDK.md) for what a
plug-in itself is). The Plug-in Marketplace panel (`Window > Plug-in
Marketplace`, or the `PluginMarketplace` command) loads one from a local
path or an `http(s)://` URL, lists what it finds, and can install any entry
straight into `<config>/plugins`. The parser lives in
[`../src/plugins/Marketplace.cpp`](../src/plugins/Marketplace.cpp)
(`ParseIndex`); [`index.json`](index.json) in this folder is a real,
loadable reference index built from the four sample plug-ins that ship in
this repository (see `plugins/sample`, `plugins/mesh_tools`,
`plugins/curve_tools`, `plugins/analysis_tools`).

## Loading the bundled reference index without a path

`index.json` in this folder is also copied to `data/plugin-index/index.json`
next to the built executable (see `CMakeLists.txt`'s `POST_BUILD` copy step
and its `install()` rules for each platform) and installed there too, so it
ships as a real runtime asset, not just a source-tree fixture.
`Application::DefaultMarketplaceIndexPath` (`src/app/Application.h/.cpp`)
resolves it with the same build-tree/install-layout/macOS-bundle search order
already used for `data/commands.json` and `data/i18n`. Running
`PluginMarketplaceIndex` with **no argument** loads that default; the panel's
"Load Bundled Index" button does the same with one click. Either way this is
still just *pointing the existing loader at a well-known path* - there is no
separate code path, no built-in registry, and no network fetch involved
(the two "must supply a path or URL by hand" caveats a plain
`PluginMarketplaceIndex <path>`/URL and the reference index in
`../docs/PLUGIN_SDK.md`'s discussion of this still apply to any *other*
index).

## Top level

```jsonc
{
  "schema_version": 1,          // required, must be exactly 1
  "index_name": "My Index",     // optional, shown in the panel
  "updated": "2026-09-28",      // optional, free-form (shown as-is)
  "plugins": [ /* array of entries, see below */ ]
}
```

`schema_version` must be present and equal to `1` - any other value (or a
missing/non-numeric one) is rejected outright, so a future breaking format
change can introduce `schema_version: 2` without an old build silently
misreading it.

## Plugin entry

```jsonc
{
  "id": "hellodino",            // required, unique, used by Install
  "name": "HelloDino",          // required, display name
  "version": "1.0.0",           // required
  "description": "...",         // optional but strongly recommended
  "author": "...",              // optional
  "homepage": "https://...",    // optional
  "min_app_version": "0.1.0",   // optional: the lowest Dino 8 version this
                                 // plug-in needs. Compared numerically
                                 // (like "api_version" below) against this
                                 // build's own version; Install refuses an
                                 // entry that needs a newer one, same as it
                                 // already refuses an api_version too new.
  "tags": ["sample", "starter"],// optional

  "dependencies": ["other-id", "another-id@1.2.0"], // optional, ids of
                                 // other entries in this same index that
                                 // must be installed first, each optionally
                                 // requiring a minimum version via "@" -
                                 // see "Dependencies" below

  "api_version": 2,             // the DINO8_PLUGIN_API_VERSION this plug-in
                                 // targets (see include/dino8_plugin.h). The
                                 // ABI is additive, so any api_version <=
                                 // this build's DINO8_PLUGIN_API_VERSION is
                                 // expected to load; a higher one means this
                                 // build is too old to run it, and Install
                                 // refuses it up front.

  // Exactly one of the next two fields is required - it says where the
  // plug-in's shared library comes from:
  "download_url": "https://example.com/hellodino.so",
  //   -- OR --
  "bundled_path": "plugins/hello_dino",

  "sha256": "…",                 // optional, only meaningful with
                                  // download_url: the expected SHA-256 of
                                  // the downloaded file, hex, lower or upper
                                  // case. Install refuses to load a file
                                  // whose hash does not match.
  "library_filename": "hellodino.so"  // optional override for the filename
                                       // Install writes into <config>/plugins
}
```

### `download_url` vs `bundled_path`

- **`download_url`** is for a plug-in fetched from the network: an
  `http://` or `https://` URL to the shared library's raw bytes. Install
  downloads it (via `curl`) to a temp file, verifies `sha256` if the entry
  gives one, then copies it into `<config>/plugins`. This is real,
  general-purpose code - it is simply never exercised by this repository's
  own automated tests, because nothing in this codebase hosts a plug-in
  library anywhere on the network to fetch (see the honesty note in the
  PR/commit description).
- **`bundled_path`** is for a plug-in that already ships next to the Dino 8
  executable - exactly the four sample plug-ins in [`index.json`](index.json).
  It is a path *relative to the app's own executable directory*, **without**
  the platform's `.so`/`.dll`/`.dylib` suffix (Install appends the right one
  for the platform it's running on). Every sample plug-in's own
  `CMakeLists.txt` already copies its built library to
  `<exe_dir>/plugins/<name>.{so,dll,dylib}` as part of a normal build (see
  e.g. `plugins/sample/CMakeLists.txt`), so `bundled_path: "plugins/hello_dino"`
  resolves to a real file the moment this project is built - no network, no
  placeholder, no separately-hosted binary.

Either way, Install's target is the same: a copy of the library lands in
`<config>/plugins` and is loaded through `plugins::Manager::LoadFile`
(`src/plugins/PluginManager.cpp`) - the identical path a plug-in dropped
into that folder by hand, or installed through the older local-folder
Package Manager, already takes. A plug-in installed from the marketplace
shows up in the Plug-in Manager panel and in `GrasshopperPluginList` like
any other.

## Dependencies

An entry's `dependencies` list names other entries **in the same index** by
`id`, each optionally suffixed `@min_version` (e.g. `"meshtools@1.2.0"`) to
require at least that version of it - a bare id (`"meshtools"`) accepts
whatever version is available. `PluginMarketplaceInstall`/
`Marketplace::InstallById` (`src/plugins/Marketplace.cpp`,
`SplitDependencySpec` in `MarketplaceIndex.cpp`) resolves this list before
installing the entry itself: a dependency already satisfied by a
currently-loaded plug-in (matched by name, the same check
`PluginMarketplaceCheckUpdates` uses) whose version meets the constraint (if
any) is left alone; one that's loaded but *older* than `min_version` is
upgraded in place - installed fresh from the loaded index, same as if it
weren't installed at all; one not loaded at all is installed the same way,
recursively resolving its own dependencies first. Installing fails - with
nothing installed - if a listed id isn't in the loaded index, the
dependency graph cycles back on an id already being resolved, or the loaded
index's own version of a dependency is itself older than `min_version` (a
real version conflict: no install or upgrade can satisfy the requirement).
The panel's own Install/Update button goes through `Marketplace::InstallById`
too, so clicking it in the UI resolves dependencies exactly like the
command does.

`PluginMarketplaceList` prints a `- requires a, b` suffix for any entry
with dependencies (even one naming an id missing from the index, since that
is exactly what would make installing it fail); the panel's detail view
shows the same list as `Requires: A (installed), B (not installed)`,
resolving each id to its display name and current install status, with
`>=min_version` shown next to a constrained entry and `(installed X, too
old)` when the loaded copy doesn't meet it.

`PluginMarketplaceUninstall`/`Marketplace::UninstallById` reverses this: it
removes the `<config>/plugins` copy it finds for the requested id, then
walks that same `dependencies` list and removes any dependency that no
other currently-installed entry in the loaded index still lists as a
dependency, recursively. A dependency still needed by some other installed
entry is left in place. This only protects a dependency uninstalled as part
of that cascade, though - uninstalling an id *directly* that some other
still-installed entry declares as its own dependency is allowed (the
marketplace doesn't lock a shared dependency in place), but is reported: both
the command and the panel print a warning naming every such dependent, since
it may now be broken.

`PluginMarketplaceList` (and the panel's own filter box) both take an
optional filter: text matched case-insensitively as a substring against an
entry's id, name, author, description, or any tag - e.g.
`PluginMarketplaceList mesh` lists only entries mentioning "mesh"
somewhere. An empty filter (the default) lists everything.

## Compatibility

The panel and the `PluginMarketplaceList` command both show a per-entry
compatibility label:

| `api_version` vs. this build's `DINO8_PLUGIN_API_VERSION` | Label |
|---|---|
| `<=` (including `0`/absent, treated as unknown) | absent → "Unknown"; otherwise "Compatible" |
| `>` | "Needs newer Dino 8" - Install refuses it |

`min_app_version`, if the entry gives one, is checked the same way against
this build's own version (numerically, via the same comparison
`CompareVersions` uses for `CheckForUpdate`): a build older than
`min_app_version` also shows "Needs newer Dino 8" and Install also refuses
it. `api_version` is checked first, so an entry that fails both checks is
reported for its `api_version` mismatch.

That "Needs newer Dino 8" panel badge is deliberately
generic (it has to fit a table cell); `CompatibilityReason` (`MarketplaceIndex.h`/
`.cpp`) spells out exactly what's missing - e.g. "needs plug-in API v3, this
build only supports up to v2", or "needs Dino 8 2.0.0 or newer, this build is
1.4.0" - the same wording `InstallEntry`'s own refusal already used, just
available before ever attempting (and failing) an install. `PluginMarketplaceList`
prints it in place of the old generic label for an incompatible entry; the
panel shows it as a tooltip on the compatibility badge and on a disabled
Install/Update button, and inline in the selected entry's detail view.

## Installing everything at once

`PluginMarketplaceInstallAll`/`Marketplace::InstallAll` installs every entry
in the loaded index that isn't already matched to a loaded plug-in (the same
`FindInstalled` check the panel's own "Installed" column and Install/Update
button already use per row), one at a time through the same `InstallById` a
single row's Install button uses - so a freshly loaded (or freshly
switched-to) index can be brought in wholesale, resolving each entry's own
dependencies normally, instead of clicking Install once per row. An entry
already pulled in earlier in the same batch as another entry's dependency is
skipped rather than reinstalled. One entry failing (an incompatible entry, an
unresolved dependency, …) does not stop the rest of the batch from being
attempted; the command prints which ids installed and warns about any that
didn't, and the panel's "Install All (`N`)" button (next to the filter box,
disabled when nothing is left to install) does the same.

## Updating everything at once

`PluginMarketplaceCheckUpdates`/`Marketplace::CheckForUpdates` only reports
what's out of date; `PluginMarketplaceUpdateAll`/`Marketplace::UpdateAll`
(`src/plugins/Marketplace.cpp`) actually installs every one of those updates,
one at a time through the same `InstallById` a single row's Update button
uses - so each resolves its own dependencies normally, and an entry whose
dependency was itself just upgraded earlier in the same batch sees that
upgrade rather than a stale version. One entry failing (a newly-introduced
version conflict, a compatibility refusal, …) does not stop the rest of the
batch from being attempted; the command prints which ids updated and warns
about any that didn't, and the panel's "Update All (`N`)" button (next to the
filter box, disabled when nothing is out of date) does the same.

## Verifying an installed copy

`PluginMarketplaceVerify <id>`/`Marketplace::VerifyInstalled` re-hashes the
file currently sitting at that entry's own `<config>/plugins` destination and
compares it against the loaded index entry's `sha256` - the identical check
`InstallEntry` makes before ever writing the file, just re-run on demand
against what is actually on disk right now. This catches a copy that was
corrupted or edited after installing without requiring a reinstall to find
out; it reports "no hash to check" (not a failure) for a `bundled_path` entry
or one whose index simply doesn't supply a `sha256`, since there's nothing to
compare in that case. The panel's detail view offers a "Verify" button next
to Requires:, enabled once the selected entry is installed.

`PluginMarketplaceVerifyAll`/`Marketplace::VerifyAll` runs that same check
once for every entry currently installed via the marketplace, in one call -
the batch counterpart to a single `PluginMarketplaceVerify <id>`, the same
way `PluginMarketplaceUpdateAll` batches a single entry's Update. An entry
never installed via the marketplace is simply left out of the result, not
reported as "not installed" - there is nothing installed there to check. The
panel's "Verify All" button sits next to "Update All", disabled when nothing
is currently installed (a cheap check - it does not itself hash anything, so
it is safe to evaluate every frame the panel is open).

## Uninstalling everything at once

`PluginMarketplaceUninstallAll`/`Marketplace::UninstallAll` uninstalls every
entry in the loaded index currently installed via the marketplace, one at a
time through the same `UninstallById` a single row's Uninstall button already
uses - so each target cascades its own now-unneeded dependencies exactly like
uninstalling it by hand would. An id an earlier target's own cascade already
removed is skipped rather than reattempted (which would otherwise surface as
a spurious failure, since by its own turn it is no longer installed at all).
The panel's "Uninstall All" button sits next to "Verify All", disabled under
the same "nothing installed" condition.

## Writing your own index

An index is just a JSON file - host it anywhere reachable over HTTPS (or
ship it locally, e.g. alongside a studio's internal plug-in collection) and
point the marketplace panel at it. There is no registration, no signing
authority, and no central catalogue this repository controls - matching the
plug-in ABI's own "no licence, no accounts, no network" starting point
(`include/dino8_plugin.h`); a marketplace here is one *possible* way to
distribute a `download_url`-based plug-in, not a requirement.
