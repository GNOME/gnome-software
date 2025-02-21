#!/usr/bin/env python3
#
# Copyright 2025 GNOME Foundation, Inc.
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Original author: Philip Withnall <pwithnall@gnome.org>

"""
Generate a HTML index for the help pages, for deployment to
pages.gitlab.gnome.org.
"""

import argparse
import os
import pathlib

import gi
from lxml import etree

gi.require_version("GnomeDesktop", "3.0")
from gi.repository import GnomeDesktop  # noqa: E402


def parse_linguas(help_srcdir):
    """Get a list of language names from help_srcdir/LINGUAS."""
    linguas_path = os.path.join(help_srcdir, "LINGUAS")
    with open(linguas_path, "r", encoding="utf-8") as linguas:
        return [l.strip() for l in linguas.readlines() if not l.startswith("#")]


def locale_name(locale):
    """Look up the human readable name of the given locale.

    locale could be, for example, `de` or `pt_BR`.
    """
    # This data actually comes from the iso-codes package, in the iso_639 and
    # iso_3166 gettext domains, with extra logic to suppress the country name
    # if the language is ‘unique’.
    #
    # Without appending .UTF-8, these come out as (eg):
    #   Portuguese (Brazil) [ISO-8859-1]
    name = GnomeDesktop.get_language_from_locale(f"{locale}.UTF-8")
    return name or locale


def format_index(published_subdir, linguas):
    """Format the HTML."""
    translation_links = [
        f"""<div class=\"linkdiv\">
          <a class=\"linkdiv\" href=\"{published_subdir}/{l}/index.html\"
             title=\"Software help ({locale_name(l)})\">
            <span class=\"title\">Software help ({locale_name(l)})</span>
          </a>
        </div>"""
        for l in linguas
    ]

    # There are a lot of nested divs here, but it means we can reuse the yelp
    # CSS from the C-locale help directory
    return f"""\
    <!DOCTYPE html>
    <html>
      <head>
        <meta http-equiv="Content-Type" content="text/html; charset=UTF-8"/>
        <title>Software</title>
        <link rel="stylesheet" type="text/css" href="{published_subdir}/C/C.css"/>
      </head>
      <body>
        <main>
          <div class="page">
            <article>
              <div class="hgroup pagewide">
                <h1 class="title">
                  <span class="title">
                    <span class="app">Software</span>
                  </span>
                </h1>
              </div>
              <div class="region">
                <div class="contents pagewide">
                  <div class="links topiclinks">
                    <div class="inner">
                      <div class="region">
                        <div class="links-divs">
                          <div class="linkdiv">
                            <a class="linkdiv"
                               href="{published_subdir}/C/index.html"
                               title="Software help (English)">
                              <span class="title">Software help (English)</span>
                            </a>
                          </div>
                          {'\n'.join(translation_links)}
                        </div>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </article>
          </div>
        </main>
      </body>
    </html>"""


def main():
    parser = argparse.ArgumentParser(
        description="Generate a HTML index for the help pages, for "
        "deployment to pages.gitlab.gnome.org.",
    )
    parser.add_argument(
        "output",
        help="File to save output to",
        type=argparse.FileType("w", encoding="utf-8"),
    )
    parser.add_argument(
        "help_srcdir",
        help="Source help directory (for example, ‘$srcdir/help’)",
        type=pathlib.Path,
    )
    parser.add_argument(
        "--published-subdir",
        help="Path of top of published help directory relative to published "
        "location of this index file",
        default="help",
    )
    args = parser.parse_args()

    # Generate the index page
    linguas = parse_linguas(args.help_srcdir)
    index_str = format_index(args.published_subdir, linguas)

    # Reformat the XML whitespace nicely
    parser = etree.XMLParser()
    root = etree.XML(index_str, parser=parser)
    etree.indent(root)

    args.output.write(etree.tostring(root, encoding="unicode"))


if __name__ == "__main__":
    main()
