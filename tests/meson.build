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
                            'unit/test_fuzz.cpp',
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

# Real binaries end to end: forti-mcp (and forti, for approvals) over stdio, through libcurl and verified
# TLS against forti-cli's stateful mock FortiOS (Python 3 stdlib + openssl; skipped without openssl).
# The mock and its harness come from the forti-cli subproject's source tree.
python3 = find_program('python3', required : false)
# A forti-cli that ships mock-fortios also has subproject_cli, which the top-level meson.build turns on
# whenever tests are not disabled.
mock_fortios_dir = forti_cli.get_variable('mock_fortios_dir', '')
if python3.found() and mock_fortios_dir != '' and not get_option('tests').disabled()
    forti_cli_exe = forti_cli.get_variable('forti_exe')
    test('mcp-e2e', python3, args : [files('integration/test_mcp.py')],
         env : {'FORTI_MCP_BINARY' : forti_mcp_exe.full_path(), 'FORTI_BINARY' : forti_cli_exe.full_path(),
                'FORTI_MOCK_DIR' : mock_fortios_dir, 'PYTHONDONTWRITEBYTECODE' : '1'},
         depends : [forti_mcp_exe, forti_cli_exe], suite : 'integration', timeout : 300)
    test('no-second-http-stack', python3,
         args : [mock_fortios_dir / '..' / 'integration' / 'scan_http.py', '--root', meson.project_source_root() / 'src',
                 '--binary', forti_mcp_exe.full_path()],
         depends : forti_mcp_exe, suite : 'integration')
else
    message('integration suite not built (needs python3 and a forti-cli subproject that ships tests/mock-fortios)')
endif
