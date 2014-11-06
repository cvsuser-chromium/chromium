#!/usr/bin/env python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import json
import unittest

from api_models import APIModels
from compiled_file_system import CompiledFileSystem
from features_bundle import FeaturesBundle
from file_system import FileNotFoundError
from mock_file_system import MockFileSystem
from object_store_creator import ObjectStoreCreator
from test_file_system import TestFileSystem
from test_util import ReadFile


_TEST_DATA = {
  'api': {
    'devtools': {
      'inspected_window.json': ReadFile(os.path.join(
          'api', 'devtools', 'inspected_window.json')),
    },
    '_api_features.json': json.dumps({
      'alarms': {},
      'app': {},
      'app.runtime': {'noparent': True},
      'app.runtime.experimental': {},
      'app.runtime.experimental.foo': {},
      'declarativeWebRequest': {},
      'devtools.inspectedWindow': {},
      'experimental.accessibility': {},
      'storage': {},
    }),
    '_manifest_features.json': '{}',
    '_permission_features.json': '{}',
    'alarms.idl': ReadFile(os.path.join('api', 'alarms.idl')),
    'declarative_web_request.json': ReadFile(os.path.join(
        'api', 'declarative_web_request.json')),
    'experimental_accessibility.json': ReadFile(os.path.join(
        'api', 'experimental_accessibility.json')),
    'page_action.json': ReadFile(os.path.join('api', 'page_action.json')),
  },
  'docs': {
    'templates': {
      'json': {
        'manifest.json': '{}',
        'permissions.json': '{}',
      }
    }
  },
}


class APIModelsTest(unittest.TestCase):
  def setUp(self):
    object_store_creator = ObjectStoreCreator.ForTest()
    compiled_fs_factory = CompiledFileSystem.Factory(object_store_creator)
    self._mock_file_system = MockFileSystem(TestFileSystem(_TEST_DATA))
    features_bundle = FeaturesBundle(
        self._mock_file_system, compiled_fs_factory, object_store_creator)
    self._api_models = APIModels(
        features_bundle, compiled_fs_factory, self._mock_file_system)

  def testGetNames(self):
    # Both 'app' and 'app.runtime' appear here because 'app.runtime' has
    # noparent:true, but 'app.runtime.experimental' etc doesn't so it's a
    # sub-feature of 'app.runtime' not a separate API.
    # 'devtools.inspectedWindow' is an API because there is no 'devtools'.
    self.assertEqual(
        ['alarms', 'app', 'app.runtime', 'declarativeWebRequest',
         'devtools.inspectedWindow', 'experimental.accessibility', 'storage'],
        sorted(self._api_models.GetNames()))

  def testGetModel(self):
    def get_model_name(api_name):
      return self._api_models.GetModel(api_name).Get().name
    self.assertEqual('devtools.inspectedWindow',
                     get_model_name('devtools.inspectedWindow'))
    self.assertEqual('devtools.inspectedWindow',
                     get_model_name('devtools/inspected_window.json'))
    self.assertEqual('devtools.inspectedWindow',
                     get_model_name('api/devtools/inspected_window.json'))
    self.assertEqual('alarms', get_model_name('alarms'))
    self.assertEqual('alarms', get_model_name('alarms.idl'))
    self.assertEqual('alarms', get_model_name('api/alarms.idl'))
    self.assertEqual('declarativeWebRequest',
                     get_model_name('declarativeWebRequest'))
    self.assertEqual('declarativeWebRequest',
                     get_model_name('declarative_web_request.json'))
    self.assertEqual('declarativeWebRequest',
                     get_model_name('api/declarative_web_request.json'))
    self.assertEqual('experimental.accessibility',
                     get_model_name('experimental.accessibility'))
    self.assertEqual('experimental.accessibility',
                     get_model_name('experimental_accessibility.json'))
    self.assertEqual('experimental.accessibility',
                     get_model_name('api/experimental_accessibility.json'))
    self.assertEqual('pageAction', get_model_name('pageAction'))
    self.assertEqual('pageAction', get_model_name('page_action.json'))
    self.assertEqual('pageAction', get_model_name('api/page_action.json'))

  def testGetNonexistentModel(self):
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('notfound').Get)
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('notfound.json').Get)
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('api/notfound.json').Get)
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('api/alarms.json').Get)
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('storage').Get)
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('api/storage.json').Get)
    self.assertRaises(FileNotFoundError,
                      self._api_models.GetModel('api/storage.idl').Get)

  def testSingleFile(self):
    # 2 stats (1 for JSON and 1 for IDL), 1 read (for IDL file which existed).
    future = self._api_models.GetModel('alarms')
    self.assertTrue(*self._mock_file_system.CheckAndReset(
        read_count=1, stat_count=2))

    # 1 read-resolve (for the IDL file).
    #
    # The important part here and above is that it's only doing a single read;
    # any more would break the contract that only a single file is accessed -
    # see the SingleFile annotation in api_models._CreateAPIModel.
    future.Get()
    self.assertTrue(*self._mock_file_system.CheckAndReset(
        read_resolve_count=1))

    # 2 stats (1 for JSON and 1 for IDL), no reads (still cached).
    future = self._api_models.GetModel('alarms')
    self.assertTrue(*self._mock_file_system.CheckAndReset(stat_count=2))
    future.Get()
    self.assertTrue(*self._mock_file_system.CheckAndReset())


if __name__ == '__main__':
  unittest.main()
