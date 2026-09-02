# Distribution: publishing to apps.repebble.com

The Pebble appstore for faces is [apps.repebble.com/faces](https://apps.repebble.com/faces). Publishing is done through the developer portal at [developer.repebble.com/dashboard](https://developer.repebble.com/dashboard).

> Some of Core Devices' publishing docs are inherited from the original Pebble and Rebble, and a few URLs 404. The flow below is what the current tooling and 2026 blog posts describe; verify exact dashboard field names against the live portal at submission time.

## What you need first

- A **Pebble developer account** on the portal (GitHub sign-in).
- A built **`.pbw`**, the single artifact the store takes. `pebble build` produces `build/<face>.pbw`, and it bundles every platform in `targetPlatforms` (so one file covers Time 2 and the legacy watches).
- **At least one screenshot per supported platform.** For `emery`: `pebble screenshot shot.png --emulator emery`. `pebble publish` can also auto-generate these.
- A **25x25 PNG menu image** shown on the Pebble's main menu. White foreground on a transparent background (the launcher tints it). Kept at [`docs/store/menu-icon.png`](store/menu-icon.png) — the Aura "A" with its wind swish.
- A **public source-code URL** (this GitHub repo) and a **support email**.

## Three ways to publish

1. **`pebble publish`** (CLI, newest): builds, auto-generates per-platform screenshots and GIFs, and uploads. Lowest friction.
2. **CloudPebble** ([cloudpebble.repebble.com](https://cloudpebble.repebble.com)): publish straight from the browser IDE.
3. **Web dashboard** (manual): "Add a Watchface", then title, source URL and support email, then upload the screenshots and the `.pbw`, then confirm. Watchfaces need a screenshot but not a promo banner (watchapps differ).

No formal human review gate is documented; a listing goes live once the assets and `.pbw` are uploaded.

## Release checklist (per face)

- [ ] `package.json`: `displayName`, a real v4 `uuid` (`uuidgen`), `version` = `major.minor.0`, `emery` present in `targetPlatforms`, `watchapp.watchface = true`.
- [ ] `pebble build` clean; check the memory-usage report is within budget.
- [ ] Sanity-check on the `emery` emulator, plus at least the round `chalk` layout.
- [ ] Screenshot(s) captured for each supported platform.
- [ ] 25x25 PNG menu image ready (`docs/store/menu-icon.png`).
- [ ] README and source URL current; version bumped.
- [ ] Publish, then confirm the live listing renders and installs on the real watch.

**Never publish without an explicit go-ahead.** It's outward-facing and hard to walk back.

## CloudPebble and this monorepo

CloudPebble ([cloudpebble.repebble.com](https://cloudpebble.repebble.com)) syncs a GitHub repo and builds the Pebble project at the **repository root**. This repo is a monorepo: each face lives in its own subdirectory (`aura-analog/`, `aura-digital/`, `aura-essential/`) and there is no project at the root, so a direct GitHub sync just builds the first subdir it finds (`aura-analog`, alphabetically) and ignores the rest. CloudPebble has no "build this subdirectory" setting; naming the CloudPebble project after a face does not select it.

Two ways around it:

- **Per-face deploy branch (keeps GitHub sync).** Point CloudPebble at a branch whose root *is* the face, generated with the helper:

  ```bash
  scripts/sync-cloudpebble.sh essential      # -> pushes branch cloudpebble-essential
  ```

  Then set the CloudPebble GitHub branch to `cloudpebble-<face>` and pull. Re-run the helper after committing changes to that face on `main`; it force-pushes the regenerated split (the branch is a mirror, not shared work, so the force push is expected). Commit the face's changes *first* — the split only captures committed content.

- **Zip import (no GitHub).** Import `<face>-<version>-cloudpebble.zip` (built with `git archive HEAD:<face>`, so the project sits at the zip root). Builds the right face immediately; re-import to update.
