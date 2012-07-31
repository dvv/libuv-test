#!/bin/sh

for i in 1 2 3 4 5 6 7 8 9 10; do
  ab -n100000 -c1000 http://127.0.0.1:8080/ &
done
