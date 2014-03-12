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
    back_button = app.child('Go back')
    install_button = app.child(roleName='frame', name='Software', recursive=False).child(roleName='panel', name='', recursive=False).child(roleName='push button', name='Install')
    remove_button = app.child(roleName='frame', name='Software', recursive=False).child(roleName='panel', name='', recursive=False).child(roleName='push button', name='Remove')

    overview_page = app.child('Overview page')
    installed_page = app.child('Installed page')
    updates_page = app.child('Updates page')
    search_page = app.child('Search page')
    details_page = app.child('Details page')

    website_details_button = details_page.child(roleName='push button', name='Website')
    history_details_button = details_page.child(roleName='push button', name='History')
    launch_details_button = details_page.child(roleName='push button', name='Launch')

    search_page_listbox = search_page.child(roleName='list box')

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

    """Details page test section"""
    search_page_listbox.child(roleName='label', name='GNU Image Manipulation Program').click()
    doDelay(4)
    assert (not overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (details_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (install_button.getState().contains(pyatspi.STATE_SHOWING) or remove_button.getState().contains(pyatspi.STATE_SHOWING))
    assert (back_button.getState().contains(pyatspi.STATE_SHOWING))
    assert (website_details_button.getState().contains(pyatspi.STATE_VISIBLE))
    assert (history_details_button.getState().contains(pyatspi.STATE_VISIBLE))

    if install_button.getState().contains(pyatspi.STATE_SHOWING):
        assert (not launch_details_button.getState().contains(pyatspi.STATE_VISIBLE))
    else:
        assert (launch_details_button.getState().contains(pyatspi.STATE_VISIBLE))

    back_button.click()
    assert (not all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (search_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not install_button.getState().contains(pyatspi.STATE_SHOWING))
    assert (not remove_button.getState().contains(pyatspi.STATE_SHOWING))
    assert (not back_button.getState().contains(pyatspi.STATE_SHOWING))

    keyCombo("Escape")
    assert (all_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not installed_button.getState().contains(pyatspi.STATE_ARMED))
    assert (not updates_button.getState().contains(pyatspi.STATE_ARMED))
    assert (overview_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not installed_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not updates_page.getState().contains(pyatspi.STATE_SHOWING))
    assert (not search_page.getState().contains(pyatspi.STATE_SHOWING))

    super_menu = root.application('gnome-shell').child(roleName='menu', name='Software')
    super_menu.click()
    root.application('gnome-shell').child(roleName='label', name='Software Sources').click()
    assert (len(app.children) == 2)
    sources_dialog = app.children[-1]
    assert (sources_dialog.child(roleName='label', name='Software Sources'))
finally:
    os.system("killall gnome-software")
