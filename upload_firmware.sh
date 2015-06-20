#!/bin/sh
sudo dfu-util -v -c 1 -i 0 -a 0 -s 0x08000000 -D musicbox.bin
