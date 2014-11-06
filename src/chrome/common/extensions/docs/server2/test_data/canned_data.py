# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json
from third_party.json_schema_compiler.json_parse import OrderedDict


CANNED_CHANNELS = OrderedDict([
  ('trunk', 'trunk'),
  ('dev', 28),
  ('beta', 27),
  ('stable', 26)
])


CANNED_BRANCHES = OrderedDict([
  ('trunk', 'trunk'),
  (28, '1500'),
  (27, '1453'),
  (26, '1410'),
  (25, '1364'),
  (24, '1312'),
  (23, '1271'),
  (22, '1229'),
  (21, '1180'),
  (20, '1132'),
  (19, '1084'),
  (18, '1025'),
  (17, '963'),
  (16, '912'),
  (15, '874'),
  (14, '835'),
  (13, '782'),
  (12, '742'),
  (11, '696'),
  (10, '648'),
  ( 9, '597'),
  ( 8, '552'),
  ( 7, '544'),
  ( 6, '495'),
  ( 5, '396'),
])


CANNED_TEST_FILE_SYSTEM_DATA = {
  'api': {
    '_api_features.json': json.dumps({
      'ref_test': { 'dependencies': ['permission:ref_test'] },
      'tester': { 'dependencies': ['permission:tester', 'manifest:tester'] }
    }),
    '_manifest_features.json': json.dumps({
      'manifest': 'features'
    }),
    '_permission_features.json': json.dumps({
      'permission': 'features'
    })
  },
  'docs': {
    'templates': {
      'intros': {
        'test.html': '<h1>hi</h1>you<h2>first</h2><h3>inner</h3><h2>second</h2>'
      },
      'json': {
        'api_availabilities.json': json.dumps({
          'trunk_api': {
            'channel': 'trunk'
          },
          'dev_api': {
            'channel': 'dev'
          },
          'beta_api': {
            'channel': 'beta'
          },
          'stable_api': {
            'channel': 'stable',
            'version': 20
          }
        }),
        'intro_tables.json': json.dumps({
          'tester': {
            'Permissions': [
              {
                'class': 'override',
                'text': '"tester"'
              },
              {
                'text': 'is an API for testing things.'
              }
            ],
            'Learn More': [
              {
                'link': 'https://tester.test.com/welcome.html',
                'text': 'Welcome!'
              }
            ]
          }
        })
      },
      'private': {
        'intro_tables': {
          'trunk_message.html': 'available on trunk'
        }
      }
    }
  }
}


CANNED_API_FILE_SYSTEM_DATA = {
  'trunk': {
    'api': {
      '_api_features.json': json.dumps({
        'contextMenus': {
          'channel': 'stable'
        },
        'events': {
          'channel': 'stable'
        },
        'extension': {
          'channel': 'stable'
        },
        'systemInfo.cpu': {
          'channel': 'stable'
        },
        'systemInfo.stuff': {
          'channel': 'dev'
        }
      }),
      '_manifest_features.json': json.dumps({
        'history': {
          'channel': 'beta'
        },
        'notifications': {
          'channel': 'beta'
        },
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'storage': {
          'channel': 'beta'
        },
        'sync': {
          'channel': 'trunk'
        },
        'web_request': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'stable'
        },
        'bluetooth': {
          'channel': 'dev'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'cookies': {
          'channel': 'dev'
        },
        'declarativeContent': {
          'channel': 'trunk'
        },
        'declarativeWebRequest': [
          { 'channel': 'beta' },
          # whitelist
          { 'channel': 'stable'}
        ],
        'falseBetaAPI': {
          'channel': 'beta'
        },
        'systemInfo.display': {
          'channel': 'stable'
        },
        'trunkAPI': {
          'channel': 'trunk'
        }
      }),
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    },
    'docs': {
      'templates': {
        'json': {
          'api_availabilities.json': json.dumps({
            'jsonAPI1': {
              'channel': 'stable',
              'version': 10
            },
            'jsonAPI2': {
              'channel': 'trunk'
            },
            'jsonAPI3': {
              'channel': 'dev'
            }
          }),
          'intro_tables.json': json.dumps({
            'test': [
              {
                'Permissions': 'probably none'
              }
            ]
          })
        }
      }
    }
  },
  '1500': {
    'api': {
      '_api_features.json': json.dumps({
        'events': {
          'channel': 'trunk'
        },
        'extension': {
          'channel': 'stable'
        },
        'systemInfo.cpu': {
          'channel': 'stable'
        },
        'systemInfo.stuff': {
          'channel': 'dev'
        }
      }),
      '_manifest_features.json': json.dumps({
        'contextMenus': {
          'channel': 'trunk'
        },
        'notifications': {
          'channel': 'beta'
        },
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'storage': {
          'channel': 'dev'
        },
        'sync': {
          'channel': 'trunk'
        },
        'system_info_display': {
          'channel': 'stable'
        },
        'web_request': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'stable'
        },
        'bluetooth': {
          'channel': 'dev'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'cookies': {
          'channel': 'dev'
        },
        'declarativeContent': {
          'channel': 'trunk'
        },
        'declarativeWebRequest': [
          { 'channel': 'beta' },
          # whitelist
          { 'channel': 'stable'}
        ],
        'downloads': {
          'channel': 'beta'
        }
      }),
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    }
  },
  '1453': {
    'api': {
      '_api_features.json': json.dumps({
        'events': {
          'channel': 'dev'
        },
        'extension': {
          'channel': 'stable'
        },
        'systemInfo.cpu': {
          'channel': 'stable'
        },
        'systemInfo.stuff': {
          'channel': 'dev'
        }
      }),
      '_manifest_features.json': json.dumps({
        'notifications': {
          'channel': 'dev'
        },
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'storage': {
          'channel': 'dev'
        },
        'system_info_display': {
          'channel': 'stable'
        },
        'web_request': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'stable'
        },
        'bluetooth': {
          'channel': 'dev'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'context_menus': {
          'channel': 'trunk'
        },
        'declarativeContent': {
          'channel': 'trunk'
        },
        'declarativeWebRequest': [
          { 'channel': 'beta' },
          # whitelist
          { 'channel': 'stable'}
        ],
        'downloads': {
          'channel': 'dev'
        }
      }),
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    }
  },
  '1410': {
    'api': {
      '_manifest_features.json': json.dumps({
        'events': {
          'channel': 'beta'
        },
        'notifications': {
          'channel': 'dev'
        },
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'web_request': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'stable'
        },
        'bluetooth': {
          'channel': 'dev'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'context_menus': {
          'channel': 'trunk'
        },
        'declarativeContent': {
          'channel': 'trunk'
        },
        'declarativeWebRequest': [
          { 'channel': 'beta' },
          # whitelist
          { 'channel': 'stable'}
        ],
        'systemInfo.display': {
          'channel': 'stable'
        }
      }),
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    }
  },
  '1364': {
    'api': {
      '_manifest_features.json': json.dumps({
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'stable'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'systemInfo.display': {
          'channel': 'stable'
        },
        'webRequest': {
          'channel': 'stable'
        }
      }),
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    }
  },
  '1312': {
    'api': {
      '_manifest_features.json': json.dumps({
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'web_request': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'stable'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'systemInfo.display': {
          'channel': 'stable'
        }
      }),
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    }
  },
  '1271': {
    'api': {
      '_manifest_features.json': json.dumps({
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'system_info_display': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'alarms': {
          'channel': 'beta'
        },
        'bookmarks': {
          'channel': 'stable'
        },
        'webRequest': {
          'channel': 'stable'
        }
      }),
      'alarms.idl': '{}',
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}',
      'windows.json': '{}'
    }
  },
  '1229': {
    'api': {
      '_manifest_features.json': json.dumps({
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        },
        'web_request': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'bookmarks': {
          'channel': 'stable'
        },
        'systemInfo.display': {
          'channel': 'beta'
        }
      }),
      'alarms.idl': '{}',
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'system_info_display.idl': '{}',
      'tabs.json': '{}'
    }
  },
  '1180': {
    'api': {
      '_manifest_features.json': json.dumps({
        'page_action': {
          'channel': 'stable'
        },
        'runtime': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'bookmarks': {
          'channel': 'stable'
        },
        'webRequest': {
          'channel': 'stable'
        }
      }),
      'bookmarks.json': '{}',
      'idle.json': '{}',
      'input_ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}'
    }
  },
  '1132': {
    'api': {
      '_manifest_features.json': json.dumps({
        'bookmarks': {
          'channel': 'trunk'
        },
        'page_action': {
          'channel': 'stable'
        }
      }),
      '_permission_features.json': json.dumps({
        'webRequest': {
          'channel': 'stable'
        }
      }),
      'bookmarks.json': '{}',
      'idle.json': '{}',
      'input.ime.json': '{}',
      'menus.json': '{}',
      'tabs.json': '{}'
    }
  },
  '1084': {
    'api': {
      '_manifest_features.json': json.dumps({
        'contents': 'nothing of interest here,really'
      }),
      'idle.json': '{}',
      'input.ime.json': '{}',
      'menus.json': '{}',
      'pageAction.json': '{}',
      'tabs.json': '{}',
      'webRequest.json': '{}'
    }
  },
  '1025': {
    'api': {
      'idle.json': '{}',
      'input.ime.json': '{}',
      'menus.json': '{}',
      'pageAction.json': '{}',
      'tabs.json': '{}',
      'webRequest.json': '{}'
    }
  },
  '963': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        },
        {
          'namespace': 'webRequest'
        }
      ])
    }
  },
  '912': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        },
        {
          'namespace': 'experimental.webRequest'
        }
      ])
    }
  },
  '874': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '835': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '782': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '742': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '696': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '648': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '597': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '552': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        },
        {
          'namespace': 'pageAction'
        }
      ])
    }
  },
  '544': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        }
      ])
    }
  },
  '495': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'menus'
        }
      ])
    }
  },
  '396': {
    'api': {
      'extension_api.json': json.dumps([
        {
          'namespace': 'idle'
        },
        {
          'namespace': 'experimental.menus'
        }
      ])
    }
  }
}
