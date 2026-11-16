# Building

## VitaSDK

Install [VitaSDK](https://vitasdk.org) with vdpm's bootstrap, then the libraries the app links
against (the same steps as `.github/workflows/cmake-psvita.yml`):

```sh
git clone --depth 1 https://github.com/vitasdk/vdpm.git && cd vdpm
./bootstrap-vitasdk.sh --install-dir /usr/local/vitasdk
export VITASDK=/usr/local/vitasdk PATH="$VITASDK/bin:$PATH"
vdpm install zlib bzip2 expat openssl curl opus libpng libjpeg-turbo freetype libvita2d
```

## Build the VPK

```sh
git clone --recurse-submodules https://github.com/LouisF37/VitaMoon.git
cd VitaMoon
cmake -S . -B build
cmake --build build -j
```

The result is `build/vitamoon.vpk` (and `build/vitamoon.elf` with debug symbols, for crash dumps).

- `-DVITA_OPT_LEVEL=-O0` builds without optimisation, for debugging (default `-O2`).
- The home screen shows the version and the build (`branch-commit`), so a tester can tell which
  build is installed.
- Submodules: ENet, inih and mdnsniff. moonlight-common-c is a copy in the repository with its patches
  ([third_party/patches/README.md](../third_party/patches/README.md)), so no patch step is needed.

## Continuous integration

Every push builds the VPK, runs the PC tests and publishes a pre-release named `build-<branch>` with a
fixed download link (`moonlight-<branch>.vpk`). Build and test logs go to the `ci-logs` branch
(`<branch>/build.log`, `tests.log`, `status.txt`), readable with plain git.

## Developer scripts (`tools/dev`)

Set the Vita's address in `VITA_IP` or in `tools/dev/vita_ip.txt` (not committed).

| Script | Use |
|---|---|
| `deploy-vpk.sh` | build, send the VPK to VitaShell's FTP (`ux0:/ABM/`), open VitaShell |
| `deploy-eboot.sh` | build and replace only `eboot.bin` of the installed app, then start it (vitacompanion) |
| `crash-dump.sh` | fetch the newest `psp2core` dump and decode it against `build/vitamoon.elf` |
| `release_info.py` | tag, title and notes of the version CI will release ([RELEASING.md](RELEASING.md)) |

## Translations

Interface strings are written in English in the sources (`N_("...")` / `tr("...")`). The French and
Spanish texts are in `tools/i18n/table.py` and `table2.py`; `python3 tools/i18n/gen.py` regenerates
`src/i18n.c`, and `python3 tools/i18n/check.py missing` lists strings without a translation.

## Other tools

- `tools/sunshine-import.ps1`: adds the PC's installed Steam games (and PS1 games for DuckStation)
  to Sunshine with their cover art.
- `tools/wol_sniffer.py`: listens for Wake-on-LAN packets, to check them without turning the PC off.
- `tools/make_background.py`: draws the five menu backgrounds, a lunar sky per colour theme
  (`assets/bg_<theme>.png`). Needs numpy and Pillow.
- `tools/art/make_brand.py`: draws the logo and the images made from it (LiveArea bubble, gate and
  background, `assets/logo_mark.png`, the waiting moon `assets/moon.png`, `docs/assets/`). Needs
  Pillow, fontTools, `rsvg-convert` and the Poppins and Inter Display fonts.
- `tools/art/make_fonts.py`: cuts Inter (Regular, Light) and Poppins (Regular) down to the characters
  VitaMoon can meet, into `assets/`, and writes the table of the characters Poppins has
  (`src/gui/title_font_cover.h`). Needs fontTools and the two fonts (Debian's `fonts-inter`, the Google
  Fonts copy of Poppins).
