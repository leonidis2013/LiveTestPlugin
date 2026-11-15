#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2026 Jerod Ulicki
"""Build helpers for GitHub Actions (also usable locally).

  ci.py targets <shard>             -> writes ids= and targets= to $GITHUB_OUTPUT (or prints them)
  ci.py package <shard> <macOS|Windows> <build dir> <dist dir>
  ci.py manifest <dist dir>         -> dist/manifest.json, read by the LiveTest Updater
  ci.py notes <dist dir>            -> dist/notes.md (release notes)
"""
import datetime
import json
import os
import pathlib
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONFIG = json.loads((ROOT / "plugins.json").read_text(encoding="utf-8"))
PLATFORMS = {"macOS": "mac", "Windows": "windows"}


def plugins_in(shard):
    return [p for p in CONFIG["plugins"] if shard in ("all", p["shard"])]


def asset_name(pid, os_name):
    return f"LiveTest-{pid}-{os_name}.zip"


def updater_asset(os_name):
    return f"LiveTestUpdater-{os_name}.zip"


def write_output(**kv):
    out = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{k}={v}" for k, v in kv.items()]
    if out:
        with open(out, "a", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    print("\n".join(lines))


def zip_dir(src: pathlib.Path, dest: pathlib.Path, os_name: str):
    """Zip a bundle folder (keeping the folder itself at the top of the zip)."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        dest.unlink()
    if os_name == "macOS":
        # ad-hoc sign the whole bundle so Apple Silicon hosts accept it, then zip with ditto
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(src)], check=True)
        subprocess.run(["ditto", "-c", "-k", "--keepParent", str(src), str(dest)], check=True)
        return
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED) as z:
        if src.is_file():
            z.write(src, src.name)
            return
        for path in sorted(src.rglob("*")):
            z.write(path, str(path.relative_to(src.parent)))


def package(shard, os_name, build, dist):
    build, dist = pathlib.Path(build), pathlib.Path(dist)
    if shard == "updater":
        art = build / "Updater" / "LiveTestUpdater_artefacts" / "Release"
        src = art / ("LiveTest Updater.app" if os_name == "macOS" else "LiveTest Updater.exe")
        if not src.exists():
            sys.exit(f"::error::{src} is missing")
        zip_dir(src, dist / updater_asset(os_name), os_name)
        print(f"packaged {updater_asset(os_name)}")
        return
    for p in plugins_in(shard):
        src = build / f"LT_{p['id']}_artefacts" / "Release" / "VST3" / f"{p['name']}.vst3"
        if not src.exists():
            sys.exit(f"::error::{src} is missing")
        zip_dir(src, dist / asset_name(p["id"], os_name), os_name)
        print(f"packaged {asset_name(p['id'], os_name)}")


def manifest(dist):
    dist = pathlib.Path(dist)
    sha = os.environ.get("GITHUB_SHA", "")[:7]
    data = {
        "suiteVersion": CONFIG["suiteVersion"],
        "built": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "commit": sha,
        "updater": {"version": CONFIG["updaterVersion"], "assets": {}},
        "plugins": [],
    }
    for os_name, key in PLATFORMS.items():
        if (dist / updater_asset(os_name)).exists():
            data["updater"]["assets"][key] = updater_asset(os_name)
    for p in CONFIG["plugins"]:
        assets = {key: asset_name(p["id"], os_name) for os_name, key in PLATFORMS.items()
                  if (dist / asset_name(p["id"], os_name)).exists()}
        data["plugins"].append({k: p[k] for k in ("id", "name", "version", "family", "sq", "modeledOn")} | {"assets": assets})
    (dist / "manifest.json").write_text(json.dumps(data, indent=2), encoding="utf-8")
    print(json.dumps(data, indent=2))


def notes(dist):
    dist = pathlib.Path(dist)
    sha = os.environ.get("GITHUB_SHA", "")[:7]
    msg = subprocess.run(["git", "log", "-1", "--pretty=%s"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    rows = "\n".join(f"| {p['name']} | {p['version']} | {p['family']} | {p['sq']} |" for p in CONFIG["plugins"])
    text = f"""**LiveTest {CONFIG['suiteVersion']}** - built {datetime.datetime.now(datetime.timezone.utc):%Y-%m-%d %H:%M UTC} from `{sha}`: {msg}

### Easiest: the LiveTest Updater
Download **LiveTestUpdater-macOS.zip** or **LiveTestUpdater-Windows.zip**, unzip and open it. It lists every plugin with the
installed and latest version: tick what you want (or press **Updates**) and click **INSTALL / UPDATE**. One password / UAC
prompt installs the whole batch into the system VST3 folder and removes old copies. Quit your host first, rescan after.

- **Mac, first launch:** macOS will say it can't verify the app. Click **Done**, open **System Settings > Privacy & Security**,
  click **Open Anyway** next to LiveTest Updater, then open it again. (Needed once per download until it's signed with an Apple Developer ID.)
- **Windows, first launch:** if **Windows protected your PC** appears, click **More info > Run anyway**.

### Manual install
Each plugin is also here as **LiveTest-&lt;name&gt;-macOS.zip / -Windows.zip**: unzip and copy the `.vst3` folder into
`/Library/Audio/Plug-Ins/VST3` (Mac) or `C:\\Program Files\\Common Files\\VST3` (Windows).

| Plugin | Version | Type | SQ model |
|---|---|---|---|
{rows}
"""
    (dist / "notes.md").write_text(text, encoding="utf-8")
    print(text)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "targets":
        shard = sys.argv[2]
        if shard == "updater":
            write_output(ids="", targets="LiveTestUpdater", plugins="OFF", updater="ON")
        else:
            ps = plugins_in(shard)
            write_output(ids=";".join(p["id"] for p in ps), targets=" ".join(f"LT_{p['id']}_VST3" for p in ps),
                         plugins="ON", updater="OFF")
    elif cmd == "package":
        package(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif cmd == "manifest":
        manifest(sys.argv[2])
    elif cmd == "notes":
        notes(sys.argv[2])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
