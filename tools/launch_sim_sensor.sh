#!/bin/bash
gcc -I../include -g -Wall -O0 -o sim_sensor sim_sensor.c ../src/utils.c  ../src/protocol.c -pthread


