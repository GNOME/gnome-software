Debugging GNOME Software
========================

GNOME Software can be a little harder to debug than other applications, because
it is typically started early in a user’s session, and it runs in the
background.

Runtime debugging
---

If gnome-software is already running and you want to enable debug output from
this point onward, run:
```
gnome-software --verbose
```

This will tell the running instance to enable verbose logging, without needing
to restart.

By default, all log messages from gnome-software are sent to
[the systemd journal](https://www.freedesktop.org/software/systemd/man/systemd-journald.service.html).
Messages, warnings and errors are always logged; debug and info messages are
only logged if verbose output is enabled.

To view the messages from the currently running gnome-software process, run:
```
journalctl --user --boot "$(which gnome-software)"
```

Start-time debugging
---

If you are starting gnome-software manually, you have a little more control over
log output.

The `--verbose` command line argument will enable verbose mode for the newly
started process. If run from a terminal, all log messages will be printed on the
terminal rather than being sent to the systemd journal. As with journal logging,
debug and info messages will only be printed if they are enabled.

An alternative to the `--verbose` argument are the following environment
variables:
 * `GS_DEBUG=1`
 * `GS_DEBUG_NO_TIME=1`
 * `G_MESSAGES_DEBUG=*`

`GS_DEBUG` and `GS_DEBUG_NO_TIME` are equivalent to `--verbose`, enabling output
of all log messages. Normally messages are printed with timestamps, but
`GS_DEBUG_NO_TIME` disables that if you want to save space.

`G_MESSAGES_DEBUG=all` is equivalent to the above, but other values can be
passed to it to filter debug log output to certain message domains.

Persistent debugging
---

Verbose debug output may be made persistent by modifying GNOME Software's systemd
service to set an extra environment variable:

```
mkdir -p ~/.config/systemd/user/gnome-software.service.d
cat <<EOF > ~/.config/systemd/user/gnome-software.service.d/debug.conf
[Service]
Environment=GS_DEBUG=1
EOF
systemctl --user daemon-reload
systemctl --user restart gnome-software.service
```

Note that this will produce a lot of debug output which will consume a
noticeable amount of space in your systemd journal over time.

Forcing an auto-update check
---

GNOME Software automatically checks for updates in the background based on a
timer. To force a background auto-update check, this timer can be reset and the
currently running GNOME Software instance will schedule a check:

```
for key in $(gsettings list-keys org.gnome.software | grep timestamp); do gsettings reset org.gnome.software $key; done
```
