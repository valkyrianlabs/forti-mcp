# forti_mcp_version.hpp carries the project version into the binary (serverInfo, --version), and for
# --version what it was built from (the forti-cli engine revision and the forti-api version; see the
# top-level meson.build).
version_hpp = configure_file(output : 'forti_mcp_version.hpp',
                             configuration : {'FORTI_MCP_VERSION' : '"' + meson.project_version() + '"',
                                              'FORTI_MCP_BUILT_FROM' : '"engine forti-cli ' + engine_revision
                                                                       + ', forti-api ' + forti_api_version + '"'})

mcp_inc = include_directories('.')

# The protocol core (no I/O), shared by the executable and the tests.
mcp_core = static_library('forti-mcp-core',
                          'protocol.cpp', 'tools.cpp', 'budget.cpp', 'options.cpp',
                          version_hpp,
                          include_directories : mcp_inc,
                          dependencies : forti_engine,
                          implicit_include_directories : false)
mcp_core_dep = declare_dependency(include_directories : mcp_inc, link_with : mcp_core,
                                  dependencies : forti_engine, sources : version_hpp)

forti_mcp_exe = executable('forti-mcp', 'main.cpp', 'stdio.cpp',
                           dependencies : mcp_core_dep,
                           implicit_include_directories : false,
                           install : true)
