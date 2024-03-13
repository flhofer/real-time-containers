#!/bin/bash
###############################
# Bash script by Florian Hofer
# last change: 19/02/2020
# ©2020 all rights reserved ☺
###############################

sudo docker build -f Dockerfile-ubuntu -t ubuntusched .
sudo docker run --rm -v "$PWD/../":/home -v /sys/fs/cgroup/:/sys/fs/cgroup/ --cap-add=SYS_NICE --cap-add=SYS_RESOURCE --cap-add=SYS_PTRACE --cap-add=IPC_LOCK --name stat1 -it ubuntusched

