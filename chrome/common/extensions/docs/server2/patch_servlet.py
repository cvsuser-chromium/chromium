# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from fnmatch import fnmatch
import logging
from urlparse import urlparse

from appengine_url_fetcher import AppEngineUrlFetcher
from caching_file_system import CachingFileSystem
from caching_rietveld_patcher import CachingRietveldPatcher
from chained_compiled_file_system import ChainedCompiledFileSystem
from compiled_file_system import  CompiledFileSystem
from environment import IsDevServer
from host_file_system_provider import HostFileSystemProvider
from instance_servlet import InstanceServlet
from render_servlet import RenderServlet
from rietveld_patcher import RietveldPatcher, RietveldPatcherError
from object_store_creator import ObjectStoreCreator
from patched_file_system import PatchedFileSystem
from server_instance import ServerInstance
from servlet import Request, Response, Servlet
import svn_constants
import url_constants

class _PatchServletDelegate(RenderServlet.Delegate):
  def __init__(self, issue, delegate):
    self._issue = issue
    self._delegate = delegate

  def CreateServerInstance(self):
    # start_empty=False because a patch can rely on files that are already in
    # SVN repository but not yet pulled into data store by cron jobs (a typical
    # example is to add documentation for an existing API).
    object_store_creator = ObjectStoreCreator(start_empty=False)

    unpatched_file_system = self._delegate.CreateHostFileSystemProvider(
        object_store_creator).GetTrunk()

    rietveld_patcher = CachingRietveldPatcher(
        RietveldPatcher(svn_constants.EXTENSIONS_PATH,
                        self._issue,
                        AppEngineUrlFetcher(url_constants.CODEREVIEW_SERVER)),
        object_store_creator)

    patched_file_system = PatchedFileSystem(unpatched_file_system,
                                            rietveld_patcher)

    patched_host_file_system_provider = (
        self._delegate.CreateHostFileSystemProvider(
            object_store_creator,
            # The patched file system needs to be online otherwise it'd be
            # impossible to add files in the patches.
            offline=False,
            # The trunk file system for this creator should be the patched one.
            default_trunk_instance=patched_file_system))

    combined_compiled_fs_factory = ChainedCompiledFileSystem.Factory(
        [unpatched_file_system], object_store_creator)

    branch_utility = self._delegate.CreateBranchUtility(object_store_creator)

    server_instance = ServerInstance(
        object_store_creator,
        combined_compiled_fs_factory,
        branch_utility,
        patched_host_file_system_provider,
        self._delegate.CreateGithubFileSystemProvider(object_store_creator),
        base_path='/_patch/%s/' % self._issue)

    # HACK: if content_providers.json changes in this patch then the cron needs
    # to be re-run to pull in the new configuration.
    _, _, modified = rietveld_patcher.GetPatchedFiles()
    if svn_constants.CONTENT_PROVIDERS_PATH in modified:
      server_instance.content_providers.Cron().Get()

    return server_instance

class PatchServlet(Servlet):
  '''Servlet which renders patched docs.
  '''
  def __init__(self, request, delegate=None):
    self._request = request
    self._delegate = delegate or InstanceServlet.Delegate()

  def Get(self):
    if (not IsDevServer() and
        not fnmatch(urlparse(self._request.host).netloc, '*.appspot.com')):
      # Only allow patches on appspot URLs; it doesn't matter if appspot.com is
      # XSS'ed, but it matters for chrome.com.
      redirect_host = 'https://chrome-apps-doc.appspot.com'
      logging.info('Redirecting from XSS-able host %s to %s' % (
          self._request.host, redirect_host))
      return Response.Redirect(
          '%s/_patch/%s' % (redirect_host, self._request.path))

    path_with_issue = self._request.path.lstrip('/')
    if '/' in path_with_issue:
      issue, path_without_issue = path_with_issue.split('/', 1)
    else:
      return Response.NotFound('Malformed URL. It should look like ' +
          'https://developer.chrome.com/_patch/12345/extensions/...')

    try:
      response = RenderServlet(
          Request(path_without_issue,
                  self._request.host,
                  self._request.headers),
          _PatchServletDelegate(issue, self._delegate)).Get()
      # Disable cache for patched content.
      response.headers.pop('cache-control', None)
    except RietveldPatcherError as e:
      response = Response.NotFound(e.message, {'Content-Type': 'text/plain'})

    redirect_url, permanent = response.GetRedirect()
    if redirect_url is not None:
      response = Response.Redirect('/_patch/%s%s' % (issue, redirect_url),
                                   permanent)
    return response
