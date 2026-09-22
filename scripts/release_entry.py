#!/usr/bin/env python3
# Copyright 2026 The Kyuzen Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The feed an installed copy reads to learn that a newer one exists.

One entry per release, newest first. An installed copy compares build
numbers, so a release that does not raise the build number is refused here
rather than published and quietly ignored.
"""

import datetime
import xml.etree.ElementTree as ElementTree

SPARKLE = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
CHANNEL_TITLE = 'Kyuzen'
CHANNEL_DESCRIPTION = 'New versions of Kyuzen'


class RefusedError(Exception):
    """Raised for a release that must not be published as it stands."""


def _empty_feed():
    ElementTree.register_namespace('sparkle', SPARKLE)
    rss = ElementTree.Element('rss', {'version': '2.0'})
    channel = ElementTree.SubElement(rss, 'channel')
    ElementTree.SubElement(channel, 'title').text = CHANNEL_TITLE
    ElementTree.SubElement(channel, 'description').text = CHANNEL_DESCRIPTION
    return rss


def entry(version, build, url, length, signature, notes, minimum_system,
          published=None):
    """One release, as the element that goes into the feed."""
    item = ElementTree.Element('item')
    ElementTree.SubElement(item, 'title').text = 'Kyuzen %s' % version
    when = published or datetime.datetime.now(datetime.timezone.utc)
    ElementTree.SubElement(item, 'pubDate').text = when.strftime(
        '%a, %d %b %Y %H:%M:%S %z')
    # Marked-up text, shown in the window that offers the update.
    ElementTree.SubElement(item, 'description').text = notes
    ElementTree.SubElement(item, '{%s}minimumSystemVersion' % SPARKLE).text = (
        minimum_system)
    ElementTree.SubElement(
        item, 'enclosure', {
            'url': url,
            'length': str(length),
            'type': 'application/octet-stream',
            '{%s}version' % SPARKLE: str(build),
            '{%s}shortVersionString' % SPARKLE: version,
            '{%s}edSignature' % SPARKLE: signature,
        })
    return item


def _builds(channel):
    for item in channel.findall('item'):
        enclosure = item.find('enclosure')
        if enclosure is None:
            continue
        try:
            yield int(enclosure.get('{%s}version' % SPARKLE, ''))
        except ValueError:
            continue


def feed_with(feed, item):
    """The feed with `item` added, newest first.

    `feed` is the current feed as text, or None for a feed nobody has written
    yet.
    """
    ElementTree.register_namespace('sparkle', SPARKLE)
    rss = _empty_feed() if feed is None else ElementTree.fromstring(feed)
    channel = rss.find('channel')
    if channel is None:
        raise RefusedError('the feed has no channel in it')

    build = int(item.find('enclosure').get('{%s}version' % SPARKLE))
    newest = max(_builds(channel), default=0)
    if build <= newest:
        raise RefusedError(
            'build %d does not come after %d, so no installed copy would take '
            'it' % (build, newest))

    channel.insert(list(channel).index(channel.findall('item')[0])
                   if channel.findall('item') else len(list(channel)), item)
    return ElementTree.tostring(rss, encoding='unicode', xml_declaration=True)
