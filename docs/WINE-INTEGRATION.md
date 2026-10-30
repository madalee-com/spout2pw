# Wine Integration: How Spout2PW Loads and Runs Under Wine

This document explains the mechanics of how `spout2pw.exe` interacts with Wine, how the `.so`/`.dll` pair is loaded, and why certain environment variables and startup ordering matter.

## Overview

Spout2PW is a dual-component application:

| Component | Location | Purpose |
|-----------|----------|---------|
| `spout2pw.exe` | Windows side (runs inside Wine) | PE executable that acts as both a **Windows service** and a **Wine builtin DLL**. Captures DX textures via SpoutDXToC, dispatches work to the Unix side. |
| `spout2pw.so` | Linux side (native) | Unix library (`unixlib`) that receives cross-boundary calls from the Windows side. Manages Vulkan pipelines and PipeWire streams. |

The two components communicate over Wine's **unix socket** using the `__wine_unix_call` mechanism.

---

## The Builtin DLL Mechanism

### What is a "Wine builtin DLL"?

Most Wine DLLs are either:
- **Native DLLs**: Real Windows PE files (e.g., `user32.dll`) that Wine wraps
- **Native overrides**: Third-party DLLs placed in the prefix that replace Wine's builtins

A **builtin DLL** is special: it's a PE file that Wine recognizes as one of its own internal libraries. Wine marks builtin DLLs with a signature string at offset `0x40` in the PE header:

```
"Wine builtin DLL"
```

When Wine loads a DLL and finds this marker, it maps the DLL as `LO_BUILTIN`. Builtin DLLs are expected to have a companion `.so` file that implements the actual functionality on Linux.

### How spout2pw.exe becomes a builtin DLL

`spout2pw.exe` is compiled as a PE DLL (not a standard Windows executable) with the "Wine builtin DLL" marker. This means:

1. When any Windows application under Wine loads `spout2pw.dll`, Wine sees the builtin marker
2. Wine looks for the companion unixlib: `spout2pw.so`
3. Wine maps `spout2pw.so` into the Wine process and exposes its symbols to the PE DLL
4. The PE DLL (`spout2pw.exe`) can now call functions from `spout2pw.so`

### Finding spout2pw.so: WINEDLLPATH

Wine locates builtin DLL companions via the `WINEDLLPATH` environment variable. Specifically:

```
WINEDLLPATH/x86_64-unix/<builtin-name>.so
```

For Spout2PW:
```
WINEDLLPATH=/path/to/spout2pw-dlls
→ $WINEDLLPATH/x86_64-unix/spout2pw.so
```

If `WINEDLLPATH` is unset or doesn't contain `spout2pw.so`, Wine's `find_builtin_dll()` fails to locate the unixlib. Subsequent calls that depend on it (like `__wine_init_unix_call()`) return `c0000135` (`STATUS_DLL_NOT_FOUND`).

---

## The Service Registration

### INF Installation

Spout2PW registers itself as a Windows service using an INF file (`spout2pw.inf`):

```inf
[DefaultInstall.Services]
AddService = Spout2Pw,0,Spout2PwService

[Spout2PwService]
Description="Spout to PipeWire bridge"
DisplayName="Spout2Pw"
ServiceBinary="%11%\spout2pw.exe"
ServiceType=288          # SERVICE_WIN32_OWN_PROCESS | SERVICE_INTERACTIVE_PROCESS
StartType=2              # SERVICE_AUTO_START
ErrorControl=1
```

Key points:
- `StartType=2` means **auto-start**: Wine's Service Control Manager (SCM) starts the service whenever a Wine process initializes
- `ServiceBinary` points to `spout2pw.exe` in `system32`
- The INF also copies `spout2pw.exe` and `spout2pw.dll` into the prefix's `system32` directory

### When does the service start?

Wine SCM auto-starts the Spout2Pw service in these scenarios:
1. During prefix preparation (when `rundll32 setupapi.dll,InstallHinfSection` runs the INF install)
2. When any Wine process launches and SCM initializes (if not already running)

The service's `ServiceMain` function (in `spout2pw.c`) runs when it starts.

---

## Service Startup Sequence

When the Spout2Pw service starts, `ServiceMain` executes:

```c
// spout2pw.c, ServiceMain()

// 1. Set up service status (SERVICE_START_PENDING → RUNNING)
SetServiceStatus(...);

// 2. Initialize the unix call interface
ret = __wine_init_unix_call();
if (ret != STATUS_SUCCESS) {
    // ERROR: c0000135 if spout2pw.so not found
    msg = "Error initializing UNIX library";
    goto stop;
}

// 3. Initialize SpoutDXToC capture library
spout_names = SpoutDXToCNewSenderNames();

// 4. Create exit event and start threads
exit_event = CreateEventW(...);
sendernames_thread_handle = CreateThread(...);
```

### `__wine_init_unix_call()`

This is the critical function. It:
1. Connects to Wine's unix socket server
2. Verifies that `spout2pw.so` is available and can be loaded
3. Returns `STATUS_SUCCESS` (0x00000000) on success, or `STATUS_DLL_NOT_FOUND` (0xC0000135) on failure

If this fails, the service **no longer shows an error dialog**. Instead it logs a debug message and continues — a truly broken unixlib will fail at the first real `UNIX_CALL`, not at init time.

---

## The c0000135 Error

### Symptom (historical)

A one-time error dialog used to appear when launching a game via `spout2pw.sh`:

```
Error initializing UNIX library (c0000135)
```

After dismissing it, the game worked fine. **The popup has since been suppressed** — the service now treats the failure as non-fatal and continues. You will only see it in debug logs (`SPOUT2PW_DEBUG=1`) if it occurs.

### Root Cause

The error occurs when `WINEDLLPATH` is **not set** at the time Wine SCM starts the Spout2Pw service. Without `WINEDLLPATH`, Wine can't find `spout2pw.so`, so `__wine_init_unix_call()` fails with `STATUS_DLL_NOT_FOUND`.

This typically happens on the **auto-start path**: Wine's SCM invokes `ServiceMain` directly when a Wine process initializes, before any application has called `LoadLibraryW("spout2pw.dll")` to establish the unixlib handle via DllMain.

**Timeline of the bug (before fix):**

```
spout2pw.sh main():
  ├── prepare_prefix()          # Line 506 (OLD)
  │     └── check_spout2pw_install()
  │           └── if files missing:
  │                 └── rundll32 setupapi.dll,InstallHinfSection
  │                       └── Wine SCM starts Spout2Pw service
  │                             └── ServiceMain → __wine_init_unix_call()
  │                                   └── WINEDLLPATH NOT SET → c0000135
  ├── setup_env()               # Line 507 (OLD)
  │     └── export WINEDLLPATH=...   # Too late!
  └── "$@"                      # Game command runs with WINEDLLPATH set
```

The service failed during `prepare_prefix()` because `setup_env()` (which exports `WINEDLLPATH`) hadn't run yet.

**Why it recovered:** On the next Wine process launch (the game itself), `WINEDLLPATH` was already set and `LoadLibraryW("spout2pw.dll")` ran, so DllMain registered the unixlib and subsequent calls succeeded. The error was transient.

### Fixes

Two layers of mitigation were applied:

**1. Script ordering** — `setup_env()` (which exports `WINEDLLPATH`) now runs **before** `prepare_prefix()`:

```bash
# spout2pw.sh main():
validate_paths
prepare_proton
setup_env          # Sets WINEDLLPATH FIRST
prepare_prefix     # Service starts with WINEDLLPATH available
"$@"
```

Now `WINEDLLPATH` is exported before any Wine process launches during prefix preparation, so the auto-start service finds the unixlib.

**2. Code-level tolerance** — `ServiceMain` no longer treats a failed `__wine_init_unix_call()` as fatal. If the unixlib was already initialized by DllMain (the common case), the redundant re-lookup simply fails harmlessly and is logged at debug level. A genuinely broken unixlib will surface its error at the first real `UNIX_CALL`.

---

## The spout2pw.sh Wrapper

The `spout2pw.sh` script serves multiple roles:

### 1. Environment Setup

`setup_env()` exports critical variables:

```bash
setup_env() {
    export WINEDLLPATH="$spout2pw/spout2pw-dlls"
    # ... debug logging if enabled
}
```

This ensures Wine can find `spout2pw.so`.

### 2. Prefix Preparation

`prepare_prefix()` checks if Spout2PW is installed in the prefix:

```bash
prepare_prefix() {
    if check_spout2pw_install; then
        log "Spout2PW install is up-to-date"
        return
    fi
    
    # Install: copy files, register service
    cp spout2pw.exe system32/
    cp spout2pw.dll system32/
    cp spout2pw.inf system32/
    run_in_prefix cmd.exe /c "rundll32 setupapi.dll,InstallHinfSection ..."
}
```

`check_spout2pw_install()` verifies:
- `spout2pw.exe` and `spout2pw.dll` exist in `system32`
- The service is registered in the registry (`HKLM\SYSTEM\CurrentControlSet\Services\Spout2Pw`)

### 3. Game Launch

After preparation, the script launches the game:

```bash
"$@" || ret="$?"
```

This runs the Proton/game command with `WINEDLLPATH` already set.

---

## File Layout

The package structure provides both Windows and Unix components:

```
spout2pw-dlls/
├── x86_64-windows/
│   ├── spout2pw.exe      # Wine builtin DLL / Windows service
│   └── spout2pw.dll      # Builtin DLL (thin wrapper)
└── x86_64-unix/
    └── spout2pw.so       # Unix library (real implementation)
```

`WINEDLLPATH` points to `spout2pw-dlls/`, so Wine finds:
- `x86_64-windows/spout2pw.exe` and `spout2pw.dll` for the PE side
- `x86_64-unix/spout2pw.so` for the unixlib

---

## Key Takeaways

1. **spout2pw.exe is a Wine builtin DLL**: It has the "Wine builtin DLL" marker and maps to `spout2pw.so`.

2. **WINEDLLPATH is critical**: Without it, Wine can't find `spout2pw.so`, causing `__wine_init_unix_call()` to fail with `c0000135`.

3. **Order matters**: `WINEDLLPATH` must be set before any Wine process launches that might start the Spout2Pw service. The fix in `spout2pw.sh` ensures this by calling `setup_env()` before `prepare_prefix()`.

4. **Service auto-start is a double-edged sword**: Wine SCM starts the service automatically, which is convenient but means the service can start during prefix preparation (before `WINEDLLPATH` is set) unless we reorder the startup sequence.

5. **The error is non-fatal**: Even if it occurs (e.g., a very early SCM auto-start), the service suppresses the popup and continues. The unixlib handle is established by DllMain on the first `LoadLibraryW`, so normal operation proceeds.

---

## Debugging Tips

To debug Spout2PW under Wine:

```bash
# Enable Wine debugging for spout2pw
export WINEDEBUG=+timestamp,+relay,+spout2pw

# Check WINEDLLPATH is set
echo $WINEDLLPATH

# Verify spout2pw.so exists
ls -la $WINEDLLPATH/x86_64-unix/spout2pw.so

# Check service registration
wine reg query HKLM\SYSTEM\CurrentControlSet\Services\Spout2Pw

# Query service status
wine sc query Spout2Pw

# Manually start the service
wine sc start Spout2Pw
```

Note: `sc start Spout2Pw` may "hang" (exit code 36 = timeout) because the service starts and stays running. This is normal — exit code 36 means the service started but didn't reach a "stopped" state, which is expected for a long-running service.
