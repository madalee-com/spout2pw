# Spout2PW: Spout2 to PipeWire bridge

**[Spout2PW website](https://spout2pw.lina.yt)**

<div style="background-color:#5a0f0f; color:#ffe0e0; padding:12px 16px; border-radius:6px; border-left:4px solid #ff3b3b; font-size:0.95em;">
<strong>⚠ Transparency notice:</strong> This fork was partially created with the assistance of local AI tools. The build pipeline, vendored dependencies, and CI configuration were generated or assisted by an AI coding agent; all code has been reviewed and is intentionally kept here.
</div>

[![Github-sponsors](https://img.shields.io/github/sponsors/hoshinolina?label=Sponsor&logo=GitHub)](https://lina.yt/sponsor)
[![Ko-Fi](https://shields.io/badge/ko--fi-Tip-ff5f5f?logo=ko-fi)](https://lina.yt/kofi)

**If you like this, please help support me with the links above!**

See the [wiki page](https://github.com/madalee-com/spout2pw/wiki) for installation and usage instructions.

## Building

```bash
git submodule update --init
./build.sh
```

This creates a package at `build/pkg`.

## Build dependencies

On Debian: `sudo apt install meson ninja-build libdbus-1-dev libwine-dev mingw-w64 libgbm-dev libdrm-dev libvulkan-dev wine64-tools`

## Credits

Based on a prototype by tytan652:

* https://codeberg.org/tytan652/spoutdxtoc
* https://codeberg.org/tytan652/spout2xdp
