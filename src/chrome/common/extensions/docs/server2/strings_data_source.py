# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from data_source import DataSource

class StringsDataSource(DataSource):
  '''Provides templates with access to a key to string mapping defined in a
  JSON configuration file.
  '''
  def __init__(self, server_instance, _):
    self._cache = server_instance.compiled_fs_factory.ForJson(
        server_instance.host_file_system_provider.GetTrunk())
    self._strings_json_path = server_instance.strings_json_path

  def _GetStringsData(self):
    return self._cache.GetFromFile(self._strings_json_path)

  def Cron(self):
    return self._GetStringsData()

  def get(self, key):
    return self._GetStringsData().Get().get(key)
