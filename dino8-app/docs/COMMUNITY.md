# Hosting a Dino 8 community

Dino 8 has no forum, no video channel and no hosted plug-in directory today, and a
repository cannot create an audience. What a repository **can** do is make starting one
cheap and make the pieces real. This page lists those pieces and the steps that are left
for a person with admin rights. Nothing here has been enabled remotely.

## What is checked in

| Piece | Where | What it does |
|---|---|---|
| Discussion category forms | `.github/DISCUSSION_TEMPLATE/*.yml` | When Discussions is on and a category with the matching slug exists, new posts in it get a structured form. |
| Issue forms | `.github/ISSUE_TEMPLATE/*.yml` | Plug-in listing request and plug-in bug report forms (work as soon as the files are on the default branch). |
| Static plug-in/community site | `tools/site/build_site.py` | Renders a plug-in index into a self-hosted site (no CDN, no tracker). |
| Plug-in SDK + templates | `plugin-sdk/`, `plugin-templates/`, `docs/PLUGIN_GUIDE.md` | The thing worth discussing. |
| Contribution policy for plug-ins | `CONTRIBUTING-PLUGINS.md` | How to get listed, the review checklist. |
| Tutorial storyboards | `docs/tutorial_scripts/*.txt` | Ten scripts verified to run in the app; usable as the exact on-screen steps of a video. |

Note on locations: GitHub only reads `.github/` at the repository root, so these live at
`<repo root>/.github/`, next to `.github/workflows/`, not under `dino8-app/`.

## Discussion categories (create these by hand)

GitHub cannot create Discussion categories from a file; the *forms* are in the repo, the
*categories* are created in the web UI. A form is matched to a category by the category's
**slug** (its name lower-cased with hyphens), so create categories with these exact names:

| Category name (slug) | Format | Form file |
|---|---|---|
| Q&A (`q-a`) | Question / Answer | `q-a.yml` |
| Ideas (`ideas`) | Open-ended | `ideas.yml` |
| Show and tell (`show-and-tell`) | Open-ended | `show-and-tell.yml` |
| Plugin development (`plugin-development`) | Question / Answer | `plugin-development.yml` |
| Tutorials and videos (`tutorials-and-videos`) | Open-ended | `tutorials-and-videos.yml` |

Steps for the repository owner (this was **not** done by the change that added these files):

1. Repository **Settings > General > Features > Discussions**: tick it.
2. In **Discussions**, edit categories (pencil icon): keep or rename the defaults and add the five above.
3. Pin one welcome post linking to `docs/PLUGIN_GUIDE.md`, `CONTRIBUTING-PLUGINS.md` and the hosted site.
4. Optionally add an issue-template `config.yml` contact link to the Discussions URL
   (`https://github.com/<owner>/<repo>/discussions`) - left out of the repository on purpose so
   that it does not point at a page that 404s before step 1.

Moderation basics worth deciding before announcing: a code of conduct (GitHub's *Community
standards* page offers one), who can lock and delete, and whether plug-in announcements need a label.

## Hosting the plug-in site

```sh
python3 tools/site/build_site.py --index plugin-index/index.json --out public \
    --site-url https://plugins.example.org/ \
    --discussions-url https://github.com/<owner>/<repo>/discussions
```

`public/` is plain files: copy it to any static host (GitHub Pages, an S3 bucket, nginx, a USB
stick). Users then run `PluginMarketplaceIndex https://plugins.example.org/plugin-index.json`.
Re-run the command whenever the index changes; there is no database. The generator rejects an
invalid index (same rules as the app's parser, plus unique safe ids, http(s) downloads, real
`sha256`) so a broken index never gets published. Verified properties (`tests/plugin_site_test.sh`):
no external requests in any output file, a CSP that forbids other origins, hostile index text is
escaped, and the app loads the generated `plugin-index.json`.

## Touch / phone use

There is no native companion app and no sync-back from a phone to a Dino 8 document. What exists: the
`ExportWebViewer Path=<file.html>` output is one self-contained file that now works with fingers on a
phone or tablet browser - one-finger orbit, two-finger pinch zoom, a mobile viewport, larger Scale
sliders on touch screens (Pointer Events; `src/io/WebViewer.cpp`). `tests/web_viewer_touch_test.sh`
checks the exported HTML and, where node + Playwright + Chromium are installed, drives real touch
input through `tests/web_viewer_touch_check.js`. Pan, selection and editing on touch are not implemented.

## Video tutorials

Video needs a person on camera and a channel; the repository can only prepare for it. Each of the
ten scripts in `docs/tutorial_scripts/` is run by `smoke.sh`, so the steps shown on screen are
guaranteed to work as written. Record against those, upload wherever you host video, and list the
result in the `tutorials-and-videos` category. Nothing in this repository embeds third-party video;
link to it instead, which keeps the generated site free of external requests.

## What is deliberately left to a human

* Enabling Discussions and creating the categories above.
* Choosing and paying for hosting, a domain, and DNS for the site.
* Announcing anywhere, recruiting the first plug-in authors, moderating.
* Running the review process in `CONTRIBUTING-PLUGINS.md` (there is no review team yet).
* Recording videos.
