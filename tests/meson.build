# Network-free tests: nothing here talks to a FortiGate. The engine runs against a scripted
# fortiapi::Transport (support/fake_transport.hpp); the end-to-end test spawns the built binary with
# temporary config, state and cache directories.
gtest_dep = dependency('gtest', main : true, required : get_option('tests'))
if gtest_dep.found()
    test('unit', executable('unitTests',
                            'unit/test_protocol.cpp',
                            'unit/test_tools.cpp',
                            'unit/test_budget.cpp',
                            'unit/test_options.cpp',
                            include_directories : include_directories('support'),
                            dependencies : [mcp_core_dep, gtest_dep],
                            implicit_include_directories : false),
         suite : 'unit')

    test('e2e', executable('e2eTests', 'e2e/test_stdio.cpp',
                           dependencies : [forti_engine.partial_dependency(includes : true), gtest_dep],
                           implicit_include_directories : false),
         env : {'FORTI_MCP_BINARY' : forti_mcp_exe.full_path()},
         depends : forti_mcp_exe,
         suite : 'e2e')
endif
