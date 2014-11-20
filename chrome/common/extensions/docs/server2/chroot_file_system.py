# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import posixpath

from docs_server_utils import StringIdentity
from file_system import FileSystem
from future import Gettable, Future


class ChrootFileSystem(FileSystem):
  '''ChrootFileSystem(fs, path) exposes a FileSystem whose root is |path| inside
  |fs|, so ChrootFileSystem(fs, '/hello').Read(['/world']) is equivalent to
  fs.Read(['/hello/world']) with the '/hello' prefix stripped from the result.
  '''

  def __init__(self, file_system, root):
    '''Parameters:
    |file_system| The FileSystem instance to transpose paths of.
    |root|        The path to transpose all Read/Stat calls by.
    '''
    self._file_system = file_system
    self._root = root.strip('/')

  def Read(self, paths, binary=False):
    # Maintain reverse mapping so the result can be mapped to the original
    # paths given (the result from |file_system| will include |root| in the
    # result, which would be wrong).
    prefixed_paths = {}
    def prefix(path):
      prefixed = posixpath.join(self._root, path)
      prefixed_paths[prefixed] = path
      return prefixed
    future_result = self._file_system.Read(
        tuple(prefix(path) for path in paths), binary=binary)
    def resolve():
      return dict((prefixed_paths[path], content)
                  for path, content in future_result.Get().iteritems())
    return Future(delegate=Gettable(resolve))

  def Stat(self, path):
    return self._file_system.Stat(posixpath.join(self._root, path))

  def GetIdentity(self):
    return StringIdentity(
        '%s/%s' % (self._file_system.GetIdentity(), self._root))
