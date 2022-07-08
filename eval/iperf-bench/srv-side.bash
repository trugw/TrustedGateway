#!/bin/bash
USAGE="srv-side.bash [-f <res_file_prefix>] <LISTEN-IP4>"
while getopts "f:h" opt; do
    case "${opt}" in
        f)
            RES_FILE_PRFX="${OPTARG}"
            echo "Result file prefix: ${RES_FILE_PRFX}"
            ;;

        h)
            echo ${USAGE}
            exit 0
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
    echo "Pass the host IP (to listen on) as sole argument"
    echo "srv-side.bash <LISTEN-IP4>"
    exit 1
fi

HOST_IP4="$1"
IPERF3="`pwd`/../iperf/build/bin/iperf3"
RES_FILE="${RES_FILE_PRFX}_`date "+%Y_%m_%d__%H_%M_%S"`.json"

echo "Results will be written to ${RES_FILE} in JSON format"
${IPERF3} --server --bind ${HOST_IP4} --json \
    > ${RES_FILE}

exit 0
