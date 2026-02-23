#!/bin/bash

# Variables
LOCAL_DIR="$(pwd)/"
REMOTE_USER="root"
REMOTE_HOST="vortexlabs.online"
REMOTE_DIR="~/projects"
SSH_KEY="$HOME/.ssh/id_ed25519"

# Step 1: Check if SSH key exists, create if not
if [ ! -f "$SSH_KEY" ]; then
    echo "SSH key not found. Generating new ed25519 key..."
    ssh-keygen -t ed25519 -C "$USER@$(hostname)" -f "$SSH_KEY" -N ""
else
    echo "SSH key already exists: $SSH_KEY"
fi

# Step 2: Copy SSH key to server if not already present
if ! ssh -o PasswordAuthentication=no -o BatchMode=yes $REMOTE_USER@$REMOTE_HOST exit 2>/dev/null; then
    echo "Copying SSH key to server..."
    ssh-copy-id -i "$SSH_KEY.pub" $REMOTE_USER@$REMOTE_HOST
else
    echo "SSH key already installed on server."
fi

# Step 3: Rsync project to server
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
