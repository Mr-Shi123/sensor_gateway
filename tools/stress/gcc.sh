#!/bin/bash
include_path="$(pwd)/../../include"
src_path="$include_path/../src"

#  echo $include_path
#  echo $src_path
gcc -I/$include_path -g -Wall -O0 \
       	-o stress_sensor stress_sensor.c \
       	$src_path/config.c \
       	$src_path/protocol.c \
	$src_path/utils.c \
	-pthread \
	-lsqlite3

