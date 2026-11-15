# LiveTest

VST3 plugins that model the **DEEP processing** and **RackExtra FX** on an Allen & Heath SQ,
so you can try processing settings and signal flow in LiveProfessor (or any VST3 host) away
from the desk. There are 21 separate plugins, each named **LiveTest …**, and the **LiveTest
Updater** app installs and updates them.

Each plugin uses the same controls, ranges and processing order as the SQ model it stands in
for. Thresholds read in **dBu, the same as the desk** (the SQ aligns +18 dBu to 0 dBFS, so
0 dBu = -18 dBFS). The sound is modeled on how the classic hardware behind each SQ model
works, using public information. Allen & Heath doesn't publish its DSP, so these are close
equivalents, not bit-exact copies. Settings carry over to the desk well, but expect small
differences in tone. For the exact sound, play a multitrack through the SQ-5 itself (USB
virtual soundcheck).

## The plugins

| Plugin | SQ model | Modeled on |
|---|---|---|
| LiveTest Opto Comp | Opto | Filament, electro-luminescent and LED optical compressors (Transient and Burn recovery) |
| LiveTest FET 76 | Peak Limiter 76 | Late-1960s FET limiting amplifier: 4/8/12/20/ALL, silver and black units, Gain Link |
| LiveTest VCA 16T | 16T | Workhorse VCA true-RMS live compressor |
| LiveTest VCA 16VU | 16VU | Vintage VCA RMS compressor (thicker low mids when driven) |
| LiveTest Bus Comp | Bus | British console VCA bus compressor (stepped controls, auto release) |
| LiveTest Tube Opto | OptTronik | Tube electro-optical leveling amplifier (feedback design, units A and B, Emphasis) |
| LiveTest Punch Comp | Mighty | Transistor-array VCA compressor (two detector voices) |
| LiveTest Distort Comp | CompStortion | Multi-mode compressor with ratio modes, Smash, Brit and distortion A-D |
| LiveTest Peak Comp | Manual Peak | Clean peak compressor (smoothing, auto hold) |
| LiveTest RMS Comp | Manual RMS | Clean RMS compressor |
| LiveTest Dyn8 | Dyn8 | 4-band dynamic EQ + 4-band multiband compressor |
| LiveTest Reverb | SMR Reverb | Classic, Hall, Room and EMT plate, with all expert pages |
| LiveTest Gated Verb | Gated Verb | Non-linear reverb: Classic, Panner, Powerbox |
| LiveTest Tap Delay | Stereo Tap Delay | Clean stereo delay: BPM/MS, tap, ping-pong or scatter |
| LiveTest ADT | ADT | Tape double-tracking: double/quad, wide, autopan |
| LiveTest Chorus | Chorus | Stereo chorus: LFO split, wide, multivoice, autopan |
| LiveTest Ensemble | Symphonic Chorus | String-ensemble chorus |
| LiveTest Flanger | Flanger | Ambient, Vintage (tape) and Wild flangers |
| LiveTest Phaser 12 | MOO 12 Stage Phaser | 12-stage analogue synth phaser |
| LiveTest BBD Delay | Bucket Brigade | Bucket-brigade analogue delay (band-limiting, compander noise, Dist) |
| LiveTest Tape Echo | Echo | Three-head tape echo: 7 modes, wow and flutter, saturation |

### Two views: SQ and UNIT
Every plugin (except Dyn8) has a **UNIT VIEW / SQ VIEW** button in the header. Each plugin
instance remembers which view it was left in.
- **SQ view** (default) follows the desk. A compressor opens like the SQ's compressor page: side
  chain on the left, transfer graph and the model's controls in the middle, and parallel controls
  plus a gain-reduction histogram on the right. Values sit in SQ-style touch tiles, so settings
  copy across one-to-one.
- **UNIT view** shows a hardware-style front panel for the unit type each model is based on. It has
  knobs with printed scales (which show the real values), stepped selectors, lit push-button rows,
  toggle switches, needle VU meters, LED gain-reduction ladders and value displays. On the FET 76,
  the panel changes between silver and black with the UNIT switch. The front panels are original
  artwork in the style of each unit type, not copies of A&H screens or any manufacturer's panel.
  Sidechain, parallel and FX output controls stay as normal panels underneath.
- Dyn8 has only the SQ view, because there's no hardware original for it.

Both views keep the Live-series header, meters and dark/light modes.

Common to all of them:
- **Zero latency** (no look-ahead) and stereo or mono.
- The same look as the rest of the Live series (LiveVoice, LiveMeter, ...), with dark and light modes.
- Every compressor has a live **transfer curve** (in dBu), a gain-reduction history, a GR meter, a
  **sidechain filter** (HPF/LPF/BPF) and a **parallel** mix.
- **FX MIX defaults to 100%** (effect only), like an SQ FX return on an FX send. Lower it to use a
  plugin as an insert.

## Installing

### LiveTest Updater (recommended)
1. From the [Latest build release](../../releases/tag/latest-build), download
   **LiveTestUpdater-macOS.zip** or **LiveTestUpdater-Windows.zip**, then unzip it.
2. Open **LiveTest Updater**.
   - **Mac, first launch:** macOS will say it can't verify the app. Click **Done**, open **System
     Settings > Privacy & Security** and click **Open Anyway**.
   - **Windows:** if **Windows protected your PC** appears, click **More info > Run anyway**.
3. Tick the plugins you want (or press **Updates**), then click **INSTALL / UPDATE**. You'll get one
   password prompt (Mac) or UAC prompt (Windows) for the whole batch. Old copies are removed first.
4. Reopen your host and rescan plug-ins.

The updater installs into the system VST3 folder (`/Library/Audio/Plug-Ins/VST3` on Mac,
`C:\Program Files\Common Files\VST3` on Windows). To check for new versions, open it again and
press **REFRESH**. It also tells you when a newer version of the updater itself is available.

### Manual
Every plugin is also attached to the release as `LiveTest-<Name>-macOS.zip` / `-Windows.zip`.
Unzip it and copy the `.vst3` folder into the VST3 folder above.

## Building

GitHub Actions builds everything on every push. Plugins are built in parallel jobs for macOS
(universal) and Windows, and the offline tests run on Linux. A push to `main` replaces the
**Latest build** release, along with the `manifest.json` that the updater reads. Push a tag such
as `v0.2.0` to keep a permanent release as well.

Versions use major.minor.patch and live in **`plugins.json`**: one `version` per plugin, plus
`suiteVersion` and `updaterVersion`. To release a change to one plugin, bump that plugin's
`version`. The updater then offers just that plugin as an update.

Building locally:
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release            # all plugins + updater
cmake --build build --config Release --parallel
cmake -S . -B build -DLTP_ONLY="OptoComp;Reverb"           # only some plugins
cmake -S . -B build -DLTP_BUILD_TESTS=ON && cmake --build build --target LiveTestTests
./build/Tests/LiveTestTests_artefacts/Release/LiveTestTests --snapshots shots   # tests + editor PNGs
```

### Offline tests
`Tests/TestMain.cpp` loads every plugin and checks for:
- silence in giving silence out, with no NaN/inf, including when every parameter is set at random
- real gain reduction from the compressors, and zero latency
- each compressor's on-screen transfer curve matching what the audio actually does
- reverb RT60 matching the DECAY setting
- exact delay times
- mono layouts working
- CPU load

### Layout
```
plugins.json        names, codes, versions, SQ model per plugin (single source of truth)
Shared/             processor base, shared editor + theme, DSP blocks, compressor/FX bases
Plugins/<Id>/       one .cpp per plugin (Plugins/Common has the shared model templates)
Updater/            LiveTest Updater app
Tests/              offline test runner
tools/ci.py         CI helpers: shard targets, packaging, manifest, release notes
```

## License
AGPL-3.0, copyright Jerod Ulicki. "Allen & Heath", "SQ", "DEEP", "RackExtra" and the SQ model
names are used only to say which desk feature each plugin corresponds to. This project is not
affiliated with or endorsed by Allen & Heath.
