#!/bin/bash
include_path="$(pwd)/../../include"
src_path="$include_path/../src"

#  echo $include_path
#  echo $src_path
gcc -I/$include_path -g -Wall -O0 \
       	-o sim_sensor sim_sensor.c \
       	$src_path/utils.c \
       	$src_path/protocol.c \
	-pthread


