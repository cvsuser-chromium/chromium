# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import hashlib
import json
import logging
import posixpath
import re
import traceback

from compiled_file_system import CompiledFileSystem
import third_party.json_schema_compiler.json_comment_eater as json_comment_eater
import third_party.json_schema_compiler.model as model
import url_constants

DEFAULT_ICON_PATH = 'images/sample-default-icon.png'

class SamplesDataSource(object):
  '''Constructs a list of samples and their respective files and api calls.
  '''
  class Factory(object):
    '''A factory to create SamplesDataSource instances bound to individual
    Requests.
    '''
    def __init__(self,
                 host_file_system,
                 app_samples_file_system,
                 compiled_fs_factory,
                 ref_resolver_factory,
                 extension_samples_path,
                 base_path):
      self._host_file_system = host_file_system
      self._app_samples_file_system = app_samples_file_system
      self._ref_resolver = ref_resolver_factory.Create()
      self._extension_samples_path = extension_samples_path
      self._base_path = base_path
      self._extensions_cache = compiled_fs_factory.Create(
          host_file_system,
          self._MakeSamplesList,
          SamplesDataSource,
          category='extensions')
      self._apps_cache = compiled_fs_factory.Create(
          app_samples_file_system,
          lambda *args: self._MakeSamplesList(*args, is_apps=True),
          SamplesDataSource,
          category='apps')

    def Create(self, request):
      '''Returns a new SamplesDataSource bound to |request|.
      '''
      return SamplesDataSource(self._extensions_cache,
                               self._apps_cache,
                               self._extension_samples_path,
                               self._base_path,
                               request)

    def _GetAPIItems(self, js_file):
      chrome_pattern = r'chrome[\w.]+'
      # Add API calls that appear normally, like "chrome.runtime.connect".
      calls = set(re.findall(chrome_pattern, js_file))
      # Add API calls that have been assigned into variables, like
      # "var storageArea = chrome.storage.sync; storageArea.get", which should
      # be expanded like "chrome.storage.sync.get".
      for match in re.finditer(r'var\s+(\w+)\s*=\s*(%s);' % chrome_pattern,
                               js_file):
        var_name, api_prefix = match.groups()
        for var_match in re.finditer(r'\b%s\.([\w.]+)\b' % re.escape(var_name),
                                     js_file):
          api_suffix, = var_match.groups()
          calls.add('%s.%s' % (api_prefix, api_suffix))
      return calls

    def _GetDataFromManifest(self, path, file_system):
      manifest = file_system.ReadSingle(path + '/manifest.json').Get()
      try:
        manifest_json = json.loads(json_comment_eater.Nom(manifest))
      except ValueError as e:
        logging.error('Error parsing manifest.json for %s: %s' % (path, e))
        return None
      l10n_data = {
        'name': manifest_json.get('name', ''),
        'description': manifest_json.get('description', None),
        'icon': manifest_json.get('icons', {}).get('128', None),
        'default_locale': manifest_json.get('default_locale', None),
        'locales': {}
      }
      if not l10n_data['default_locale']:
        return l10n_data
      locales_path = path + '/_locales/'
      locales_dir = file_system.ReadSingle(locales_path).Get()
      if locales_dir:
        locales_files = file_system.Read(
            [locales_path + f + 'messages.json' for f in locales_dir]).Get()
        try:
          locales_json = [(locale_path, json.loads(contents))
                          for locale_path, contents in
                          locales_files.iteritems()]
        except ValueError as e:
          logging.error('Error parsing locales files for %s: %s' % (path, e))
        else:
          for path, json_ in locales_json:
            l10n_data['locales'][path[len(locales_path):].split('/')[0]] = json_
      return l10n_data

    def _MakeSamplesList(self, base_dir, files, is_apps=False):
      # HACK(kalman): The code here (for legacy reasons) assumes that |files| is
      # prefixed by |base_dir|, so make it true.
      files = ['%s%s' % (base_dir, f) for f in files]
      file_system = (self._app_samples_file_system if is_apps else
                     self._host_file_system)
      samples_list = []
      for filename in sorted(files):
        if filename.rsplit('/')[-1] != 'manifest.json':
          continue

        # This is a little hacky, but it makes a sample page.
        sample_path = filename.rsplit('/', 1)[-2]
        sample_files = [path for path in files
                        if path.startswith(sample_path + '/')]
        js_files = [path for path in sample_files if path.endswith('.js')]
        js_contents = file_system.Read(js_files).Get()
        api_items = set()
        for js in js_contents.values():
          api_items.update(self._GetAPIItems(js))

        api_calls = []
        for item in sorted(api_items):
          if len(item.split('.')) < 3:
            continue
          if item.endswith('.removeListener') or item.endswith('.hasListener'):
            continue
          if item.endswith('.addListener'):
            item = item[:-len('.addListener')]
          if item.startswith('chrome.'):
            item = item[len('chrome.'):]
          ref_data = self._ref_resolver.GetLink(item)
          # TODO(kalman): What about references like chrome.storage.sync.get?
          # That should link to either chrome.storage.sync or
          # chrome.storage.StorageArea.get (or probably both).
          # TODO(kalman): Filter out API-only references? This can happen when
          # the API namespace is assigned to a variable, but it's very hard to
          # to disambiguate.
          if ref_data is None:
            continue
          api_calls.append({
            'name': ref_data['text'],
            'link': ref_data['href']
          })

        sample_base_path = sample_path.split('/', 1)[1]
        if is_apps:
          url = url_constants.GITHUB_BASE + '/' + sample_base_path
          icon_base = url_constants.RAW_GITHUB_BASE + '/' + sample_base_path
          download_url = url
        else:
          url = sample_base_path
          icon_base = sample_base_path
          download_url = sample_base_path + '.zip'

        manifest_data = self._GetDataFromManifest(sample_path, file_system)
        if manifest_data['icon'] is None:
          icon_path = posixpath.join(
              self._base_path, 'static', DEFAULT_ICON_PATH)
        else:
          icon_path = '%s/%s' % (icon_base, manifest_data['icon'])
        manifest_data.update({
          'icon': icon_path,
          'download_url': download_url,
          'url': url,
          'files': [f.replace(sample_path + '/', '') for f in sample_files],
          'api_calls': api_calls
        })
        samples_list.append(manifest_data)

      return samples_list

  def __init__(self,
               extensions_cache,
               apps_cache,
               extension_samples_path,
               base_path,
               request):
    self._extensions_cache = extensions_cache
    self._apps_cache = apps_cache
    self._extension_samples_path = extension_samples_path
    self._base_path = base_path
    self._request = request

  def _GetSampleId(self, sample_name):
    return sample_name.lower().replace(' ', '-')

  def _GetAcceptedLanguages(self):
    accept_language = self._request.headers.get('Accept-Language', None)
    if accept_language is None:
      return []
    return [lang_with_q.split(';')[0].strip()
            for lang_with_q in accept_language.split(',')]

  def FilterSamples(self, key, api_name):
    '''Fetches and filters the list of samples specified by |key|, returning
    only the samples that use the API |api_name|. |key| is either 'apps' or
    'extensions'.
    '''
    return [sample for sample in self.get(key) if any(
        call['name'].startswith(api_name + '.')
        for call in sample['api_calls'])]

  def _CreateSamplesDict(self, key):
    if key == 'apps':
      samples_list = self._apps_cache.GetFromFileListing('/').Get()
    else:
      samples_list = self._extensions_cache.GetFromFileListing(
          self._extension_samples_path + '/').Get()
    return_list = []
    for dict_ in samples_list:
      name = dict_['name']
      description = dict_['description']
      if description is None:
        description = ''
      if name.startswith('__MSG_') or description.startswith('__MSG_'):
        try:
          # Copy the sample dict so we don't change the dict in the cache.
          sample_data = dict_.copy()
          name_key = name[len('__MSG_'):-len('__')]
          description_key = description[len('__MSG_'):-len('__')]
          locale = sample_data['default_locale']
          for lang in self._GetAcceptedLanguages():
            if lang in sample_data['locales']:
              locale = lang
              break
          locale_data = sample_data['locales'][locale]
          sample_data['name'] = locale_data[name_key]['message']
          sample_data['description'] = locale_data[description_key]['message']
          sample_data['id'] = self._GetSampleId(sample_data['name'])
        except Exception as e:
          logging.error(traceback.format_exc())
          # Revert the sample to the original dict.
          sample_data = dict_
        return_list.append(sample_data)
      else:
        dict_['id'] = self._GetSampleId(name)
        return_list.append(dict_)
    return return_list

  def get(self, key):
    return {
      'apps': lambda: self._CreateSamplesDict('apps'),
      'extensions': lambda: self._CreateSamplesDict('extensions')
    }.get(key, lambda: {})()
