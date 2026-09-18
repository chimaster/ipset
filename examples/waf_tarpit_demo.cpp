// waf_tarpit_demo.cpp
//
// Simulates what a WAF/IDS process does when it flags an abusive client:
// drop the source IP into an ipset with a timeout. A companion firewall
// rule (see README.md) routes anything in that set into a tarpit instead
// of a normal DROP, so the attacker's connection stalls instead of
// getting a fast, cheap "retry immediately" signal.
//
// Run with sudo/CAP_NET_ADMIN (needs kernel netlink access to ip_set):
//   sudo ./waf_tarpit_demo

#include "ipset_wrapper.h"

#include <iostream>

using ipsettarpit::IpsetResult;
using ipsettarpit::IpsetSet;

static const char* resultName(IpsetResult r) {
    switch (r) {
        case IpsetResult::Ok: return "Ok";
        case IpsetResult::AlreadyPresent: return "AlreadyPresent";
        case IpsetResult::NotPresent: return "NotPresent";
        case IpsetResult::Error: return "Error";
    }
    return "?";
}

int main() {
    const std::string setName = "waf_offenders_demo";
    IpsetSet ipset;

    // One-time setup (idempotent -- safe to call at every WAF worker
    // startup). 900s default timeout: an offender auto-expires from the
    // block after 15 minutes even if nothing ever removes it explicitly.
    auto r = ipset.ensureSet(setName, "hash:ip", /*defaultTimeoutSeconds=*/900);
    std::cout << "ensureSet(" << setName << ") -> " << resultName(r) << "\n";
    if (r == IpsetResult::Error) {
        std::cerr << "  error: " << ipset.lastError() << "\n";
        std::cerr << "  (needs root / CAP_NET_ADMIN and the ip_set kernel module)\n";
        return 1;
    }

    const std::string attacker = "203.0.113.7"; // TEST-NET-3, safe example IP

    // This is the call a WAF makes inline when it detects abuse, e.g.
    // "5 malformed requests in 10 seconds from this IP" -> tarpit it
    // for 5 minutes.
    r = ipset.add(setName, attacker, /*timeoutSeconds=*/300);
    std::cout << "add(" << attacker << ", 300s) -> " << resultName(r) << "\n";

    // Calling add() again for the same IP within the window -- this is
    // the case the original version of this code silently mishandled.
    r = ipset.add(setName, attacker, 300);
    std::cout << "add(" << attacker << ", 300s) again -> " << resultName(r)
              << "  (correctly not a hard error)\n";

    r = ipset.test(setName, attacker);
    std::cout << "test(" << attacker << ") -> " << resultName(r) << "\n";

    r = ipset.test(setName, "198.51.100.9"); // a different, unblocked IP
    std::cout << "test(198.51.100.9) -> " << resultName(r) << "\n";

    // Manual early unblock, e.g. an admin override.
    r = ipset.del(setName, attacker);
    std::cout << "del(" << attacker << ") -> " << resultName(r) << "\n";

    r = ipset.del(setName, attacker);
    std::cout << "del(" << attacker << ") again -> " << resultName(r)
              << "  (correctly not a hard error)\n";

    if (r == IpsetResult::Error) {
        std::cerr << "  error: " << ipset.lastError() << "\n";
    }

    std::cout << "\nInspect with: sudo ipset list " << setName << "\n";
    std::cout << "Clean up with: sudo ipset destroy " << setName << "\n";
    return 0;
}
