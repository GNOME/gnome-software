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

# Remove Flatpak triggers, to speed up the tests (specifically on Fedora,
# they could even break the tests by a deadlock or something like that).
if [ -d /usr/share/flatpak/triggers ]; then
  sudo rm -rf /usr/share/flatpak/triggers
fi

# FIXME: The tests should be isolated and use mock services so they do not
# require a functioning system bus. This will have to do for now though.
sudo mkdir -p /run/dbus
sudo mkdir -p /var
sudo ln -s /var/run /run
sudo dbus-daemon --system --fork
#sudo /usr/lib/polkit-1/polkitd --no-debug &
#sudo /usr/libexec/fwupd/fwupd --verbose &

# FIXME: Running the flatpak tests as root means the system helper doesn’t
# need to be used, which makes them run a lot faster.
sudo \
dbus-run-session -- \
meson test \
        -C _build \
        --timeout-multiplier ${MESON_TEST_TIMEOUT_MULTIPLIER} \
        --no-suite flaky \
	--print-errorlogs \
        "$@"

exit_code=$?

python3 .gitlab-ci/meson-junit-report.py \
        --project-name=gnome-software \
        --job-id "${CI_JOB_NAME}" \
        --output "_build/${CI_JOB_NAME}-report.xml" \
        "${log_file}"

exit $exit_code
