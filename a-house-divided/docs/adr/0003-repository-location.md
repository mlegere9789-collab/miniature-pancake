# ADR 0003: Built in-place under a self-contained directory

**Status:** accepted for now.

**Decision.** The project lives under `a-house-divided/` in the repository the session was attached to, with its own workflow
(`.github/workflows/house-divided-ci.yml`, path-filtered), its own toolchain pin, and a separate `audits/` folder for reference-environment material.
No product file refers to anything outside `a-house-divided/`.

**Extraction.** The directory can move to its own repository with its history using `git subtree split --prefix=a-house-divided` (or `git filter-repo --subdirectory-filter a-house-divided`);
only the workflow's paths need adjusting. Do this before any public release, before G7.
