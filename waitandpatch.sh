#!/bin/sh
patcher="$1"

pid="$(pidof sekiro.exe)"
pidof_exit=$?
while [ $pidof_exit -eq 1 ]
do
	pid="$(pidof sekiro.exe)"
	pidof_exit=$?
done

shift
"$patcher" $pid $@
