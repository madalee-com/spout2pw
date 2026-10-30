# Spout2PW: Spout2 on Linux bridge

**[Main website](https://spout2pw.lina.yt/)**

No more green screens, no more UI overlays! Use Spout2 output with Windows apps running in Proton, and send the video directly to OBS.

https://github.com/user-attachments/assets/b6f4f5a3-518c-4c20-b3e7-af96a523c75d

## Support

If you like this tool, please help support my work!

[![Github-sponsors](https://img.shields.io/github/sponsors/hoshinolina?label=Sponsor&logo=GitHub)](https://lina.yt/sponsor)
[![Ko-Fi](https://shields.io/badge/ko--fi-Tip-ff5f5f?logo=ko-fi)](https://lina.yt/kofi)

## What is this?

Spout2PW is an extension for Proton/Wine that bridges [Spout2](https://spout.zeal.co/) video streams into [PipeWire](https://pipewire.org/) video streams.

Spout2 is an app-to-app video streaming technology for Windows, and PipeWire is a general audio/video streaming framework for Linux. When you run a Windows app with Spout2 support under Proton/Wine, Spout2PW takes its Spout2 streams out from the "virtual Windows world" and seamlessly converts them to PipeWire streams in the "native Linux world".

Then, you can use [OBS-PWVideo](https://github.com/hoshinolina/obs-pwvideo) to add them as a source directly in the Linux version of OBS.

## Installation

**Important note for Linux Mint users:** Linux Mint ships an old version of PipeWire (1.0) that is problematic. See [[this section|Troubleshooting#your-pipewire-version-is-too-old]] for more info and a workaround.

You need to install both Spout2PW (the Proton/Wine extension) and OBS-PWVideo (the OBS plugin).

Grab the latest Spout2PW release (named **spout2pw-x.x.x-bin.tar.gz**) from the [releases page](https://github.com/hoshinolina/spout2pw/releases) and extract it. You should normally extract it somewhere in your home directory (if you use Steam Flatpak see [that section](#with-steam-flatpak) for special considerations).

Spout2PW comes in a folder with a version number, such as **spout2pw-0.1.2** (this is to make it clear what version you downloaded/extracted). To be able to update it more easily, you might want to rename it to just **spout2pw**. That way, when you update it, you won't have to change the Steam launch settings. This would look like this:

<img width="686" alt="Screenshot of Spout2PW extracted to ~/spout2pw" src="https://github.com/user-attachments/assets/4bfb14c8-7668-47c9-aebf-7240fdbdc022" />

Inside the folder, **spout2pw.sh** is the main launcher for Spout2PW. Please don't move or change any other files, as they are needed for the launcher to work.

The example paths in this guide assume you have set it up like this, although of course you are free to use any path you like.

To install OBS-PWVideo, follow these instructions depending on your distro/environment:

### OBS Flatpak users

**Hint:** *Don't know if you're using OBS Flatpak or not? Go to Help ➞ Log Files ➞ View Current Log and scroll down a bit. If you see **Flatpak Branch** mentioned, you are using Flatpak. If you are using an immutable distro such as SteamOS, Bazzite, etc., then you are probably using Flatpak.*

OBS-PWVideo is available on Flathub. Search for **OBSPWVideo** in your software manager, or use the terminal to install it:

    flatpak install com.obsproject.Studio.Plugin.OBSPWVideo

<img width="752" alt="obspwvideo-flatpak" src="https://github.com/user-attachments/assets/fd854df2-e865-4ece-8237-85fa4343f192" />

### Fedora users

The plugin is available directly in Fedora as a package called **obs-studio-plugin-pwvideo**. Just install it with your favorite package manager app (where you install your software), or from the terminal:

    sudo dnf install obs-studio-plugin-pwvideo

### Arch (and CachyOS, etc.)

The plugin is available as an [AUR package](https://aur.archlinux.org/packages/obs-pwvideo) (note: this package is maintained by a third party and may not always be fully up to date).

### Ubuntu/Debian users

A .deb package is available on the [obs-pwvideo releases page](https://github.com/hoshinolina/obs-pwvideo/releases). Note: the .deb build is not officially tested, it is provided as a convenience. It requires a relatively recent system, LTS distros may not work.

### Other distros

Use the portable plugin package from the [obs-pwvideo releases page](https://github.com/hoshinolina/obs-pwvideo/releases). It's named `obs-pwvideo-X.X.X-x86_64-ubuntu-gnu.tar.xz`, but it should work on other Linux distros as long as the system dependencies are recent enough. Extract it and move the **obs-pwvideo** folder into `~/.config/obs-studio/plugins/`.

## Usage

Spout2PW works with either [**umu-launcher**](https://github.com/Open-Wine-Components/umu-launcher) to run apps outside of Steam, or with Steam directly. Regardless of what method you choose, you will be running Windows apps (with Spout2 support) through a *Compatibility Tool* which is some version of [**Proton**](https://en.wikipedia.org/wiki/Proton_(software)) (which is itself a version of [**Wine**](https://www.winehq.org/)).

Currently supported Wine/Proton versions:

* Proton Experimental 10 (Steam built-in)
* [GE-Proton](https://github.com/GloriousEggroll/proton-ge-custom) 10-29 or newer (umu-launcher, Flatpak extension, manual install, etc.)

**Note:** *Spout2PW requires features only available in Proton 10+ (which is based on Wine 10). Standalone Wine 11+ support is not yet available, but planned. If you try to use an incompatible version, Spout2PW will show an error message box.*

The Steam instructions depend on whether you're using Steam Flatpak, or a native Steam package. If you don't know, you can find out like this:

1. Launch steam and go to the settings dialog
1. Click on **Storage**
1. Look at the path above the disk usage numbers

If it says **.VAR/APP/COM.VALVESOFTWARE.STEAM**, you are using Flatpak and you should go to the [Flatpak section](#with-steam-flatpak). Otherwise, continue on to the [non-Flatpak](#with-steam-non-flatpak) section.

<img width="425" alt="Screenshot of the Steam Storage settings dialog" src="https://github.com/user-attachments/assets/3a433151-57ea-4c44-a0bb-1daf2b920864" />

### With Steam (non-Flatpak)

1. Under the game/app you want to run, click on the gear icon and select **Properties**.
1. Under **Compatibility**, tick **Force the use of a specific Steam Play compatibility tool** and select one of the supported Proton versions from the list above (you probably want **Proton Experimental** unless you have installed something else yourself).
1. Under **General**, change the launch options to:

    `/path/to/spout2pw/spout2pw.sh %command%`

   Where `/path/to/spout2pw` is the path where you extracted Spout2PW.

Then, simply launch the app/game.

<img width="505" alt="Steam Compatibility settings screenshot" src="https://github.com/user-attachments/assets/02f8bb62-7c9b-40db-b908-e2480a33296b" /><img width="505" alt="Steam General settings screenshot configured for Spout2PW" src="https://github.com/user-attachments/assets/563379cc-b02a-4715-a7c9-2c6077ff9916" />

**Hint:** *If the path is under your home directory, you can use `~` instead of typing it out. For example, if Spout2PW is under `/home/lina/spout2pw/spout2pw.sh`, you can just use `~/spout2pw/spout2pw.sh %command%` instead. This works almost everywhere on Linux!*

**Hint:** *It is recommended to disable the Steam Overlay (under General) unless you plan to use it, since it can cause trouble (keyboard shortcut issues, extra load, etc.). This has nothing to do with Spout2PW, it's just generally a good idea.*

**Important: If the app/game fails to start with no error messages, you are probably using the Flatpak version of Steam. In that case, see the following section.**

### With Steam (Flatpak)

**IMPORTANT: There is a [bug](https://github.com/ValveSoftware/steam-for-linux/issues/12698) in Steam/Proton that can cause Proton Experimental not to work with Flatpak in some cases (whether you use Spout2PW or not). Spout2PW now includes a workaround for this. If you want to run an app/game with Proton Experimental without Spout2PW and you find it doesn't launch at all, change the launch options to `LD_AUDIT= %command%` (that's with a space between `=` and `%command%`).**

The Flatpak version of Steam runs in a sandbox and can't access Spout2PW in your home directory by default. To make it work, you have two options:

#### Method 1: Using the Flatpak Steam home directory

When Steam runs inside Flatpak, the home directory it sees (which we call `~`) is actually the directory `~/.var/app/com.valvesoftware.Steam/` (where `~` is your real home directory). For example, if your home directory is `/home/lina`, then a real file at `/home/lina/.var/app/com.valvesoftware.Steam/X` would show up to Steam as if it were `/home/lina/X`.

You can move Spout2PW into `~/.var/app/com.valvesoftware.Steam/` to make it accessible to Steam (if you are using a graphical file manager, you might need to enable an option to *Show hidden files/directories* to see the `.var` folder). Then, in the Steam launch options, use `~` to represent that entire subfolder.

For example, if your username is **lina**, you can move the main Spout2PW folder `spout2pw` into this path:

    /home/lina/.var/app/com.valvesoftware.Steam/

Which would look like this:

<img width="915" alt="spout2pw-steam-flatpak" src="https://github.com/user-attachments/assets/10131283-f11a-473a-a33c-ecd6789ab3ff" />

And then set the Steam launch options to:

    ~/spout2pw/spout2pw.sh %command%

Don't forget to also change the compatibility tool as described in the [non-Flatpak section](#with-steam-non-flatpak).

#### Method 2: Adding extra Flatpak permissions

Another option is to grant Steam access to the Spout2PW directory directly. For example, if Spout2PW is in a folder called `spout2pw` in your home directory, that is, `~/spout2pw`, then you can run this command in a terminal:

    flatpak override --user com.valvesoftware.Steam --filesystem=~/spout2pw

Then you can follow the normal instructions in the [non-Flatpak section](#with-steam-non-flatpak).

### With umu-launcher

Prefix the spout2pw launcher script to `umu-run`, like this:

    ~/spout2pw/spout2pw.sh umu-run your-favorite-app.exe

You can use the usual umu-launcher environment variables.

**Hint:** *If you want to run VTube Studio with umu-launcher, you have to pass the `-nosteam` argument, like so:*

    ~/spout2pw/spout2pw.sh umu-run "~/.local/share/Steam/steamapps/common/VTube Studio/VTube Studio.exe" -nosteam

### Connecting the stream

Once your app is running under Spout2PW, launch OBS and add a **PipeWire Video** source to your scene.

<img width="661" alt="source" src="https://github.com/user-attachments/assets/bc4d61cf-c348-44f7-9c9f-d818fb0ffc31" />

Select the Spout2 source stream in the source properties, and that's it! You should now see your Spout2 video in OBS.

## App-specific tips

### VTube Studio

To make transparency work properly in VTube Studio, use the Color Picker background, set the color to fully black, and enable the "Transparent in capture" toggle.

### VSeeFace

To make transparency work properly, add the `--background-color '#000000'` argument to set the background color to black.

## Planned improvements

* Proper multi-gpu/hybrid graphics support

## Common issues

If you get an error message or things don't work properly, please see the [[Troubleshooting]] page.

### The stream runs really slow, at 1 FPS

You are probably using an old version of PipeWire, see [this section](#issues-with-old-pipewire-versions).

### Random problems resizing/restarting the Spout2 stream

This is known to be flaky right now, due to a combination of Spout2 being quite a messy/unreliable design and Proton/Wine bugs. Please avoid resizing your sources while streaming. Spout2PW already includes some workarounds for this, but it is likely impossible to fully fix without fixing the underlying Proton/Wine bugs, so it will take some time.

### My character displays properly, but the background is not transparent but rather a blended color / haze / etc.

Make sure the background in your sender app is black. This is usually the only way to get *correct* alpha blending (it will result in what we call correct *premultiplied alpha* output, which matches the blend mode in the Spout2 OBS plugin named as such).

A lot of people use "Default" blending with the OBS Spout2 plugin and think it works, however 99.9% of the time this is *mathematically wrong* and will cause subtly thicker/colored/incorrect outlines for your character (and also makes it harder for OBS to render, causing scaling edge issues or extra GPU load depending on the method). It is impossible to output **correct** non-premultiplied alpha in a source application without going out of your way to do it.

A lot of source application developers don't understand this whole alpha mess, and tell their users to use "Default" blending thinking it will remove the background. While it kind of will, it will cause other subtle issues. Essentially, GPUs simply do not support "non-premultiplied" alpha properly for the kinds of things you want to use Spout2 for (it also causes other problems too).

While it would be possible to support "non-premultiplied" alpha in Spout2PW the same way it works in the OBS Spout2 plugin, it would require adding hundreds of lines of code (requires adding an entire Vulkan render pass), and it still wouldn't give you the intended, correct transparency, so this is not a priority. Please, pretty please, just tell your app developer to switch to premultiplied (which usually just means having a transparent black background). It will help people using Spout2 on Windows too.

If you are an application developer, please read [this](https://www.realtimerendering.com/blog/gpus-prefer-premultiplication/) and [this](https://amindforeverprogramming.blogspot.com/2013/07/why-alpha-premultiplied-colour-blending.html) to learn more. If you're wondering how to "cut" the character out of the background to send to Spout2, the answer is, you simply can't do that (correctly). You have to render to a buffer with a transparent background *first*, send that to Spout2, then composite that on top of your background if you want to show a non-black background to the user inside your app.

### Issues with hybrid graphics (multiple GPUs)

Hybrid graphics isn't fully correctly supported yet. It might or might not work. This will be fixed soon (Spout2PW has to make sure it runs on the same GPU as the Spout2 output app).

In addition, hybrid graphics with the NVidia proprietary driver causes all sorts of problems. If you want to use hybrid graphics between an NVidia GPU and another vendor's GPU (for example, Spout2PW/Proton on one GPU and OBS on another), please use the NVK driver for NVidia instead of the proprietary driver. Otherwise, please make sure that both OBS and the app using Spout2 run on the same GPU.

## What's the performance like?

Compared to the [OBS Spout2 Plugin](https://github.com/Off-World-Live/obs-spout2-plugin), Spout2PW adds one extra buffer copy on-GPU (it has to copy the image out of the Spout2 texture and into a PipeWire buffer texture). Since all the copying happens on the GPU, this should not have much performance impact. PipeWire video sharing itself is much more efficient and better designed than Spout2, so it should add practically no significant overhead. In exchange, it does use more GPU memory, since it uses multiple buffers to avoid having to block on reads/writes of a single buffer (Spout2PW configures around 5 video frames' worth).

Spout2PW should not cause any additional "frame sync" issues, and in fact should behave better than the native Spout2 plugin (since it does not properly synchronize Spout2 texture access, while Spout2PW does). The way it works is that when OBS wants to render a new frame, it "asks" Spout2PW to grab a Spout2 frame and send it over (Spout2PW runs locked to the OBS frame rate). The app sending frames via Spout2 itself usually runs on its own frame clock, so you might have some skipped/duplicated frames depending on the timing (this is the same on Windows). If OBS runs at a slower frame rate (e.g. 30FPS stream with a 60FPS locked source app), then Spout2PW will only trigger and copy data at the OBS rate.

Spout2PW itself has zero latency, and obs-pwvideo generally adds a latency of ~one frame, though a fully zero-latency (synchronous) mode is planned.

Unlike Spout2, PipeWire does not do anything until both sides are connected and running, so if you don't start OBS or don't connect the stream, Spout2PW will simply be idle and use no CPU/GPU time (the sending app will still write frames to the Spout2 texture unless you turn off the sender, though).

## What about native Linux apps?

Spout2PW uses [libfunnel](https://github.com/hoshinolina/libfunnel/) under the hood, which is a library I developed to make creating PipeWire video streams easy. In other words, it's analogous to the Spout2 SDK on Windows itself. If you are an application developer and you want to add PipeWire video support to a native Linux build of your app, please check out that repo! The raw PipeWire API is extremely complicated and difficult to use, so libfunnel exists to bridge the gap and take care of all the details for you.