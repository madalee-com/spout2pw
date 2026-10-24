# Spout2PW Documentation

This directory contains detailed documentation for the Spout2PW project — a bridge between **Spout2** (Windows DirectX texture sharing) and **PipeWire** (Linux multimedia framework).

## Architecture Overview

Spout2PW runs as a **Wine service** on Linux, intercepting Spout2 sender/receiver calls made by Windows applications and forwarding the captured textures to PipeWire streams.

### Dual-Component Design

The project is split into two compiled targets:

| Component | File | Target | Role |
|-----------|------|--------|------|
| **Windows side** | `src/spout2pw.c` | PE DLL (`.dll`) | Runs inside Wine, hooks Spout2 API and captures textures via DXVK interop |
| **Unix side** | `src/spout2pw_unix.c` | Native shared library | Runs on Linux host, manages PipeWire streams and Vulkan memory |

The two components communicate via **Wine's Unix call interface** (`__wine_unix_call`), which allows the Wine PE to invoke native Linux functions.

### Key Dependencies

- **spoutdxtoc**: Spout2 DX Capture library — handles DirectX texture acquisition
- **libfunnel**: A wrapper around PipeWire for Vulkan-streamed content
- **DXVK**: Provides `IDXGIVkInteropDevice` / `IDXGIVkInteropSurface` for extracting raw Vulkan handles from D3D11 textures
- **Vulkan**: Used for image blitting and memory management on the Linux side
- **PipeWire**: The final output sink, making Spout2 textures available to Linux applications (OBS, VLC, etc.)

## File Index

| File | Description |
|------|-------------|
| [spout2pw.c](#spout2pwc) | Windows-side service entry point and texture capture logic |
| [spout2pw_unix.c](#spout2pw_unixc) | Unix-side PipeWire source management and Vulkan pipeline |
| [spout2pw_unix.h](#spout2pw_unixh) | Shared structures and Wine Unix call dispatch definitions |
| [meson.build](../meson.build) | Build configuration (cross-compilation to mingw, subprojects) |

## How It Works (High-Level Flow)

```
┌─────────────────────────────────────────────────────────────┐
│  Windows Application (via Wine)                             │
│  ── Spout2 API calls ──► spoutdxtoc.dll                    │
│                              │                              │
│                              ▼                              │
│  spout2pw.c (Wine PE service)                              │
│  ── Query DXVK interop ──► D3D11 Texture                  │
│                              │                              │
│                              ▼                              │
│  UNIX_CALL(spout2pw_unix.c)                                │
│                              │                              │
│              ┌───────────────┴───────────────┐             │
│              ▼                               ▼             │
│   Vulkan: extract VkImage          PipeWire (via libfunnel)│
│   from DXVK internal struct        stream → queue → sink  │
└─────────────────────────────────────────────────────────────┘
```

## Building

See the [project root README](../README.md) for build instructions. The project uses Meson with a Wine cross-compilation toolchain.

## Environment Variables

| Variable | Purpose |
|----------|---------|
| `SPOUT2PW_APPNAME` | Custom application name for PipeWire session (default: `Spout2PW`) |
| `SPOUT2PW_INSTANCE` | Custom PipeWire stream instance name |
| `SPOUT2PW_VALIDATION` | Set to `1` to enable Vulkan validation layers |
