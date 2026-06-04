#!/bin/bash
set -e
git rm hd.img iso_root/boot/kernel.elf 2>/dev/null || true
git commit -m "Remove tracked binary files" --allow-empty
bash setup.sh
