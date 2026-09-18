// ipset_wrapper.h
//
// Minimal, thread-safe C++ wrapper around Debian/Ubuntu libipset for
// programmatically adding/removing/testing IP-set members with a timeout.
//
// Typical use case: a WAF or IDS process detects an abusive client and
// calls IpsetSet::add(ip, timeout_seconds) to drop that IP into a set
// that an existing nftables/iptables rule routes to a tarpit (or DROP).
// See examples/waf_tarpit_demo.cpp and README.md for the full pipeline.
//
// Verified against libipset 7.19 (Ubuntu 24.04 "noble"). The libipset
// C API is NOT stable across major distro releases (ipset_session_init's
// signature, for one, changed between versions) -- if you're on an older
// or newer system, re-check include/libipset/session.h against this file
// before assuming it will compile unmodified.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>

extern "C" {
#include <libipset/data.h>
#include <libipset/parse.h>
#include <libipset/session.h>
#include <libipset/types.h>
}

namespace ipsettarpit {

// Result of an ipset operation. Distinguishes "already true" (e.g. adding
// an element that's already a member) from an actual failure, which the
// original 8-year-old version of this code collapsed into a single
// "ret = 0" success path -- silently hiding real errors from callers.
enum class IpsetResult {
    Ok,              // operation succeeded
    AlreadyPresent,  // ADD: element was already a member (not an error)
    NotPresent,      // DEL/TEST: element was not a member
    Error,           // a genuine failure; call last_error() for detail
};

// One ipset session wrapper, safe to share across threads: every public
// call takes an internal mutex, mirroring the fact that libipset's
// session/netlink handling is not thread-safe. For high call-rate use
// (e.g. inline in a request path), prefer one long-lived IpsetSet
// instance over constructing one per call -- ipset_session_init/fini
// is not free.
class IpsetSet {
   public:
    IpsetSet();
    ~IpsetSet();

    IpsetSet(const IpsetSet&) = delete;
    IpsetSet& operator=(const IpsetSet&) = delete;

    // Ensures a set named `setName` of type `setType` exists (e.g.
    // "hash:ip" or "hash:ip,port"), with the given default timeout in
    // seconds (0 = no default timeout; per-element timeouts still work
    // as long as the set was created with `-exist timeout` support,
    // which this always requests). Safe to call repeatedly -- an
    // already-existing set of the same type is treated as success.
    IpsetResult ensureSet(const std::string& setName, const std::string& setType,
                          uint32_t defaultTimeoutSeconds = 0);

    // Adds `element` (e.g. an IP, or "ip,port" depending on set type)
    // to `setName`, expiring after `timeoutSeconds` (0 = use the set's
    // default / no timeout). This is the WAF-facing call: "block this
    // attacker for N seconds."
    IpsetResult add(const std::string& setName, const std::string& element,
                    uint32_t timeoutSeconds);

    // Removes `element` from `setName` early (e.g. on manual unblock).
    IpsetResult del(const std::string& setName, const std::string& element);

    // Checks whether `element` is currently a member of `setName`.
    IpsetResult test(const std::string& setName, const std::string& element);

    // Human-readable detail for the most recent Error result on this
    // object. Empty if the last call did not error.
    const std::string& lastError() const { return lastError_; }

   private:
    IpsetResult runCmd(enum ipset_cmd cmd, const std::string& setName, const std::string& element,
                       uint32_t timeoutSeconds, const std::string& setType);

    std::mutex mutex_;
    std::string lastError_;
};

}  // namespace ipsettarpit
