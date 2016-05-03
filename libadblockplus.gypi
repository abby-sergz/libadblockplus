{
  'includes': ['common.gypi'],
  'conditions': [
    ['OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris" \
       or OS=="netbsd" or OS=="android"', {
      'target_defaults': {
        # V8 headers cause a build error on recent gcc.
        # Adding -fpermissive to solve this.
        # See https://issues.adblockplus.org/ticket/4950
        # We might be able to do without after upgrading V8.
        'cflags_cc': [ '-Wall', '-W', '-Wno-unused-parameter',
                    '-Wnon-virtual-dtor', '-pedantic',
                    '-fexceptions', '-fpermissive' ]
      }
    }],
    ['OS=="win"', {
      'target_defaults': {
        'conditions': [
          ['target_arch=="x64"', {
            'msvs_configuration_platform': 'x64'
          }]
        ],
        'msvs_configuration_attributes': {
          'CharacterSet': '1',
        },
        'msbuild_toolset': 'v140_xp',
        'defines': [
          'WIN32',
        ],
        'link_settings': {
          'libraries': ['-lDbgHelp'],
        },
      }
    }],
  ],

  'target_defaults': {
    'configurations': {
      'Debug': {
        'defines': [
          'DEBUG'
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'conditions': [
              ['component=="shared_library"', {
                'RuntimeLibrary': '3',  #/MDd
              }, {
                'RuntimeLibrary': '1',  #/MTd
              }
            ]]
          }
        }
      },
      'Release': {
        'msvs_settings': {
          'VCCLCompilerTool': {
            'conditions': [
              ['component=="shared_library"', {
                'RuntimeLibrary': '2',  #/MD
              }, {
                'RuntimeLibrary': '0',  #/MT
              }
            ]]
          }
        }
      }
    },
  }
}
