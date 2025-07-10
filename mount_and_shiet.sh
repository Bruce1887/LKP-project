#! /bin/bash

insmod ouichefs.ko
if [ $? -ne 0 ]; then
	echo "Failed to insert ouichefs module"
	exit 1
fi

mkdir -p /mnt/ouiche

losetup -fP test.img
if [ $? -ne 0 ]; then
	echo "Failed to set up loop device"
	exit 1
fi

mount -t ouichefs /dev/loop0 /mnt/ouiche

if [ $? -ne 0 ]; then
	echo "Failed to mount ouichefs"
	exit 1
fi

echo "ouichefs mounted successfully at /mnt/ouiche"


printf 'A%.0s' {1..140} > /mnt/ouiche/medium.txt
echo hej > /mnt/ouiche/small.txt

echo "Files created in /mnt/ouiche:"

# Note: will only change directories in your current shell if you source it
cd /mnt/ouiche
