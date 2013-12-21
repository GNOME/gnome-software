#! /usr/bin/python

# This a simple test, using the dogtail framework:
#
# Activate the app via the org.gtk.Application bus interface.
# Check that the expected actions are exported on the session bus.
# Activate each action and verify the result.

import os
import dbus
from dogtail.tree import *
from dogtail.utils import *
from dogtail.procedural import *

#run('gnome-software')

app = root.application('org.gnome.Software');

bus = dbus.SessionBus()
proxy = bus.get_object('org.gnome.Software', '/org/gnome/Software')
dbus_app = dbus.Interface(proxy, 'org.gtk.Application')
dbus_app.Activate([])

doDelay(1)
assert (len(app.children) == 1)

dbus_actions = dbus.Interface(proxy, 'org.gtk.Actions')

names = dbus_actions.List()
assert (u'quit' in names)
assert (u'about' in names)

dbus_actions.Activate(u'about', [], [])

doDelay (1)
assert (len(app.children) == 2)
app.dialog('About Software').child('Close').click()
doDelay (1)
assert (len(app.children) == 1)

dbus_actions.Activate(u'quit', [], [])
assert (len(app.children) == 0)
