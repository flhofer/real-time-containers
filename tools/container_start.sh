#!/bin/bash
###############################
# Bash script by Florian Hofer
# last change: 04/09/2019
# ©2018 all rights reserved ☺
###############################

sudo docker build -t ubuntusched .
sudo docker run -v "$PWD/../":/home -v /sys/fs/cgroup/:/sys/fs/cgroup/ --cap-add=SYS_NICE --cap-add=SYS_RESOURCE --cap-add=SYS_PTRACE --cap-add=IPC_LOCK --name stat1 -it ubuntusched

