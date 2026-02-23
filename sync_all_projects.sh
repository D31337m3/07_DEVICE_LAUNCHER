#!/bin/bash

# Sync entire local projects directory to remote server
# Usage: Run from the parent directory of your 'projects' folder

LOCAL_PROJECTS_DIR="./projects/"
REMOTE_USER="root"
REMOTE_HOST="vortexlabs.online"
REMOTE_PROJECTS_DIR="~/projects"

# Step 1: Ensure rsync is installed on remote server
echo "Checking rsync installation on remote server..."
ssh $REMOTE_USER@$REMOTE_HOST "which rsync || (echo 'Installing rsync...' && apt-get install -y rsync)"

# Step 2: Warn about shell cleanliness
echo "If you still get protocol errors, check your remote shell config files (.bashrc, .profile, .bash_profile) for any output (echo, printf, etc.) outside conditional blocks. Remove/comment out any output-producing lines."

# Step 3: Sync projects directory
rsync -avz --delete "$LOCAL_PROJECTS_DIR" "$REMOTE_USER@$REMOTE_HOST:$REMOTE_PROJECTS_DIR"

echo "Sync complete!"
