# Troubleshooting tips

If you are having issues, please enable debugging to collect a debug log. The log might tell you what the problem is. If you want to file a bug, please attach an error log.

To enable logging, add `-debug` after `spout2pw.sh`, like this (for Steam):

`~/spout2pw/spout2pw.sh -debug %command%`

You will find the debug log in your home directory, named **steam-****.log**. If you are using Steam Flatpak, the log will be in **~/.var/app/com.valvesoftware.Steam/steam-****.log** instead.

# Error messages

## Missing Vulkan (device or instance) extension

This means your GPU driver is missing a feature that is required for Spout2PW to work. In some cases, this can be resolved by updating your driver or changing the configuration.

### Missing Vulkan device extension: VK_EXT_external_memory_dma_buf

If you have an Nvidia card and are using the Nvidia proprietary drivers, you need to enable the `modeset` parameter. Some distros like Arch Linux and Fedora Linux enable it by default, but not all do.

To check whether it's enabled, run this in a terminal:

```
sudo cat /sys/module/nvidia_drm/parameters/modeset
```

This should say `Y`. If you see `N` instead, you need to enable it and then reboot.

For Ubuntu/Debian based distros:

```
echo 'options nvidia-drm modeset=1' | sudo tee -a /etc/modprobe.d/nvidia-enable-modeset.conf
echo 'options nvidia-current-drm modeset=1' | sudo tee -a /etc/modprobe.d/nvidia-enable-modeset.conf
sudo update-initramfs -u -k all
```

(Two options lines because Debian packaging uses different module names, it's harmless to have both)

For other distributions, please check your distribution documentation.

### Missing Vulkan device extension: VK_EXT_image_drm_format_modifier

This probably means you are using an old AMD card (GCN4 or older). The drivers [do not support this yet](https://gitlab.freedesktop.org/mesa/mesa/-/issues/5882). You will have to wait until this is supported, or use a newer GPU.

AMD GCN5 and newer GPUs (RX Vega, 4-digit RX, or any Ryzen APU) are supported.

## Failed to set up Vulkan for stream (c00000bb)

This usually means that there is an issue with your GPU driver.

Chances are you are using the Nvidia proprietary drivers version 550 or older, which have buggy and incomplete support for some features that spout2pw needs (they claim to support the required Vulkan extensions, which is why you don't get the more specific error message mentioned above, but then they're actually missing features that are required to make those extensions usable). If you use the Nvidia drivers, you need version 555 or newer (the newer the better, in general).

## Failed to set up Vulkan for stream (c000023e)

This probably means your PipeWire version is too old, although this should have been caught by the `spout2pw.sh` script. See the next section.

## Your PipeWire version is too old

Some Linux distributions such as Linux Mint ship an old PipeWire version (1.0.x) which does not currently work properly with Spout2PW. These old versions also won't work properly with Nvidia GPUs (even things like window capture can randomly glitch). You can check your PipeWire version by running the `pipewire --version` command in a terminal. The minimum required version for Spout2PW to work properly is PipeWire 1.2.7.

For Linux Mint, you can work around this by installing [this PPA](https://launchpad.net/~savoury1/+archive/ubuntu/pipewire). If you need help upgrading PipeWire, please ask in a support community for your Linux distribution.

If a newer PipeWire is not easily available for your distribution, please consider switching to one that ships more up-to-date software, such as [Fedora Linux](https://www.fedoraproject.org/)! Multimedia software and streaming are worlds that move quite quickly, and bugs are being actively fixed quite fast, so if you're stuck running old software you will probably have a worse experience.

The author of Spout2PW (Lina) personally recommends [Fedora KDE Plasma](https://fedoraproject.org/kde/) for streaming. It comes with major updates every 6 months and is very well maintained, and we have several people working on multimedia/streaming! Lina also co-maintains the OBS package for Fedora, including the Browser Source plugin.
