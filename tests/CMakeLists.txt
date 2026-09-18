# All meaningful ipset operations (ADD/DEL/TEST, and even resolving a set's
# type for an existing set) require live netlink access to the kernel's
# ip_set subsystem -- there isn't a pure-logic subset of this wrapper that
# can be unit tested without CAP_NET_ADMIN and the ip_set kernel module
# loaded. So this is a single integration test, gated on root, rather than
# a mocked unit test suite. See README.md "Testing" section.

find_program(BASH_PROGRAM bash)

if(BASH_PROGRAM)
    add_test(
        NAME ipset_integration
        COMMAND ${BASH_PROGRAM} ${CMAKE_CURRENT_SOURCE_DIR}/integration_test.sh
                $<TARGET_FILE:waf_tarpit_demo>
    )
    set_tests_properties(ipset_integration PROPERTIES
        SKIP_RETURN_CODE 77
    )
endif()
