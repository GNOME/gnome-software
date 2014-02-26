#! /usr/bin/python

# This a simple test, using the dogtail framework:
#
# Click on All, Installed, Updates and check that the button state
# and the main page content update as expected. Type a few characters
# on the overview page, hit Enter, and verify that we end up on the
# search page. Hit Escape and verify that we go back to the overview
# page.

from gi.repository import Gio

settings = Gio.Settings.new("org.gnome.desktop.interface")
settings.set_boolean ("toolkit-accessibility", True)

import os
from dogtail.tree import *
from dogtail.utils import *
from dogtail.procedural import *

try:
    run('gnome-software')

    app = root.application('gnome-software');

    all_button = app.child('All')
    installed_button = app.child('Installed')
    updates_button = app.child('Updates')

    overview_page = app.child('Overview page')
    installed_page = app.child('Installed page')
    updates_page = app.child('Updates page')
    search_page = app.child('Search page')

    all_button.click()
    assert (all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))

    installed_button.click()
    assert (not all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))

    updates_button.click()
    assert (not all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))

    installed_button.click()
    assert (not all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))

    all_button.click()
    assert (all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))

    type("gimp\n")
    doDelay(2)
    assert (not all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (search_page.getState().contains(pyatspi.STATE_SHOWING))

    keyCombo("Escape")
    assert (all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))
finally:
    os.system("killall gnome-software")
