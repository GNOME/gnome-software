#!/bin/bash

pwd
echo "args: $@"
gdb --quiet --batch --nh --ex r --ex "t a a bt"  --ex quit --args plugins/flatpak/gs-self-test-flatpak
