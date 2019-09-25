#!/bin/bash
###############################
# Bash script by Florian Hofer
# last change: 04/09/2019
# ©2018 all rights reserved ☺
###############################

sudo docker build -t ubuntubuild -f Dockerfile-Build-ubuntu .
sudo docker run --rm -v "$PWD":/home --name build1 -it ubuntubuild

sudo docker build -t debianbuild -f Dockerfile-Build-debian .
sudo docker run --rm -v "$PWD":/home --name build2 -it debianbuild

sudo docker build -t fedorabuild -f Dockerfile-Build-fedora .
sudo docker run --rm -v "$PWD":/home --name build3 -it fedorabuild

sudo docker build -t alpinebuild -f Dockerfile-Build-alpine .
sudo docker run --rm -v "$PWD":/home --name build4 -it alpinebuild

