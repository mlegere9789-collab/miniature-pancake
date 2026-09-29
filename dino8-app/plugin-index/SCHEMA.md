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
  "min_app_version": "0.1.0",   // optional, informational only (not enforced)
  "tags": ["sample", "starter"],// optional

  "dependencies": ["other-id"], // optional, ids of other entries in this
                                 // same index that must be installed first -
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
`id`. `PluginMarketplaceInstall`/`Marketplace::InstallById`
(`src/plugins/Marketplace.cpp`) resolves this list before installing the
entry itself: each dependency already satisfied by a currently-loaded
plug-in (matched by name, the same check `PluginMarketplaceCheckUpdates`
uses) is left alone, and each other is installed first, recursively
resolving its own dependencies the same way. Installing fails - with
nothing installed - if a listed id isn't in the loaded index, or if the
dependency graph cycles back on an id already being resolved.

## Compatibility

The panel and the `PluginMarketplaceList` command both show a per-entry
compatibility label:

| `api_version` vs. this build's `DINO8_PLUGIN_API_VERSION` | Label |
|---|---|
| `<=` (including `0`/absent, treated as unknown) | absent → "Unknown"; otherwise "Compatible" |
| `>` | "Needs newer Dino 8" - Install refuses it |

## Writing your own index

An index is just a JSON file - host it anywhere reachable over HTTPS (or
ship it locally, e.g. alongside a studio's internal plug-in collection) and
point the marketplace panel at it. There is no registration, no signing
authority, and no central catalogue this repository controls - matching the
plug-in ABI's own "no licence, no accounts, no network" starting point
(`include/dino8_plugin.h`); a marketplace here is one *possible* way to
distribute a `download_url`-based plug-in, not a requirement.
