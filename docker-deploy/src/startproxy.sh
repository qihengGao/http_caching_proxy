#!/bin/bash
make clean
make
./runproxy &
while true ; do continue ; done