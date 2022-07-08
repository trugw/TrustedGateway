#!/bin/bash
USAGE="cli-side.bash [-b] [-t sleep] <DST-SRV-IP4>"
SLEEP_SEC=1
while getopts "rht:" opt; do
    case "${opt}" in
        r)
            IPERF_OPTS="--reverse"
            echo "Reverse mode:  server will be the sender"
            ;;

        h)
            echo ${USAGE}
            exit 0
            ;;

        t)
            echo "Will sleep ${OPTARG} seconds between iterations"
            SLEEP_SEC=${OPTARG}
            ;;

        *)
            echo "Unknown option"
            echo ${USAGE}
            exit 1
            ;;
    esac
done
shift $((OPTIND-1))

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
    echo "Pass the target IP (of iperf server) as sole argument"
    echo ${USAGE}
    exit 1
fi

DST_IP4="$1"
IPERF3="`pwd`/../iperf/build/bin/iperf3"

# **********

ITERATIONS=20
IPERF_OPTS="${IPERF_OPTS} --client ${DST_IP4}"

RUN_IPERF="${IPERF3} $IPERF_OPTS"

# Run iPerf3
for i in `seq 1 ${ITERATIONS}`
do
    echo "Iteration: $i"
    date
    eval "${RUN_IPERF}" >/dev/null 2>/dev/null || exit 1
    sleep ${SLEEP_SEC}
done

exit 0
