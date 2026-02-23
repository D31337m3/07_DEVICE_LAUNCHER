#!/bin/bash

# Variables
REMOTE_USER="root"
REMOTE_HOST="vortexlabs.online"
REMOTE_PROJECT_DIR="~/projects/07_DEVICE_LAUNCHER/build"
LOCAL_DEST_DIR="./build_bin_from_server"

# Create local destination directory if it doesn't exist
mkdir -p "$LOCAL_DEST_DIR"

# Rsync .bin files from server's build directory to local destination
rsync -avz --include='*.bin' --exclude='*' \
    "$REMOTE_USER@$REMOTE_HOST:$REMOTE_PROJECT_DIR/" "$LOCAL_DEST_DIR/"

echo "Pull complete! .bin files are in $LOCAL_DEST_DIR"
