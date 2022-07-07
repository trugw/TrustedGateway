#!/bin/bash
echo "Updating checksums ..."

OUT_DIR="../out"
if [ ! -d "${OUT_DIR}" ]; then
    echo "Missing a webapp (out) directory:"
    echo "${OUT_DIR}"
    echo "Run this script from the tools/ directory"
    echo "Compile the WebApp before running this script"
    { echo "... FAIL"; exit 1; }
fi

MAIN_JS="${OUT_DIR}/npfctl.js"
WORKER_JS="${OUT_DIR}/npfctl.worker.js"
WA_WASM="${OUT_DIR}/npfctl.wasm"
if   [ ! -f "${MAIN_JS}" ]   || \
     [ ! -f "${WORKER_JS}" ] || \
     [ ! -f "${WA_WASM}" ] ; then
    echo "At least one of the WebApp files is not existing"
    echo "Required:"
    echo "${MAIN_JS}"
    echo "${WORKER_JS}"
    echo "${WA_WASM}"
    { echo "... FAIL"; exit 1; }
fi

WEB_ASSETS_DIR="../../configservice/assets"
if [ ! -d "${WEB_ASSETS_DIR}" ]; then
    echo "WebServer assets directory missing"
    echo "Missing: ${WEB_ASSETS_DIR}"
    { echo "... FAIL"; exit 1; }
fi

HTML_FILES=( `find ${WEB_ASSETS_DIR} -maxdepth 1 -name "*.html"` )
if [ ${#HTML_FILES[@]} -eq 0 ]; then
    echo "Found no HTML files in WebServer assets folder"
    { echo "... FAIL"; exit 1; }
fi

# Checksum Update Chain:
# 1. update WA_WASM sha256 checksum in worker
# 2. update WORKER_JS sha256 checksum in main
# 3. update MAIN_JS SRI sha384 checksum in all HTML files

# 1.
WASM_HASH=`openssl dgst -r -sha256 "${WA_WASM}" | cut -d' ' -f1`
echo "New WASM Hash: ${WASM_HASH}"
echo "Updating ${WORKER_JS}"
sed -i -e "s/^\(var refWasmHash ='\).*\(';\)/\1${WASM_HASH}\2/" "${WORKER_JS}"

# 2.
WORKER_HASH=`openssl dgst -r -sha256 "${WORKER_JS}" | cut -d' ' -f1`
echo "New Worker Hash: ${WORKER_HASH}"
echo "Updating ${MAIN_JS}"
sed -i -e "s/^\(var refWorkerHash = '\).*\(';\)$/\1${WORKER_HASH}\2/" "${MAIN_JS}"

# 3.
MAIN_SRI_HASH=`openssl dgst -r -sha384 -binary "${MAIN_JS}" | openssl base64 -A`
echo "New SRI hash: ${MAIN_SRI_HASH}"
# replace "/" in base64 with "\/" to avoid problems in sed
MAIN_SRI_HASH=`echo ${MAIN_SRI_HASH} | sed "s/\//\\\\\\\\\//g"`
for h in "${HTML_FILES[@]}"
do
    echo "Updating $h"
    sed -i -e "s/\(npfctl_script_sri = \"sha384-\)[A-Za-z0-9+/]*\(\";\)$/\1${MAIN_SRI_HASH}\2/" "$h"
done

echo "... done" && exit 0
