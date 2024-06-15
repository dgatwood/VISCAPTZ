#!/bin/sh

IFS="
"

read DATA

DATA="$(echo "$DATA" | sed 's/^M *//' | sed 's/ *z$//' | tr -d ' ')"

PREVDATA=""
FIRST=1
MINX="0"
MINY="0"
MAXX="0"
MAXY="0"

while [ "$DATA" != "$PREVDATA" ] ; do
  FIRSTPAIR="$(echo "$DATA" | sed 's/L.*$//')"
  PREVDATA="$DATA"
  DATA="$(echo "$DATA" | sed 's/^[^L]*L//')"
  # echo "PAIR: $FIRSTPAIR"
  X="$(echo "$FIRSTPAIR" | sed 's/,.*$//')"
  Y="$(echo "$FIRSTPAIR" | sed 's/^.*,//')"
  if [ "$FIRST" = "1" ] ; then
    MINX="$X"
    MINY="$Y"
    MAXX="$X"
    MAXY="$Y"
    FIRST=0
  else
    if [ $(echo "$X < $MINX" | bc -l) == "1" ] ; then
      MINX="$X"
    fi
    if [ $(echo "$Y < $MINY" | bc -l) == "1" ] ; then
      MINY="$Y"
    fi
    if [ $(echo "$X > $MAXX" | bc -l) == "1" ] ; then
      MAXX="$X"
    fi
    if [ $(echo "$Y > $MAXY" | bc -l) == "1" ] ; then
      MAXY="$Y"
    fi
  fi
done

echo "MINX: $MINX"
echo "MAXX: $MAXX"
echo "MINY: $MINY"
echo "MAXY: $MAXY"

CENTERX="$(echo "($MINX + $MAXX) / 2.0" | bc -l)"
CENTERY="$(echo "($MINY + $MAXY) / 2.0" | bc -l)"
RADIUS="$(echo "($MAXX - $MINX) / 2.0" | bc -l)"

echo "$CENTERX,$CENTERY"
echo "RADIUS: $RADIUS"

echo '<circle cx="'"$CENTERX"'" cy="'"$CENTERY"'" r="0.01" stroke="blue" fill="blue" stroke-width="1" />'

