# Lakka Installer NX

A Nintendo Switch homebrew application that downloads and installs
[Lakka](https://www.lakka.tv) directly from your console.  Built with the
[Borealis](https://github.com/natinusala/borealis) UI framework.

## Features

- **Version browser** – browse all available stable releases
- **Nightly / dev builds** – optionally list and install nightly builds
- **One-tap install** – downloads the `.7z` archive and extracts it to the
  SD card in one step
- **Update checker** – see whether a newer version is available and update
  in-place
- **Progress display** – real-time download and extraction progress with a
  custom NanoVG progress bar
- **Configuration** – remembers installed version, channel, and user
  preferences across launches

## Screenshots

*(the app uses the standard Borealis Switch UI – tabs on the left, lists on the
right, dialogs for confirmation, and a full-screen progress view during
installation)*

## Building

### Prerequisites

| Component | Notes |
|-----------|-------|
| [devkitPro](https://devkitpro.org/wiki/Getting_Started) | Provides the `devkitA64` toolchain and `libnx` |
| `switch-curl` | HTTP downloads |
| `switch-mbedtls` | TLS support for HTTPS |
| `switch-zlib`, `switch-bzip2` | Compression |
| `switch-freetype`, `switch-libpng` | Font / image rendering (Borealis) |
| `switch-glfw`, `switch-mesa` | OpenGL rendering backend (Borealis) |

### Quick start

```bash
# 1. Clone this repo (with submodules)
git clone --recursive https://github.com/user/lakka-install-nx.git
cd lakka-install-nx

# 2. Run the setup script (downloads LZMA SDK, copies resources, installs deps)
chmod +x scripts/setup.sh
./scripts/setup.sh

# 3. Build
make

# The output is lakka-install-nx.nro – copy it to /switch/ on your SD card.
```

### Manual dependency install

```bash
sudo dkp-pacman -S --needed \
    switch-dev switch-curl switch-mbedtls \
    switch-zlib switch-bzip2 switch-freetype \
    switch-libpng switch-glfw switch-mesa
```

## Usage

1. Copy `lakka-install-nx.nro` to the `/switch/` directory on your SD card.
2. Launch via the Homebrew Menu (hold **R** while launching a game, or use a
   title-override approach).
3. Use the **Home** tab to check your current installation or quickly install
   the latest stable release.
4. Use the **Stable** or **Nightly** tabs to pick a specific version.
5. Confirm the install – the app downloads the `.7z` from
   `https://le.builds.lakka.tv/Switch.aarch64/` and extracts it to the
   SD card root.
6. Reboot your Switch into RCM mode and inject the Lakka payload to boot
   into Lakka.

## Project structure

```
lakka-install-nx/
├── Makefile                      Build system (devkitA64)
├── scripts/setup.sh              One-time setup / dependency download
├── source/
│   ├── main.cpp                  Entry point
│   ├── util/
│   │   ├── net.hpp / net.cpp           HTTP client (libcurl) + async download
│   │   ├── extract.hpp / extract.cpp   7z extraction (LZMA SDK wrapper)
│   │   ├── config.hpp / config.cpp     INI config management
│   │   └── lakka_api.hpp / .cpp        Version list fetching & parsing
│   └── views/
│       ├── main_view.hpp / .cpp        Root TabFrame (Home, Stable, Nightly, Settings)
│       └── install_page.hpp / .cpp     Full-screen download+extract progress
├── lib/
│   ├── borealis/                 Borealis GUI library (git submodule)
│   └── lzma/                    LZMA SDK C sources (downloaded by setup.sh)
├── romfs/                        Borealis resources bundled into the .nro
└── icon.jpg                      Homebrew menu icon
```

## Configuration

Settings are stored at `sdmc:/config/lakka-install-nx/config.ini`:

```ini
[lakka]
installed_version=6.1
installed_channel=stable

[settings]
show_dev_versions=false
auto_check_updates=true
install_path=sdmc:/
```

## Download sources

| Channel | URL |
|---------|-----|
| Stable  | `https://le.builds.lakka.tv/Switch.aarch64/` |
| Nightly | `https://nightly.builds.lakka.tv/latest/Switch.aarch64/` |

## License

This project is licensed under **GNU GPLv3 (or later)**.  
See [LICENSE](LICENSE) for project terms.  
Third-party components keep their own licenses (for example, Borealis in `lib/borealis`).
