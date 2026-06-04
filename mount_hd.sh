#!/bin/bash
# mount_hd.sh - Mount/unmount AaronOS hd.img on host
# Usage: ./mount_hd.sh [mount|umount|copy-to|copy-from]

HD_IMG="hd.img"
MNT_DIR="/tmp/aaronos_mount"

if [ ! -f "$HD_IMG" ]; then
    echo "Error: $HD_IMG not found. Run setup.sh first."
    exit 1
fi

case "${1:-mount}" in
    mount)
        mkdir -p "$MNT_DIR"
        if [[ "$(uname)" == "Darwin" ]]; then
            hdiutil attach "$HD_IMG" -mountpoint "$MNT_DIR" 2>/dev/null
            echo "Mounted at $MNT_DIR"
        else
            sudo mount -t vfat -o loop "$HD_IMG" "$MNT_DIR" 2>/dev/null
            if [ $? -ne 0 ]; then
                OFFSET=$(sudo fdisk -l "$HD_IMG" 2>/dev/null | awk '/^[^ ]/{if($1~"^'"$HD_IMG"'")print $2}' | head -1)
                if [ -n "$OFFSET" ]; then
                    sudo mount -o loop,offset=$((OFFSET*512)) "$HD_IMG" "$MNT_DIR"
                else
                    sudo mount -o loop,offset=0 "$HD_IMG" "$MNT_DIR"
                fi
            fi
            echo "Mounted at $MNT_DIR (sudo)"
        fi
        ;;
    umount|unmount)
        if [[ "$(uname)" == "Darwin" ]]; then
            hdiutil detach "$MNT_DIR" 2>/dev/null || hdiutil detach "/Volumes/AARONOS" 2>/dev/null || echo "Not mounted"
        else
            sudo umount "$MNT_DIR" 2>/dev/null || echo "Not mounted"
        fi
        echo "Unmounted."
        ;;
    copy-to)
        if [ -z "$2" ]; then echo "Usage: $0 copy-to <local_file> [remote_name]"; exit 1; fi
        LOCAL="$2"
        REMOTE="${3:-$2}"
        if [ ! -f "$LOCAL" ]; then echo "Error: $LOCAL not found"; exit 1; fi
        "$0" mount
        cp "$LOCAL" "$MNT_DIR/$REMOTE"
        "$0" umount
        echo "Copied $LOCAL -> hd.img:/$REMOTE"
        ;;
    copy-from)
        if [ -z "$2" ]; then echo "Usage: $0 copy-from <remote_name> [local_name]"; exit 1; fi
        REMOTE="$2"
        LOCAL="${3:-$2}"
        "$0" mount
        if [ -f "$MNT_DIR/$REMOTE" ]; then
            cp "$MNT_DIR/$REMOTE" "$LOCAL"
            echo "Copied hd.img:/$REMOTE -> $LOCAL"
        else
            echo "Error: $REMOTE not found on image"
        fi
        "$0" umount
        ;;
    *)
        echo "Usage: $0 [mount|umount|copy-to|copy-from]"
        ;;
esac
