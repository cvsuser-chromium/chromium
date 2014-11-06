#!/usr/bin/env python
# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from collections import Mapping
import json
from operator import itemgetter
import unittest

from compiled_file_system import CompiledFileSystem
from object_store_creator import ObjectStoreCreator
from permissions_data_source import PermissionsDataSource
from server_instance import ServerInstance
from third_party.handlebar import Handlebar
from test_file_system import TestFileSystem


_PERMISSION_FEATURES = {
  # This will appear for extensions with a description as defined in the
  # permissions.json file.
  'activeTab': {
    'name': 'activeTab',
    'platforms': ['extensions'],
  },
  # This will appear for apps and extensions with an auto-generated description
  # since the entry appears in _api_features.json.
  'alarms': {
    'name': 'alarms',
    'platforms': ['apps', 'extensions'],
  },
  # This won't appear for anything since there's no entry in permissions.json
  # and it's not an API.
  'audioCapture': {
    'name': 'audioCapture',
    'platforms': ['apps'],
  },
  # This won't appear for anything because it's private.
  'commandLinePrivate': {
    'name': 'commandLinePrivate',
    'platforms': ['apps', 'extensions']
  },
  # This will only appear for apps with an auto-generated description because
  # it's an API.
  'cookies': {
    'name': 'cookies',
    'platforms': ['apps']
  },
}


_PERMISSIONS_JSON = {
  # This will appear for both apps and extensions with a custom description,
  # anchor, etc.
  'host-permissions': {
    'name': 'match pattern',
    'anchor': 'custom-anchor',
    'partial': 'permissions/host_permissions.html',
    'platforms': ['apps', 'extensions'],
    'literal_name': True
  },
  # A custom 'partial' here overrides the default partial.
  'activeTab': {
    'partial': 'permissions/active_tab.html'
  },
}


_PERMISSIONS_PARTIALS = {
  'active_tab.html': 'active tab',
  'host_permissions.html': 'host permissions',
  'generic_description.html': 'generic description',
}


_API_FEATURES = {
  'alarms': {
    'dependencies': ['permission:alarms']
  },
  'cookies': {
    'dependencies': ['permission:cookies']
  },
}


class PermissionsDataSourceTest(unittest.TestCase):
  def testCreatePermissionsDataSource(self):
    expected_extensions = [
      {
        'anchor': 'custom-anchor',
        'description': 'host permissions',
        'literal_name': True,
        'name': 'match pattern',
        'platforms': ['apps', 'extensions']
      },
      {
        'anchor': 'activeTab',
        'description': 'active tab',
        'name': 'activeTab',
        'platforms': ['extensions'],
      },
      {
        'anchor': 'alarms',
        'description': 'generic description',
        'name': 'alarms',
        'platforms': ['apps', 'extensions'],
      },
    ]

    expected_apps = [
      {
        'anchor': 'custom-anchor',
        'description': 'host permissions',
        'literal_name': True,
        'name': 'match pattern',
        'platforms': ['apps', 'extensions'],
      },
      {
        'anchor': 'alarms',
        'description': 'generic description',
        'name': 'alarms',
        'platforms': ['apps', 'extensions'],
      },
      {
        'anchor': 'cookies',
        'description': 'generic description',
        'name': 'cookies',
        'platforms': ['apps'],
      },
    ]

    test_file_system = TestFileSystem({
      'api': {
        '_api_features.json': json.dumps(_API_FEATURES),
        '_manifest_features.json': '{}',
        '_permission_features.json': json.dumps(_PERMISSION_FEATURES),
      },
      'docs': {
        'templates': {
          'json': {
            'manifest.json': '{}',
            'permissions.json': json.dumps(_PERMISSIONS_JSON),
          },
          'private': {
            'permissions': _PERMISSIONS_PARTIALS
          },
        }
      }
    })

    permissions_data_source = PermissionsDataSource(
        ServerInstance.ForTest(test_file_system), None)

    actual_extensions = permissions_data_source.get('declare_extensions')
    actual_apps = permissions_data_source.get('declare_apps')

    # Normalise all test data.
    #   - Sort keys. Since the tests don't use OrderedDicts we can't make
    #     assertions about the order, which is unfortunate. Oh well.
    #   - Render all of the Handlerbar instances so that we can use ==.
    #     Handlebars don't implement __eq__, but they probably should.
    for lst in (actual_apps, actual_extensions,
                expected_apps, expected_extensions):
      lst.sort(key=itemgetter('name'))
      for mapping in lst:
        for key, value in mapping.iteritems():
          if isinstance(value, Handlebar):
            mapping[key] = value.Render().text

    self.assertEqual(expected_extensions, actual_extensions)
    self.assertEqual(expected_apps, actual_apps)

if __name__ == '__main__':
  unittest.main()
