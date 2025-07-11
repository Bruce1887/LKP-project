#!/bin/bash

# Write 2 MB of 'A' characters to big.txt
head -c 2097152 < /dev/zero | tr '\0' 'A' > /mnt/ouiche/big.txt

# Write 140 'A' characters to medium.txt
printf 'A%.0s' {1..140} > /mnt/ouiche/medium.txt

# Write "hej" followed by newline to small.txt
echo hej > /mnt/ouiche/small.txt

echo "Files created in /mnt/ouiche:"
