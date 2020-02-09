#!/bin/bash

cset set --cpu=0,4,8,12,16,20,24,28 user -m 0 --cpu_exclusive --mem_exclusive
cset set --cpu=2,6,10,14,18,22,26,30 system -m 1 --cpu_exclusive --mem_exclusive
cset shield --shield -k on --sysset=system --userset=user
cset proc --move --fromset=root --toset=system -k on --force

