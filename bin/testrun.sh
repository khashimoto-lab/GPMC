#!/bin/bash

DIR=`dirname $0`
PPCMD=$1
COEF=$2
CNF=$3

TIMEOUT_TD=60

SIMPCNF=`mktemp cnf.XXXXXX`
GRCNF=`mktemp gr.XXXXXX`
TD=`mktemp td.XXXXXX`

trap "rm -f $SIMPCNF $GRCNF $TD; exit 1" 2 5 6 11
trap "rm -f $SIMPCNF $GRCNF $TD; echo 'c o timeout'; exit 1" 15
trap "rm -f $SIMPCNF $GRCNF $TD" EXIT

SECONDS=0

echo "c o preprocessing (simp) starts ..."
$DIR/preproc $CNF $SIMPCNF $GRCNF $PPCMD
STATUS=$?
echo "c o preprocessing (simp) ends"
echo "c o elapsed time = $SECONDS s"

if [ $STATUS -eq 10 ]; then
    exit 0
fi

if [ $STATUS -ne 20 ]; then
    echo "c o "
    echo "c o flow_cutter starts ..."
    timeout $TIMEOUT_TD $DIR/flow_cutter_pace17 $GRCNF > $TD
    echo "c o flow_cutter ends"
    echo "c o elapsed time = $SECONDS s"
    echo "c o "
    echo "c o gpmc starts ..."
    $DIR/gpmc -pp -cs=8000 -coef=$COEF $SIMPCNF $TD 
else
    echo "c o "
    echo "c o gpmc starts ..."
    $DIR/gpmc -pp -cs=8000 $SIMPCNF
fi

echo "c o total time = $SECONDS s"

