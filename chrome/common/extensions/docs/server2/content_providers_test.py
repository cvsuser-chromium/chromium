#!/usr/bin/env python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
import unittest

from compiled_file_system import CompiledFileSystem
from content_providers import ContentProviders
from object_store_creator import ObjectStoreCreator
from test_file_system import TestFileSystem
from test_util import DisableLogging


_HOST = 'https://developer.chrome.com'


_CONTENT_PROVIDERS = {
  'apples': {
    'chromium': {
      'dir': 'apples'
    },
    'serveFrom': 'apples-dir',
  },
  'bananas': {
    'serveFrom': '',
    'chromium': {
      'dir': ''
    },
  },
  'github-provider': {
    'serveFrom': 'gh',
    'github': {
      'owner': 'GoogleChrome',
      'repo': 'hello-world',
    },
  },
  'github-provider-with-dir': {
    'serveFrom': 'gh2',
    'github': {
      'dir': 'tomatoes/are/a',
      'owner': 'SomeOwner',
      'repo': 'some-repo',
    },
  },
  'tomatoes': {
    'serveFrom': 'tomatoes-dir/are/a',
    'chromium': {
      'dir': 'tomatoes/are/a'
    },
  },
}


_FILE_SYSTEM_DATA = {
  'docs': {
    'templates': {
      'json': {
        'content_providers.json': json.dumps(_CONTENT_PROVIDERS),
      },
    },
  },
  'apples': {
    'gala.txt': 'gala apples',
    'green': {
      'granny smith.txt': 'granny smith apples',
    },
  },
  'tomatoes': {
    'are': {
      'a': {
        'vegetable.txt': 'no they aren\'t',
        'fruit': {
          'cherry.txt': 'cherry tomatoes',
        },
      },
    },
  },
}


class _MockGithubFileSystemProvider(object):
  '''A GithubFileSystemProvider imitation which records every call to Create
  and returns them from GetAndReset.
  '''

  def __init__(self, file_system):
    self._file_system = file_system
    self._calls = []

  def Create(self, owner, repo):
    self._calls.append((owner, repo))
    return self._file_system

  def GetAndReset(self):
    calls = self._calls
    self._calls = []
    return calls


class ContentProvidersTest(unittest.TestCase):
  def setUp(self):
    test_file_system = TestFileSystem(_FILE_SYSTEM_DATA)
    self._github_fs_provider = _MockGithubFileSystemProvider(test_file_system)
    self._content_providers = ContentProviders(
        CompiledFileSystem.Factory(ObjectStoreCreator.ForTest()),
        test_file_system,
        self._github_fs_provider)

  def testSimpleRootPath(self):
    provider = self._content_providers.GetByName('apples')
    self.assertEqual(
        'gala apples',
        provider.GetContentAndType(_HOST, 'gala.txt').Get().content)
    self.assertEqual(
        'granny smith apples',
        provider.GetContentAndType(_HOST, 'green/granny smith.txt').Get()
            .content)

  def testComplexRootPath(self):
    provider = self._content_providers.GetByName('tomatoes')
    self.assertEqual(
        'no they aren\'t',
        provider.GetContentAndType(_HOST, 'vegetable.txt').Get().content)
    self.assertEqual(
        'cherry tomatoes',
        provider.GetContentAndType(_HOST, 'fruit/cherry.txt').Get().content)

  def testEmptyRootPath(self):
    provider = self._content_providers.GetByName('bananas')
    self.assertEqual(
        'gala apples',
        provider.GetContentAndType(_HOST, 'apples/gala.txt').Get().content)

  def testSimpleServlet(self):
    provider, path = self._content_providers.GetByServeFrom('apples-dir')
    self.assertEqual('apples', provider.name)
    self.assertEqual('', path)
    provider, path = self._content_providers.GetByServeFrom(
        'apples-dir/are/forever')
    self.assertEqual('apples', provider.name)
    self.assertEqual('are/forever', path)

  def testComplexServlet(self):
    provider, path = self._content_providers.GetByServeFrom(
        'tomatoes-dir/are/a')
    self.assertEqual('tomatoes', provider.name)
    self.assertEqual('', path)
    provider, path = self._content_providers.GetByServeFrom(
        'tomatoes-dir/are/a/fruit/they/are')
    self.assertEqual('tomatoes', provider.name)
    self.assertEqual('fruit/they/are', path)

  def testEmptyStringServlet(self):
    provider, path = self._content_providers.GetByServeFrom('tomatoes-dir/are')
    self.assertEqual('bananas', provider.name)
    self.assertEqual('tomatoes-dir/are', path)
    provider, path = self._content_providers.GetByServeFrom('')
    self.assertEqual('bananas', provider.name)
    self.assertEqual('', path)

  @DisableLogging('error')
  def testProviderNotFound(self):
    self.assertEqual(None, self._content_providers.GetByName('cabbages'))

  def testGithubContentProvider(self):
    provider, path = self._content_providers.GetByServeFrom(
        'gh/apples/green/granny smith.txt')
    self.assertEqual('github-provider', provider.name)
    self.assertEqual('apples/green/granny smith.txt', path)
    self.assertEqual([('GoogleChrome', 'hello-world')],
                     self._github_fs_provider.GetAndReset())
    self.assertEqual(
        'granny smith apples',
        provider.GetContentAndType(_HOST, path).Get().content)

  def testGithubContentProviderWithDir(self):
    provider, path = self._content_providers.GetByServeFrom(
        'gh2/fruit/cherry.txt')
    self.assertEqual('github-provider-with-dir', provider.name)
    self.assertEqual('fruit/cherry.txt', path)
    self.assertEqual([('SomeOwner', 'some-repo')],
                     self._github_fs_provider.GetAndReset())
    self.assertEqual(
        'cherry tomatoes',
        provider.GetContentAndType(_HOST, path).Get().content)

if __name__ == '__main__':
  unittest.main()
