#!/usr/bin/env python3
# Copyright 2026 The Kyuzen Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The feed a released copy reads, and what may go into it.

Run: python3 arcium/test/release_entry_unittest.py
"""

import os
import sys
import unittest
import xml.etree.ElementTree as ElementTree

sys.path.insert(
    0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..',
                    'scripts'))

import release_entry  # noqa: E402

SPARKLE = 'http://www.andymatuschak.org/xml-namespaces/sparkle'


def items(feed):
    return ElementTree.fromstring(feed).findall('./channel/item')


class EntryTest(unittest.TestCase):

    def entry(self, version='0.1.1', build='2', **kwargs):
        defaults = dict(
            version=version,
            build=build,
            url='https://example.invalid/Kyuzen-%s.dmg' % version,
            length=1234,
            signature='a-signature',
            notes='<p>Something changed.</p>',
            minimum_system='13.0',
        )
        defaults.update(kwargs)
        return release_entry.entry(**defaults)

    def test_an_entry_names_everything_an_updater_needs(self):
        feed = release_entry.feed_with(None, self.entry())
        item, = items(feed)
        enclosure = item.find('enclosure')
        self.assertEqual('2', enclosure.get('{%s}version' % SPARKLE))
        self.assertEqual('0.1.1',
                         enclosure.get('{%s}shortVersionString' % SPARKLE))
        self.assertEqual('https://example.invalid/Kyuzen-0.1.1.dmg',
                         enclosure.get('url'))
        self.assertEqual('1234', enclosure.get('length'))
        self.assertEqual('a-signature',
                         enclosure.get('{%s}edSignature' % SPARKLE))
        self.assertEqual('13.0',
                         item.find('{%s}minimumSystemVersion' % SPARKLE).text)
        self.assertIn('Something changed.',
                      item.find('description').text)

    def test_the_newest_release_is_first(self):
        feed = release_entry.feed_with(None, self.entry('0.1.0', '1'))
        feed = release_entry.feed_with(feed, self.entry('0.1.1', '2'))
        first, second = items(feed)
        self.assertEqual(
            '2', first.find('enclosure').get('{%s}version' % SPARKLE))
        self.assertEqual(
            '1', second.find('enclosure').get('{%s}version' % SPARKLE))

    def test_a_build_number_that_does_not_increase_is_refused(self):
        # An installed copy compares build numbers, so a release that does not
        # raise it would be offered to nobody and hide the one before it.
        feed = release_entry.feed_with(None, self.entry('0.1.1', '2'))
        with self.assertRaises(release_entry.RefusedError):
            release_entry.feed_with(feed, self.entry('0.1.2', '2'))
        with self.assertRaises(release_entry.RefusedError):
            release_entry.feed_with(feed, self.entry('0.1.2', '1'))

    def test_a_feed_nobody_has_written_yet_is_a_feed(self):
        feed = release_entry.feed_with(None, self.entry())
        channel = ElementTree.fromstring(feed).find('./channel')
        self.assertTrue(channel.find('title').text)

    def test_release_notes_are_carried_as_marked_up_text(self):
        feed = release_entry.feed_with(
            None, self.entry(notes='<p>Faster &amp; smaller.</p>'))
        item, = items(feed)
        self.assertIn('Faster &amp; smaller.', item.find('description').text)


if __name__ == '__main__':
    unittest.main()
