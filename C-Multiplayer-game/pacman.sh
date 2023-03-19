#!/bin/bash

cd pacman
for i in $(seq 3)
do
./bin/client_b 222 3771 a bbb < ~/in.txt &>/dev/null & 
done
