# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import posixpath

from compiled_file_system import SingleFile
from file_system import FileNotFoundError
from future import Gettable, Future
from schema_util import ProcessSchema
from svn_constants import API_PATH
from third_party.json_schema_compiler.model import Namespace, UnixName


@SingleFile
def _CreateAPIModel(path, data):
  schema = ProcessSchema(path, data)
  if os.path.splitext(path)[1] == '.json':
    schema = schema[0]
  return Namespace(schema, schema['namespace'])


class APIModels(object):
  '''Tracks APIs and their Models.
  '''

  def __init__(self, features_bundle, compiled_fs_factory, file_system):
    self._features_bundle = features_bundle
    self._model_cache = compiled_fs_factory.Create(
        file_system, _CreateAPIModel, APIModels)

  def GetNames(self):
    # API names appear alongside some of their methods/events/etc in the
    # features file. APIs are those which either implicitly or explicitly have
    # no parent feature (e.g. app, app.window, and devtools.inspectedWindow are
    # APIs; runtime.onConnectNative is not).
    api_features = self._features_bundle.GetAPIFeatures().Get()
    return [name for name, feature in api_features.iteritems()
            if ('.' not in name or
                name.rsplit('.', 1)[0] not in api_features or
                feature.get('noparent'))]

  def GetModel(self, api_name):
    # Callers sometimes specify a filename which includes .json or .idl - if
    # so, believe them. They may even include the 'api/' prefix.
    if os.path.splitext(api_name)[1] in ('.json', '.idl'):
      if not api_name.startswith(API_PATH + '/'):
        api_name = posixpath.join(API_PATH, api_name)
      return self._model_cache.GetFromFile(api_name)

    assert not api_name.startswith(API_PATH)

    # API names are given as declarativeContent and app.window but file names
    # will be declarative_content and app_window.
    file_name = UnixName(api_name).replace('.', '_')
    # Devtools APIs are in API_PATH/devtools/ not API_PATH/, and have their
    # "devtools" names removed from the file names.
    basename = posixpath.basename(file_name)
    if basename.startswith('devtools_'):
      file_name = posixpath.join(
          'devtools', file_name.replace(basename, basename[len('devtools_'):]))

    futures = [self._model_cache.GetFromFile('%s/%s.%s' %
                                             (API_PATH, file_name, ext))
               for ext in ('json', 'idl')]
    def resolve():
      for future in futures:
        try:
          return future.Get()
        except FileNotFoundError: pass
      # Propagate the first FileNotFoundError if neither were found.
      futures[0].Get()
    return Future(delegate=Gettable(resolve))
