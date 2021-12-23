#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2021 Matthew Leeds
# SPDX-License-Identifier: GPL-2.0+

"""
Generates AppStream metainfo for a set of Progressive Web Apps

Usage: pwa-metainfo-generator.py LIST.CSV

The CSV format expected looks like this:

https://app.diagrams.net/,Apache-2.0
https://pinafore.social/,AGPL-3.0-only
...

The output will be written to a file with the same name as the input but a .xml
file ending.

This tool uses the web app's manifest to fill out the AppStream info, so an
Internet connection is required
"""

import csv
import sys
import xml.etree.ElementTree as ET
import requests
import json
import hashlib
from urllib.parse import urljoin
from bs4 import BeautifulSoup

# w3c categories: https://github.com/w3c/manifest/wiki/Categories
# appstream categories: https://specifications.freedesktop.org/menu-spec/latest/apa.html
# left out due to no corresponding appstream category:
# entertainment, food, government, kids, lifestyle, personalization, politics, shopping, travel
w3c_to_appstream_categories = {
    "books": "Literature",
    "business": "Office",
    "education": "Education",
    "finance": "Finance",
    "fitness": "Sports",
    "games": "Game",
    "health": "MedicalSoftware",
    "magazines": "News",
    "medical": "MedicalSoftware",
    "music": "Music",
    "navigation": "Maps",
    "news": "News",
    "photo": "Photography",
    "productivity": "Office",
    "security": "Security",
    "social": "Chat",
    "sports": "Sports",
    "utilities": "Utility",
    "weather": "Utility"
}


def get_manifest_for_url(url):
    response = requests.get(url)
    response.raise_for_status()
    soup = BeautifulSoup(response.text, 'html.parser')
    manifest_path = soup.head.find('link', rel='manifest', href=True)['href']
    manifest_response = requests.get(urljoin(url, manifest_path))
    manifest_response.raise_for_status()
    return json.loads(manifest_response.text)

def copy_metainfo_from_manifest(url, app_component, manifest):
    # Short name seems more suitable in practice
    try:
      ET.SubElement(app_component, 'name').text = manifest['short_name']
    except KeyError:
      ET.SubElement(app_component, 'name').text = manifest['name']

    # Generate a unique app ID that meets the spec requirements. A different
    # app ID will be used upon install that is determined by the backing browser
    app_id = 'org.gnome.Software.WebApp-' + hashlib.sha1(url.encode('utf-8')).hexdigest()
    ET.SubElement(app_component, 'id').text = app_id

    # Avoid using maskable icons if we can, they don't have nice rounded edges
    normal_icon_exists = False
    for icon in manifest['icons']:
        if 'purpose' not in icon or icon['purpose'] == 'any':
            normal_icon_exists = True

    for icon in manifest['icons']:
        if 'purpose' in icon and icon['purpose'] != 'any' and normal_icon_exists:
            continue
        icon_element = ET.SubElement(app_component, 'icon')
        icon_element.text = urljoin(url, icon['src'])
        icon_element.set('type', 'remote')
        size = icon['sizes'].split(' ')[-1]
        icon_element.set('width', size.split('x')[0])
        icon_element.set('height', size.split('x')[1])

    if 'screenshots' in manifest:
        screenshots_element = ET.SubElement(app_component, 'screenshots')
        for screenshot in manifest['screenshots']:
            screenshot_element = ET.SubElement(screenshots_element, 'screenshot')
            screenshot_element.set('type', 'default')
            image_element = ET.SubElement(screenshot_element, 'image')
            image_element.text = urljoin(url, screenshot['src'])
            size = screenshot['sizes'].split(' ')[-1]
            image_element.set('width', size.split('x')[0])
            image_element.set('height', size.split('x')[1])
            if 'label' in screenshot:
                ET.SubElement(screenshot_element, 'caption').text = screenshot['label']

    if 'categories' in manifest:
        categories_element = ET.SubElement(app_component, 'categories')
        for category in manifest['categories']:
            try:
                mapped_category = w3c_to_appstream_categories[category]
                ET.SubElement(categories_element, 'category').text = mapped_category
            except KeyError:
                pass

    if 'description' in manifest:
        ET.SubElement(app_component, 'summary').text = manifest['description']

def main():
    if len(sys.argv) != 2 or not sys.argv[1].endswith('.csv'):
        print('Usage: {} input.csv'.format(sys.argv[0]))
        sys.exit(1)

    input_filename = sys.argv[1]
    out_filename = input_filename.replace('.csv', '.xml')
    with open(input_filename) as input_csv:
        components = ET.Element('components')
        components.set('version', '0.15')
        reader = csv.reader(input_csv)
        for app in reader:
            app_component = ET.SubElement(components, 'component')
            app_component.set('type', 'webapp')

            launchable = ET.SubElement(app_component, 'launchable')
            launchable.set('type', 'url')
            launchable.text = app[0]

            url = ET.SubElement(app_component, 'url')
            url.set('type', 'homepage')
            url.text = app[0]

            project_license = ET.SubElement(app_component, 'project_license')
            project_license.text = app[1]

            # metadata license is a required field but we don't have one, assume FSFAP?
            metadata_license = ET.SubElement(app_component, 'metadata_license')
            metadata_license.text = 'FSFAP'

            copy_metainfo_from_manifest(app[0], app_component, get_manifest_for_url(app[0]))

    tree = ET.ElementTree(components)
    ET.indent(tree)
    tree.write(out_filename, xml_declaration=True, encoding='utf-8', method='xml')


if __name__=='__main__':
        main()
