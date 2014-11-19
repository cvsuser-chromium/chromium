#!/usr/bin/env python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from cStringIO import StringIO
import json
import unittest
from zipfile import ZipFile

from compiled_file_system import CompiledFileSystem
from content_provider import ContentProvider
from file_system import FileNotFoundError
from object_store_creator import ObjectStoreCreator
from test_file_system import TestFileSystem
from third_party.handlebar import Handlebar


_HOST = 'https://developer.chrome.com/'


_REDIRECTS_JSON = json.dumps({
  'oldfile.html': 'storage.html',
  'index.html': 'https://developers.google.com/chrome',
})


# Test file system data which exercises many different mimetypes.
_TEST_DATA = {
  'dir': {
    'a.txt': 'a.txt content',
    'b.txt': 'b.txt content',
    'c': {
      'd.txt': 'd.txt content',
    },
  },
  'dir2': {
    'dir3': {
      'a.txt': 'a.txt content',
      'b.txt': 'b.txt content',
      'c': {
        'd.txt': 'd.txt content',
      },
    },
  },
  'img.png': 'img.png content',
  'read.txt': 'read.txt content',
  'redirects.json': _REDIRECTS_JSON,
  'run.js': 'run.js content',
  'site.css': 'site.css content',
  'storage.html': 'storage.html content',
}


class ContentProviderUnittest(unittest.TestCase):
  def setUp(self):
    self._content_provider = self._CreateContentProvider()

  def _CreateContentProvider(self, supports_zip=False):
    test_file_system = TestFileSystem(_TEST_DATA)
    return ContentProvider(
        'foo',
        CompiledFileSystem.Factory(ObjectStoreCreator.ForTest()),
        test_file_system,
        # TODO(kalman): Test supports_templates=False.
        supports_templates=True,
        supports_zip=supports_zip)

  def _assertContent(self, content, content_type, content_and_type):
    # Assert type so that str is differentiated from unicode.
    self.assertEqual(type(content), type(content_and_type.content))
    self.assertEqual(content, content_and_type.content)
    self.assertEqual(content_type, content_and_type.content_type)

  def testPlainText(self):
    self._assertContent(
        u'a.txt content', 'text/plain',
        self._content_provider.GetContentAndType(_HOST, 'dir/a.txt').Get())
    self._assertContent(
        u'd.txt content', 'text/plain',
        self._content_provider.GetContentAndType(_HOST, 'dir/c/d.txt').Get())
    self._assertContent(
        u'read.txt content', 'text/plain',
        self._content_provider.GetContentAndType(_HOST, 'read.txt').Get())
    self._assertContent(
        unicode(_REDIRECTS_JSON, 'utf-8'), 'application/json',
        self._content_provider.GetContentAndType(_HOST, 'redirects.json').Get())
    self._assertContent(
        u'run.js content', 'application/javascript',
        self._content_provider.GetContentAndType(_HOST, 'run.js').Get())
    self._assertContent(
        u'site.css content', 'text/css',
        self._content_provider.GetContentAndType(_HOST, 'site.css').Get())

  def testTemplate(self):
    content_and_type = self._content_provider.GetContentAndType(
        _HOST, 'storage.html').Get()
    self.assertEqual(Handlebar, type(content_and_type.content))
    content_and_type.content = content_and_type.content.source
    self._assertContent(u'storage.html content', 'text/html', content_and_type)

  def testImage(self):
    self._assertContent(
        'img.png content', 'image/png',
        self._content_provider.GetContentAndType(_HOST, 'img.png').Get())

  def testZipTopLevel(self):
    zip_content_provider = self._CreateContentProvider(supports_zip=True)
    content_and_type = zip_content_provider.GetContentAndType(
        _HOST, 'dir.zip').Get()
    zipfile = ZipFile(StringIO(content_and_type.content))
    content_and_type.content = zipfile.namelist()
    self._assertContent(
        ['dir/a.txt', 'dir/b.txt', 'dir/c/d.txt'], 'application/zip',
        content_and_type)

  def testZip2ndLevel(self):
    zip_content_provider = self._CreateContentProvider(supports_zip=True)
    content_and_type = zip_content_provider.GetContentAndType(
        _HOST, 'dir2/dir3.zip').Get()
    zipfile = ZipFile(StringIO(content_and_type.content))
    content_and_type.content = zipfile.namelist()
    self._assertContent(
        ['dir3/a.txt', 'dir3/b.txt', 'dir3/c/d.txt'], 'application/zip',
        content_and_type)

  def testNotFound(self):
    self.assertRaises(
        FileNotFoundError,
        self._content_provider.GetContentAndType(_HOST, 'oops').Get)


if __name__ == '__main__':
  unittest.main()
