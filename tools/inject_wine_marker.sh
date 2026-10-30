#!/bin/bash
# Inject "Wine builtin DLL" marker into a PE DLL at offset 0x40
DLL="$1"

if [ -z "$DLL" ]; then
    echo "Usage: $0 <dll_path>"
    exit 1
fi

if [ ! -f "$DLL" ]; then
    echo "Error: $DLL not found"
    exit 1
fi

# Check if marker already exists
if grep -q "Wine builtin DLL" "$DLL" 2>/dev/null; then
    echo "Marker already present in $DLL"
    exit 0
fi

# Create the marker (32 bytes)
MARKER=$(printf 'Wine builtin DLL\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0' | head -c 32)

# Inject at offset 0x40
python3 -c "
import sys
with open('$DLL', 'r+b') as f:
    f.seek(0x40)
    f.write(b'Wine builtin DLL\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00')
print('Marker injected into $DLL')
"
