#!/bin/bash

DIR=`dirname $0`
PPCMD=$1
COEF=$2
CNF=$3

SIMPCNF=`mktemp cnf.XXXXXX`
GRCNF=`mktemp gr.XXXXXX`
TD=`mktemp td.XXXXXX`

trap "rm -f $SIMPCNF $GRCNF $TD; exit 1" 2 5 6 11
trap "rm -f $SIMPCNF $GRCNF $TD; echo 'c o timeout'; exit 1" 15
trap "rm -f $SIMPCNF $GRCNF $TD" EXIT

SECONDS=0

echo "c o preprocessing (simp) starts ..."
$DIR/preproc $CNF $SIMPCNF $GRCNF $PPCMD
echo "c o preprocessing (simp) ends"
echo "c o elapsed time = $SECONDS s"
echo "c o "
echo "c o flow_cutter starts ..."
timeout 120 $DIR/flow_cutter_pace17 $GRCNF > $TD
echo "c o flow_cutter ends"
echo "c o elapsed time = $SECONDS s"
echo "c o "
echo "c o gpmc starts ..."
$DIR/gpmc -pp -cs=8000 -coef=$COEF $SIMPCNF $TD 

echo "c o total time = $SECONDS s"

