#!/bin/bash

# Check for superuser privileges
if [ "$EUID" -ne 0 ]; then
  echo "Requires superuser privileges"
  exit 1
fi

# === 1. Set disk path and source file ===
DISK_DIRECTORY_PATH="../cp2m5/disks"
FILE_PATH="../src/dev/storage/simplessd/ims/db/build"
DB_PATH="../src/dev/storage/simplessd/ims/db"


if [ ! -d "$DISK_DIRECTORY_PATH" ]; then
    echo "Cannot find disk directory path"
    exit 1
fi  

if [ ! -d "$FILE_PATH" ]; then
    echo "Cannot find file path"
    exit 1
fi  

DISK_IMG="${DISK_DIRECTORY_PATH}/disk.img"
SRC_FILE="${FILE_PATH}/mydb_engine"
MOUNT_POINT="/mnt"

if [ -d "$DB_PATH" ]; then
    echo "Try to rebuild DB engine to check the DB engine is up to date"
    (cd "$DB_PATH" && make clean && make)
else
    echo "DB path is error ,the DB engine maybe is not newest"
fi


if [ ! -f "$SRC_FILE" ]; then
  echo "Cannot find file: $SRC_FILE"
  exit 1
fi

# Unmount if already mounted
if mountpoint -q "$MOUNT_POINT"; then
    echo "🧹 $MOUNT_POINT is mounted, unmounting..."
    umount "$MOUNT_POINT"
fi

# === 2. Create disk image and partition if not exists ===
if [ ! -f "$DISK_IMG" ]; then
    echo "Creating new disk image..."
    dd if=/dev/zero of="$DISK_IMG" oflag=direct bs=1M count=1024

    losetup -f --partscan "$DISK_IMG"
    LOOP_DEV=$(losetup -j "$DISK_IMG" | awk -F: '{print $1}')
    echo "🔄 Attached to $LOOP_DEV"

    # Create partition using fdisk
    echo "o
n
p
1


w" | fdisk "$LOOP_DEV"

    partprobe "$LOOP_DEV"
    PART="${LOOP_DEV}p1"

    if [ ! -b "$PART" ]; then
        echo "Waiting for partition device to appear..."
        sleep 1
    fi

    echo "Creating ext4 filesystem on $PART"
    mkfs.ext4 "$PART"
else
    losetup -f --partscan "$DISK_IMG"
    LOOP_DEV=$(losetup -j "$DISK_IMG" | awk -F: '{print $1}')
    PART="${LOOP_DEV}p1"
fi

# === 3. Mount and copy file ===
if [ ! -d "$MOUNT_POINT" ]; then
    echo "Create mount directory"
    mkdir -p "$MOUNT_POINT"
fi

mount "$PART" "$MOUNT_POINT"

echo "Copying $SRC_FILE to $MOUNT_POINT"
cp "$SRC_FILE" "$MOUNT_POINT"

echo "Done: $SRC_FILE copied to virtual disk at $MOUNT_POINT"

# === 4. Unmount and release loop device ===
if mountpoint -q "$MOUNT_POINT"; then
    echo "Unmounting $MOUNT_POINT..."
    umount "$MOUNT_POINT"
fi

if [ -n "$LOOP_DEV" ] && losetup "$LOOP_DEV" | grep -q "$DISK_IMG"; then
    echo "Releasing loop device $LOOP_DEV"
    losetup -d "$LOOP_DEV"
fi
