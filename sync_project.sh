#!/bin/bash

# Local project directory (current directory)
LOCAL_DIR="$(pwd)/"

# Remote server and target directory
REMOTE_USER="root"
REMOTE_HOST="vortexlabs.online"
REMOTE_DIR="~/projects"

# Rsync options:
# -a: archive mode (preserves permissions, symlinks, etc.)
# -v: verbose
# -z: compress during transfer
# --delete: delete files on remote that are not present locally
# --exclude: exclude build artifacts if desired (optional)
rsync -avz --delete \
    --exclude 'build/' \
    --exclude '*.pyc' \
    --exclude '*.o' \
    --exclude '*.exe' \
    --exclude '*.bin' \
    --exclude '*.elf' \
    --exclude '.git/' \
    "$LOCAL_DIR" "$REMOTE_USER@$REMOTE_HOST:$REMOTE_DIR"

echo "Sync complete!"
