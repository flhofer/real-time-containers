#!/bin/bash 

for i in {10..49}; do 
  eval "sudo rm log-rt-app-tst-${i}/log-thread1-0.log"
done

./containers.sh test 10
./containers.sh stop 1*
mkdir log/1-1
cp log/* log/1-1

./containers.sh test 10 11
./containers.sh stop 1*
mkdir log/1-2
cp log/* log/1-2

./containers.sh test 10 11 12
./containers.sh stop 1*
mkdir log/1-3
cp log/* log/1-3

./containers.sh test 10 11 12 13
./containers.sh stop 1*
mkdir log/1-4
cp log/* log/1-4

./containers.sh test 10 11 12 13 14
./containers.sh stop 1*
mkdir log/1-5
cp log/* log/1-5

./containers.sh test 10 11 12 13 14 15
./containers.sh stop 1*
mkdir log/1-6
cp log/* log/1-6

./containers.sh test 10 11 12 13 14 15 16
./containers.sh stop 1*
mkdir log/1-7
cp log/* log/1-7

./containers.sh test 10 11 12 13 14 15 16 17
./containers.sh stop 1*
mkdir log/1-8
cp log/* log/1-8

./containers.sh test 10 11 12 13 14 15 16 17 18
./containers.sh stop 1*
mkdir log/1-9
cp log/* log/1-9

./containers.sh test 10 11 12 13 14 15 16 17 18 19
./containers.sh stop 1*
mkdir log/1-10
cp log/* log/1-10

./containers.sh test 20
./containers.sh stop 2*
mkdir log/2-1
cp log/* log/2-1

./containers.sh test 20 21
./containers.sh stop 2*
mkdir log/2-2
cp log/* log/2-2

./containers.sh test 30
./containers.sh stop 3*
mkdir log/3-1
cp log/* log/3-1

./containers.sh test 30 31
./containers.sh stop 3*
mkdir log/3-2
cp log/* log/3-2

./containers.sh test 30 31 32
./containers.sh stop 3*
mkdir log/3-3
cp log/* log/3-3

./containers.sh test 40
./containers.sh stop 4*
mkdir log/4-1
cp log/* log/4-1

./containers.sh test 40 41
./containers.sh stop 4*
mkdir log/4-2
cp log/* log/4-2

./containers.sh test 40 41 42
./containers.sh stop 4*
mkdir log/4-3
cp log/* log/4-3

./containers.sh test 40 41 42 43
./containers.sh stop 4*
mkdir log/4-4
cp log/* log/4-4

./containers.sh test 40 41 42 43 44
./containers.sh stop 4*
mkdir log/4-5
cp log/* log/4-5

./containers.sh test 40 41 42 43 44 45
./containers.sh stop 4*
mkdir log/4-6
cp log/* log/4-6

./containers.sh test 40 41 42 43 44 45 46
./containers.sh stop 4*
mkdir log/4-7
cp log/* log/4-7

./containers.sh test 40 41 42 43 44 45 46 47
./containers.sh stop 4*
mkdir log/4-8
cp log/* log/4-8

./containers.sh test 40 41 42 43 44 45 46 47 48
./containers.sh stop 4*
mkdir log/4-9
cp log/* log/4-9

./containers.sh test 40 41 42 43 44 45 46 47 48 49
./containers.sh stop 4*
mkdir log/4-10
cp log/* log/4-10

