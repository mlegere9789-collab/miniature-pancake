# {{cookiecutter.plugin_name}}

{{cookiecutter.description}}

Generated from the Dino 8 `import/export` plug-in template. It builds against the
Dino 8 plug-in SDK only - no Dino 8 source tree needed.

```sh
# 1. get the SDK once (from a Dino 8 source checkout; skip if you already have one):
cmake -S <dino8>/dino8-app/plugin-sdk -B sdk-build && cmake --install sdk-build --prefix ~/dino8-sdk
# 2. build this plug-in:
cmake -S . -B build -DCMAKE_PREFIX_PATH=~/dino8-sdk
cmake --build build
# 3. load it: copy build/{{cookiecutter.plugin_slug}}.so (.dll / .dylib) into Dino 8's
#    <config>/plugins folder, or run   GrasshopperFolders add <folder containing it>
# 4. try it:
#    MyXyzImport points.txt   /   (select objects)  MyXyzExport out.txt
```

* `src/plugin.c` - the plug-in.
* `manifest.json` - the Package Manager manifest (name/version/author/description).
* `index-entry.json` - a marketplace index entry to paste into your own plug-in index.
* Package it for distribution with `plugin-templates/package_plugin.py`; the
  full story (versioning, listing, review) is in `docs/PLUGIN_GUIDE.md`.
