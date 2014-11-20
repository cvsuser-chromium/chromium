# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from branch_utility import BranchUtility
from compiled_file_system import CompiledFileSystem
from empty_dir_file_system import EmptyDirFileSystem
from environment import IsDevServer
from github_file_system_provider import GithubFileSystemProvider
from host_file_system_provider import HostFileSystemProvider
from third_party.json_schema_compiler.memoize import memoize
from render_servlet import RenderServlet
from object_store_creator import ObjectStoreCreator
from server_instance import ServerInstance

class InstanceServletRenderServletDelegate(RenderServlet.Delegate):
  '''AppEngine instances should never need to call out to SVN. That should only
  ever be done by the cronjobs, which then write the result into DataStore,
  which is as far as instances look. To enable this, crons can pass a custom
  (presumably online) ServerInstance into Get().

  Why? SVN is slow and a bit flaky. Cronjobs failing is annoying but temporary.
  Instances failing affects users, and is really bad.

  Anyway - to enforce this, we actually don't give instances access to SVN.  If
  anything is missing from datastore, it'll be a 404. If the cronjobs don't
  manage to catch everything - uhoh. On the other hand, we'll figure it out
  pretty soon, and it also means that legitimate 404s are caught before a round
  trip to SVN.
  '''
  def __init__(self, delegate):
    self._delegate = delegate

  @memoize
  def CreateServerInstance(self):
    object_store_creator = ObjectStoreCreator(start_empty=False)
    branch_utility = self._delegate.CreateBranchUtility(object_store_creator)
    # In production have offline=True so that we can catch cron errors.  In
    # development it's annoying to have to run the cron job, so offline=False.
    host_file_system_provider = self._delegate.CreateHostFileSystemProvider(
        object_store_creator,
        offline=not IsDevServer())
    github_file_system_provider = self._delegate.CreateGithubFileSystemProvider(
        object_store_creator)
    return ServerInstance(object_store_creator,
                          CompiledFileSystem.Factory(object_store_creator),
                          branch_utility,
                          host_file_system_provider,
                          github_file_system_provider)

class InstanceServlet(object):
  '''Servlet for running on normal AppEngine instances.
  Create this via GetConstructor() so that cache state can be shared amongst
  them via the memoizing Delegate.
  '''
  class Delegate(object):
    '''Allow runtime dependencies to be overriden for testing.
    '''
    def CreateBranchUtility(self, object_store_creator):
      return BranchUtility.Create(object_store_creator)

    def CreateHostFileSystemProvider(self, object_store_creator, **optargs):
      return HostFileSystemProvider(object_store_creator, **optargs)

    def CreateGithubFileSystemProvider(self, object_store_creator):
      return GithubFileSystemProvider(object_store_creator)

  @staticmethod
  def GetConstructor(delegate_for_test=None):
    render_servlet_delegate = InstanceServletRenderServletDelegate(
        delegate_for_test or InstanceServlet.Delegate())
    return lambda request: RenderServlet(request, render_servlet_delegate)

  # NOTE: if this were a real Servlet it would implement a Get() method, but
  # GetConstructor returns an appropriate lambda function (Request -> Servlet).
