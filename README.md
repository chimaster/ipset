# ipset-tarpit

A small, thread-safe C++ wrapper around Debian/Ubuntu `libipset` for
programmatically adding, removing, and testing IP-set members with a
timeout — built for the common WAF/IDS pattern of *"tag this attacker's
IP for N seconds, and let a firewall rule quietly stall them instead of
handing back a fast, cheap failure."*

Verified end-to-end against a live kernel (`libipset 7.19`,
Ubuntu 24.04 "noble") — every success/failure branch below was observed
against the real `ip_set` netlink interface, not inferred from headers.

## Why this exists

A lot of ipset+iptables tutorials show the *shell* side of tarpitting.
Very little shows the C/C++ side: how a WAF process itself adds an IP to
the set at the moment it detects abuse, with a timeout so the block
expires on its own. That's what this repo is for.

## The pipeline

```
 WAF / IDS detects abuse   ipset (kernel)              firewall rule
┌──────────────────────┐  ┌────────────────────┐      ┌────────────────────────┐
│ e.g. 5 malformed reqs │  │ waf_offenders       │      │ match src IP in the    │
│ in 10s from 203.0.113 │─▶│ hash:ip, timeout 300│─────▶│ set → TARPIT (or DROP) │
│ .7  → ipset.add(...)  │  │ auto-expires        │      │ instead of a normal    │
└──────────────────────┘  └────────────────────┘      │ REJECT/allow            │
                                                        └────────────────────────┘
```

1. Once, at startup, create the set (idempotent):
   ```cpp
   IpsetSet ipset;
   ipset.ensureSet("waf_offenders", "hash:ip", /*defaultTimeout=*/900);
   ```
2. One matching firewall rule, set up once, outside your process:
   ```sh
   # nftables (preferred on current distros)
   nft add table inet filter
   nft add set inet filter waf_offenders '{ type ipv4_addr; timeout 15m; }'
   nft add rule inet filter input ip saddr @waf_offenders drop
   # (swap `drop` for a tarpit-style delay if you're using xtables-addons'
   #  TARPIT target via iptables, since nftables has no native equivalent yet)

   # or classic iptables + xtables-addons TARPIT
   ipset create waf_offenders hash:ip timeout 900
   iptables -A INPUT -m set --match-set waf_offenders src -j TARPIT
   ```
3. Inline in your WAF's request path, when it flags a client:
   ```cpp
   ipset.add("waf_offenders", clientIp, 300); // tarpit this IP for 5 minutes
   ```
   That's the entire integration surface. The kernel handles expiry;
   your firewall rule handles routing traffic from that IP into the
   tarpit; this library just handles the "add/remove/check membership"
   plumbing safely from C++.

**Why ipset instead of just adding an iptables rule per IP:** iptables
rule evaluation is roughly linear in the number of rules; an ipset
lookup is a hash-table lookup, so this scales to thousands of blocked
IPs without slowing down every packet on the box. It's also why WAFs
use ipset for this instead of individually-managed DROP rules.

## API

```cpp
#include "ipset_wrapper.h"
using namespace ipsettarpit;

IpsetSet ipset; // one long-lived instance per process/worker;
                // construction is cheap, but re-init/fini per call is not

IpsetResult ensureSet(name, type, defaultTimeoutSeconds = 0);
IpsetResult add(setName, element, timeoutSeconds);
IpsetResult del(setName, element);
IpsetResult test(setName, element);
const std::string& lastError(); // populated only when a call returns Error
```

`IpsetResult` is one of `Ok`, `AlreadyPresent`, `NotPresent`, `Error`.
This is the main correctness fix over naive wrapper code you'll find
elsewhere: adding an element that's already a member, or deleting one
that's already gone, are **not** errors — they're reported as
`AlreadyPresent`/`NotPresent` so your WAF can log accurately, while an
actual failure (bad type, permission denied, module not loaded, set
doesn't exist) is reported as `Error` with a real message in
`lastError()`. See `src/ipset_wrapper.cpp` for exactly which
kernel-reported conditions map to which result — each branch there was
confirmed against a live `ip_set`, not assumed.

## Building

```sh
sudo apt-get install libipset-dev pkg-config cmake g++
cmake -B build && cmake --build build
sudo ./build/waf_tarpit_demo
```

`examples/waf_tarpit_demo.cpp` walks through the full create → add →
duplicate-add → test → delete → duplicate-delete cycle and prints the
`IpsetResult` for each step.

## Requirements

- Linux, with the `ip_set` kernel module (and the relevant set-type
  module, e.g. `ip_set_hash_ip`) loaded.
- `CAP_NET_ADMIN` — you do **not** need to run your WAF process as root
  for this; grant just that capability instead, e.g.:
  ```sh
  sudo setcap cap_net_admin+ep /path/to/your/waf_binary
  ```
- `libipset-dev` at build time (Debian/Ubuntu package name).

## Portability warning: libipset's ABI is not stable across distro versions

While building this, an 8-year-old version of this wrapper (using
`ipset_session_init(printf)`, single argument) failed to compile
against current `libipset` (7.19, Ubuntu 24.04) — the function gained a
second parameter and the callback signature changed to include the
session and a user pointer:

```c
// old (libipset ~5.x era):
struct ipset_session *ipset_session_init(ipset_outfn outfn);

// current (libipset 7.x, e.g. Ubuntu 24.04 "noble"):
struct ipset_session *ipset_session_init(ipset_print_outfn outfn, void *p);
```

`libipset/ui.h`, referenced in old examples, no longer ships either. If
you're targeting an older distro (e.g. Debian 9/10-era `libipset3`),
diff `/usr/include/libipset/session.h` against
`src/ipset_wrapper.cpp` before assuming this builds unmodified — this
is exactly the kind of drift that quietly breaks "it worked when I
wrote it 8 years ago" code.

## Testing

Every meaningful operation here (`ADD`/`DEL`/`TEST`, and even resolving
an existing set's type) requires live netlink access to the kernel, so
there isn't a pure-logic subset that mocks cleanly. `tests/` has one
integration test, gated on root + the `ipset` CLI being present, that
runs the demo and asserts on real kernel state afterward; it exits with
CMake's `SKIP_RETURN_CODE` (77) rather than failing when that access
isn't available, which is the common case in CI containers.

```sh
sudo ctest --test-dir build --output-on-failure
```

## Origins

This library grew out of an 8-year-old ipset smoke test, kept at
[`legacy/ipset_test.cpp`](legacy/ipset_test.cpp) for
reference. It no longer compiles as-is against current `libipset`
(`ipset_session_init()`'s signature changed, and `libipset/ui.h` was
removed — see the portability note above), and it silently mapped every
`ipset_cmd` failure to "success," which is what motivated the
`IpsetResult` error handling here. Worth a skim if you're curious how
the API drifted, or want the absolute-minimal single-file version
without the wrapper class.

## License

MIT (or match whatever the rest of this repository already uses).
