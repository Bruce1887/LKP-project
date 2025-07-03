#! /bin/bash

make KERNELDIR=../lx/linux-6.5.7

if [ $? -ne 0 ]; then
	echo "Build failed"
	exit 1
fi

cp ouichefs.ko ../lx/share/

echo "Build and copy successful"
exit 0