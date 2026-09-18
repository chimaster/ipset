# CMake generated Testfile for 
# Source directory: /home/jenny/projects/ipset/tests
# Build directory: /home/jenny/projects/ipset/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ipset_integration "/usr/bin/bash" "/home/jenny/projects/ipset/tests/integration_test.sh" "/home/jenny/projects/ipset/build/waf_tarpit_demo")
set_tests_properties(ipset_integration PROPERTIES  SKIP_RETURN_CODE "77" _BACKTRACE_TRIPLES "/home/jenny/projects/ipset/tests/CMakeLists.txt;11;add_test;/home/jenny/projects/ipset/tests/CMakeLists.txt;0;")
