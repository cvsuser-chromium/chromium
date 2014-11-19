# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from api_data_source import APIDataSource
from api_list_data_source import APIListDataSource
from api_models import APIModels
from availability_finder import AvailabilityFinder
from compiled_file_system import CompiledFileSystem
from content_providers import ContentProviders
from empty_dir_file_system import EmptyDirFileSystem
from environment import IsDevServer
from features_bundle import FeaturesBundle
from github_file_system_provider import GithubFileSystemProvider
from host_file_system_provider import HostFileSystemProvider
from host_file_system_iterator import HostFileSystemIterator
from intro_data_source import IntroDataSource
from object_store_creator import ObjectStoreCreator
from path_canonicalizer import PathCanonicalizer
from reference_resolver import ReferenceResolver
from samples_data_source import SamplesDataSource
import svn_constants
from template_renderer import TemplateRenderer
from test_branch_utility import TestBranchUtility
from test_object_store import TestObjectStore

class ServerInstance(object):

  def __init__(self,
               object_store_creator,
               compiled_fs_factory,
               branch_utility,
               host_file_system_provider,
               github_file_system_provider,
               base_path='/'):
    '''
    |object_store_creator|
        The ObjectStoreCreator used to create almost all caches.
    |compiled_fs_factory|
        Factory used to create CompiledFileSystems, a higher-level cache type
        than ObjectStores. This can usually be derived from just
        |object_store_creator| but under special circumstances a different
        implementation needs to be passed in.
    |branch_utility|
        Has knowledge of Chrome branches, channels, and versions.
    |host_file_system_provider|
        Creates FileSystem instances which host the server at alternative
        revisions.
    |github_file_system_provider|
        Creates FileSystem instances backed by GitHub.
    |base_path|
        The path which all HTML is generated relative to. Usually this is /
        but some servlets need to override this.
    '''
    self.object_store_creator = object_store_creator

    self.compiled_fs_factory = compiled_fs_factory

    self.host_file_system_provider = host_file_system_provider
    host_fs_at_trunk = host_file_system_provider.GetTrunk()

    self.github_file_system_provider = github_file_system_provider

    assert base_path.startswith('/') and base_path.endswith('/')
    self.base_path = base_path

    self.host_file_system_iterator = HostFileSystemIterator(
        host_file_system_provider,
        branch_utility)

    self.features_bundle = FeaturesBundle(
        host_fs_at_trunk,
        self.compiled_fs_factory,
        self.object_store_creator)

    self.api_models = APIModels(
        self.features_bundle,
        self.compiled_fs_factory,
        host_fs_at_trunk)

    self.availability_finder = AvailabilityFinder(
        branch_utility,
        compiled_fs_factory,
        self.host_file_system_iterator,
        host_fs_at_trunk,
        object_store_creator)

    self.api_list_data_source_factory = APIListDataSource.Factory(
        self.compiled_fs_factory,
        host_fs_at_trunk,
        self.features_bundle,
        self.object_store_creator)

    self.api_data_source_factory = APIDataSource.Factory(
        self.compiled_fs_factory,
        host_fs_at_trunk,
        svn_constants.API_PATH,
        self.availability_finder,
        branch_utility)

    self.ref_resolver_factory = ReferenceResolver.Factory(
        self.api_data_source_factory,
        self.api_models,
        object_store_creator)

    self.api_data_source_factory.SetReferenceResolverFactory(
        self.ref_resolver_factory)

    # Note: samples are super slow in the dev server because it doesn't support
    # async fetch, so disable them.
    if IsDevServer():
      extension_samples_fs = EmptyDirFileSystem()
      app_samples_fs = EmptyDirFileSystem()
    else:
      extension_samples_fs = host_fs_at_trunk
      app_samples_fs = github_file_system_provider.Create(
          'GoogleChrome', 'chrome-app-samples')
    self.samples_data_source_factory = SamplesDataSource.Factory(
        extension_samples_fs,
        app_samples_fs,
        CompiledFileSystem.Factory(object_store_creator),
        self.ref_resolver_factory,
        svn_constants.EXAMPLES_PATH,
        base_path)

    self.api_data_source_factory.SetSamplesDataSourceFactory(
        self.samples_data_source_factory)

    self.intro_data_source_factory = IntroDataSource.Factory(
        self.compiled_fs_factory,
        host_fs_at_trunk,
        self.ref_resolver_factory,
        [svn_constants.INTRO_PATH, svn_constants.ARTICLE_PATH])

    self.path_canonicalizer = PathCanonicalizer(
        self.compiled_fs_factory,
        host_fs_at_trunk)

    self.content_providers = ContentProviders(
        self.compiled_fs_factory,
        host_fs_at_trunk,
        self.github_file_system_provider)

    # TODO(kalman): Move all the remaining DataSources into DataSourceRegistry,
    # then factor out the DataSource creation into a factory method, so that
    # the entire ServerInstance doesn't need to be passed in here.
    self.template_renderer = TemplateRenderer(self)

    self.strings_json_path = svn_constants.STRINGS_JSON_PATH
    self.manifest_json_path = svn_constants.MANIFEST_JSON_PATH
    self.manifest_features_path = svn_constants.MANIFEST_FEATURES_PATH

  @staticmethod
  def ForTest(file_system, base_path='/'):
    object_store_creator = ObjectStoreCreator.ForTest()
    return ServerInstance(object_store_creator,
                          CompiledFileSystem.Factory(object_store_creator),
                          TestBranchUtility.CreateWithCannedData(),
                          HostFileSystemProvider.ForTest(file_system,
                                                         object_store_creator),
                          GithubFileSystemProvider.ForEmpty(),
                          base_path=base_path)

  @staticmethod
  def ForLocal():
    object_store_creator = ObjectStoreCreator(start_empty=False,
                                              store_type=TestObjectStore)
    host_file_system_provider = HostFileSystemProvider.ForLocal(
        object_store_creator)
    return ServerInstance(
        object_store_creator,
        CompiledFileSystem.Factory(object_store_creator),
        TestBranchUtility.CreateWithCannedData(),
        host_file_system_provider,
        GithubFileSystemProvider.ForEmpty())
