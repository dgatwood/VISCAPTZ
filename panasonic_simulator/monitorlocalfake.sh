#!/bin/sh

while true ; do DATA="$(curl -s 'http://127.0.0.1/cgi-bin/aw_ptz?cmd=%23GZ&res=1' | sed 's/^gz//')"; clear; echo "$DATA" ; done
