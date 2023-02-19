#!/bin/bash
cd server
make clean
make
cd ..
cd client
make clean
make
cd ..