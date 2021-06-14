#!/bin/bash
echo "Patching the compiled npfctl.js file"

OUT_DIR="../out"
if [ ! -d "${OUT_DIR}" ]; then
    echo "Missing a out directory:"
    echo "${OUT_DIR}"
    echo "Run this script from the tools/ directory"
    { echo "... FAIL"; exit 1; }
fi

MAIN_JS="${OUT_DIR}/npfctl.js"
REPLACE_JS="../proxy-js/load_check_worker.js"
if   [ ! -f "${MAIN_JS}" ]   || \
     [ ! -f "${REPLACE_JS}" ]; then
    echo "At least one of the required files is missing"
    echo "Required:"
    echo "${MAIN_JS}"
    echo "${REPLACE_JS}"
    { echo "... FAIL"; exit 1; }
fi

# Ugly hack, but what we basically do is, we replace
# multiple lines in MAIN_JS with the content of REPLACE_JS
NEW_MAIN="./.tmp.js"

sed '/\/\/ Worker/q' ${MAIN_JS} > "${NEW_MAIN}" && \
cat "${REPLACE_JS}" >> "${NEW_MAIN}" && \
sed -n '/WebGLClient\.prefetch()/,$p' ${MAIN_JS} >> "${NEW_MAIN}" && \
mv "${NEW_MAIN}" "${MAIN_JS}" \
|| { echo "... FAILED"; exit 1; }

echo "... done" && exit 0