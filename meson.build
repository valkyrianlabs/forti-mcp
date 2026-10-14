project('forti-mcp', 'cpp',
        version : files('VERSION'),
        meson_version : '>=1.3.0',
        license : 'MIT',
        default_options : ['warning_level=3', 'cpp_std=c++23', 'buildtype=release', 'b_ndebug=if-release',
                          'forti-api:tests=disabled'])

# The command engine, built from forti-cli's source (subproject; forti-cli builds only the engine
# there). It brings forti-api, the only FortiOS implementation, with it.
forti_engine = dependency('forti-engine')

subdir('src')
subdir('tests')
