#!/bin/bash

set +e

case "$1" in
  --log-file)
    log_file="$2"
    shift
    shift
    ;;
  *)
    log_file="_build/meson-logs/testlog.json"
esac

# FIXME: The tests need to be run as root
if ! [ $(id -u) = 0 ]; then
    echo "Tests need to be run as root"
    exit 1
fi

meson test \
        -C _build \
        --timeout-multiplier ${MESON_TEST_TIMEOUT_MULTIPLIER} \
        --no-suite flaky \
        "$@"

exit_code=$?

python3 .gitlab-ci/meson-junit-report.py \
        --project-name=gnome-software \
        --job-id "${CI_JOB_NAME}" \
        --output "_build/${CI_JOB_NAME}-report.xml" \
        "${log_file}"

exit $exit_code
