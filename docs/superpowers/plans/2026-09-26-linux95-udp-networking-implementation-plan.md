# Linux95 UDP Networking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` (or `superpowers:subagent-driven-development` if explicitly chosen) to implement this plan task-by-task. Each task uses TDD; run the named test RED before production code, then GREEN before committing.

**Goal:** Add a generic IPv4 UDP transport to Linux95 that builds, transmits, validates, receives, and dispatches datagrams over the existing network stack, with real QEMU transmit/echo proof.

**Architecture:** Keep UDP packet framing/checksums independent in `kernel/net/udp.*`, keep the eight-entry fixed binding registry independent in `kernel/net/udp_bindings.*`, and integrate both through the existing `network.cpp` IPv4/ARP polling path. Use the existing IPv4 builder, next-hop decision, ARP cache, Ethernet transmitter, RTL8139 poller, PIT uptime, and QEMU network test pattern. DNS remains a later consumer of UDP, not a dependency of this transport.

**Tech Stack:** Freestanding C++17 x86-64 kernel; fixed-size buffers and tables; host C++ unit/integration tests; Python QEMU smoke harness; QEMU user-mode networking and a local UDP echo responder.

**Spec:** `docs/superpowers/specs/2026-09-26-linux95-udp-networking-design.md` (approved design; authoritative over this plan).

**Planning baseline:** `v1.0-dev` merge commit `4f059a318bb4a9d5848e419f3673c0016c3731ac`. The planning workspace has the same tracked source tree as this baseline. Implementation must begin from a clean worktree based on current `v1.0-dev`, not by adding implementation commits to the preserved `ring3-processes` branch.

## Global Constraints

- Use IPv4 protocol number `17` for UDP while preserving ICMP protocol `1`.
- The current network layer uses a maximum IPv4 packet length of 1500 bytes and does not support IP fragmentation.
- UDP v1 therefore rejects transmit requests with payloads larger than **1472 bytes**.
- UDP v1 uses a fixed table of **8 kernel bindings**.
- UDP v1 supports **one unresolved-ARP pending datagram**.
- Pending UDP ARP resolution uses a **1 second** timeout measured with the existing PIT uptime clock.
- The QEMU harness will start a local UDP echo responder on host port `40000`. Linux95 will bind guest UDP port `40001`, send its test datagram from `40001` to `10.0.2.2:40000` through QEMU user networking, and require the echo reply to return to the `40001` binding.
- No dynamic allocation is required for UDP build/parse, the binding table, pending UDP transmit state, or receive dispatch.
- DNS is explicitly deferred to the next networking milestone after generic UDP is proven.
- Preserve existing RTL8139, Ethernet, ARP, IPv4, ICMP, desktop, no-network, Ring 3, and preemptive-scheduling behavior. Do not refactor those subsystems as part of UDP beyond the narrow IPv4 dispatch and ARP integration points below.
- Preserve the existing optional/missing-userspace startup behavior and fatal-user-fault isolation behavior.
- Do not add TCP, DHCP, IPv6, fragmentation/reassembly, sockets, per-process networking syscalls, DNS, or user-space UDP APIs.
- Do not generate ICMP Port Unreachable for unbound UDP destinations; safely ignore them.

## Review Focus

- An IPv4 payload may contain bytes after the UDP-declared length; verify they are excluded from the callback payload. Pin this in Task 1's parser tests.
- Boundary lengths can wrap when the eight-byte UDP header is added or converted; test empty payload, exactly 1472 bytes, 1473 bytes, and malformed declared lengths in Task 1.
- A nonzero bad UDP checksum must never invoke a service callback, while checksum zero remains valid for IPv4. Test the packet distinction in Task 1 and the callback boundary in Task 3.
- An ARP packet for a different next hop must not release the pending UDP packet; its timeout must free the slot without sending stale data. Test both in Task 4.
- UDP and ICMP can independently be waiting on the same ARP resolution; test that one ARP learning event advances both without replacing either operation in Task 4 and retain the existing QEMU ICMP regression in Task 5.

---

## File Map

- Create `kernel/net/udp.hpp` and `kernel/net/udp.cpp` for the allocation-free UDP datagram builder, parser, and IPv4 pseudo-header checksum.
- Create `kernel/net/udp_bindings.hpp` and `kernel/net/udp_bindings.cpp` for an instantiable fixed-capacity registry and callback dispatch; `network.cpp` owns the single production registry.
- Modify `kernel/net/network.hpp` to publish the UDP receive callback type and `bind_udp_port`, `unbind_udp_port`, and `send_udp` APIs.
- Modify `kernel/net/network.cpp` to reset/own the binding and pending states, dispatch IPv4 protocols 1 and 17, transmit UDP over existing routes, and resolve one pending UDP packet through existing ARP polling.
- Modify `Makefile` to compile/link new kernel sources and add focused host tests, network integration test, and a dedicated UDP QEMU test image/mode. Keep `test-preemption-source` as the first `test` prerequisite and preserve existing test order/contracts.
- Create `tests/host/udp_test.cpp` for the isolated packet codec/checksum contract.
- Create `tests/host/udp_bindings_test.cpp` for capacity, uniqueness, lifetime, and callback behavior.
- Create `tests/host/network_udp_test.cpp` with fake RTL8139 receive/transmit and PIT clock providers to exercise the production polling, dispatch, routing, and pending-ARP path. Host-mode diagnostic output must be suppressed without changing freestanding output.
- Modify `kernel/kernel.cpp` only for the compile-time QEMU UDP self-test setup/callback; it must not add a normal-boot UDP service or DNS behavior.
- Modify `tests/qemu_smoke.py` for `--udp-network-test`, a bounded local UDP echo responder, required ordered proof markers, and failure/reset detection.
- Do not modify process/scheduler, interrupt, desktop, storage, filesystem, user-program, or release files for this milestone.

## Interfaces Shared Across Tasks

Task 1 produces the packet API consumed by Tasks 3–5:

```cpp
namespace linux95::net::udp {

constexpr uint16_t kHeaderLength = 8;
constexpr uint16_t kMaxPayloadLength = 1472;

struct DatagramView {
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t* payload;
    uint16_t payload_length;
};

uint16_t checksum(const Ipv4Address& source,
                  const Ipv4Address& destination,
                  const uint8_t* datagram,
                  uint16_t datagram_length);

bool build(uint8_t* output, uint16_t capacity,
           const Ipv4Address& source,
           const Ipv4Address& destination,
           uint16_t source_port, uint16_t destination_port,
           const uint8_t* payload, uint16_t payload_length,
           uint16_t& output_length);

bool parse(const uint8_t* input, uint16_t enclosing_length,
           const Ipv4Address& source,
           const Ipv4Address& destination,
           DatagramView& out);

}
```

`checksum` computes the one's-complement result over the datagram bytes as supplied, including the checksum field. `build` zeroes that field before calculating and stores `0xFFFF` if the computed value is zero; `parse` verifies a nonzero field by requiring the result over the complete datagram to be zero. `parse` accepts a zero checksum, limits the view to declared UDP length, and rejects malformed lengths.

Task 2 produces a fixed registry used by Task 3:

```cpp
#include <stddef.h>

namespace linux95::net::udp::bindings {
constexpr size_t kMaxUdpBindings = 8;
using ReceiveCallback = void (*)(const Ipv4Address& source_address,
                                 uint16_t source_port,
                                 uint16_t destination_port,
                                 const uint8_t* payload,
                                 uint16_t payload_length,
                                 void* context);
struct Binding {
    bool in_use;
    uint16_t local_port;
    ReceiveCallback callback;
    void* callback_context;
};
struct Table {
    Binding entries[kMaxUdpBindings];
};
void reset(Table& table);
bool bind(Table& table, uint16_t port,
          ReceiveCallback callback, void* context);
bool unbind(Table& table, uint16_t port);
bool dispatch(Table& table, const Ipv4Address& source_address,
              uint16_t source_port, uint16_t destination_port,
              const uint8_t* payload, uint16_t payload_length);
}
```

Public production API in `network.hpp`:

```cpp
using UdpReceiveCallback = net::udp::bindings::ReceiveCallback;
bool bind_udp_port(uint16_t port, UdpReceiveCallback callback, void* context);
bool unbind_udp_port(uint16_t port);
bool send_udp(const net::Ipv4Address& destination,
              uint16_t source_port, uint16_t destination_port,
              const uint8_t* payload, uint16_t payload_length);
```

`send_udp` returns true only if the datagram is transmitted immediately or accepted into the one pending-ARP slot; it does not promise peer delivery. Callback payload storage aliases the current receive frame and is valid only during the callback.

---

### Task 1: UDP Datagram Builder, Parser, and Checksum

**Files:**
- Create: `tests/host/udp_test.cpp`
- Create: `kernel/net/udp.hpp`
- Create: `kernel/net/udp.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `net::Ipv4Address` from `kernel/net/net_types.hpp`.
- Produces: `net::udp::{DatagramView, checksum, build, parse}`, consumed by Tasks 3–5.

- [ ] **Step 1: Write failing codec tests first.** Add named assertions for `[PASS] udp_build`, `[PASS] udp_parse`, `[PASS] udp_checksum`, `[PASS] udp_checksum_zero_encoded_ffff`, `[PASS] udp_zero_checksum_ipv4`, and `[PASS] udp_bad_checksum_rejected`. Cover network-byte-order ports, zero-length payload, odd-length payload checksum, known pseudo-header checksum vector, a bounded payload search whose computed checksum is zero and whose encoded field must be `0xFFFF`, 1472-byte maximum build, 1473-byte rejection, null pointer with nonzero length, null parser input, short header, declared length below 8, declared length longer than enclosing payload, trailing enclosing bytes ignored, and valid/invalid nonzero checksums.
- [ ] **Step 2: Run the new target before adding production code.** Run `make build/host-udp-test`. Expected: RED because `net/udp.hpp` and its API do not yet exist.
- [ ] **Step 3: Implement the minimal packet API in `kernel/net/udp.hpp/.cpp`.** Read/write UDP fields in network byte order. Check all lengths before reading/writing. Compute checksum over source/destination IPv4, zero, protocol 17, UDP length, checksum-zeroed header, and payload; pad an odd final byte as the high byte. `parse` accepts a zero checksum, verifies a nonzero one, and exposes only `udp_length - 8` payload bytes.
- [ ] **Step 4: Run focused GREEN.** Run `make build/host-udp-test && ./build/host-udp-test`. Expected: all codec/checksum assertions pass without unresolved symbols.
- [ ] **Step 5: Commit.** `git add Makefile kernel/net/udp.hpp kernel/net/udp.cpp tests/host/udp_test.cpp && git commit -m "Add generic UDP packet codec"`.

### Task 2: Fixed Eight-Port UDP Binding Registry

**Files:**
- Create: `tests/host/udp_bindings_test.cpp`
- Create: `kernel/net/udp_bindings.hpp`
- Create: `kernel/net/udp_bindings.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: Task 1 `DatagramView`-independent `Ipv4Address` and callback metadata types.
- Produces: `net::udp::bindings::Table`, `bind`, `unbind`, `dispatch`, and `kMaxUdpBindings == 8`, consumed by Task 3.

- [ ] **Step 1: Write failing registry tests first.** Test binding ports 1–8, rejection of port zero/null callback/duplicate/full-table ninth binding, unbinding a missing port without side effects, successful unbind/rebind, exactly the matching destination callback, ignored unbound port, correct source IP/ports/payload/context, and callback payload lifetime documented as call-only.
- [ ] **Step 2: Run RED.** Run `make build/host-udp-bindings-test`. Expected: compile failure because the registry API does not exist.
- [ ] **Step 3: Implement the fixed registry.** Use exactly eight in-object entries and no heap or global-only singleton so the same registry implementation can be instantiated in host tests. Dispatch only one matching binding and never invoke a callback for absent ports.
- [ ] **Step 4: Run GREEN.** Run `make build/host-udp-bindings-test && ./build/host-udp-bindings-test`. Expected: all capacity, binding, unbinding, and callback assertions pass.
- [ ] **Step 5: Commit.** `git add Makefile kernel/net/udp_bindings.hpp kernel/net/udp_bindings.cpp tests/host/udp_bindings_test.cpp && git commit -m "Add fixed UDP port bindings"`.

### Task 3: IPv4 UDP Receive and Port Dispatch

**Files:**
- Create: `tests/host/network_udp_test.cpp`
- Modify: `kernel/net/network.hpp`
- Modify: `kernel/net/network.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: Task 1 `udp::parse`; Task 2 `udp::bindings::Table` and operations; existing `ipv4::parse`, `EthernetView`, RTL8139 poll API.
- Produces: public `network::{UdpReceiveCallback, bind_udp_port, unbind_udp_port}` and IPv4 protocol dispatch for ICMP 1 / UDP 17.

- [ ] **Step 1: Write failing production-path integration tests first.** Create `network_udp_test.cpp` with fake RTL8139 initialization, transmit, and receive providers plus a controllable PIT uptime provider. Queue real Ethernet/IPv4/UDP frames into the fake receive ring and call production `network::poll()`. Verify protocol 17 reaches exactly the bound callback with source IP/port, destination port, and payload; unbound port, bad nonzero checksum, nonlocal destination, and other IPv4 protocol never call it; protocol 1 still reaches the existing ICMP echo result path. Do not test a copied dispatch implementation.
- [ ] **Step 2: Run RED.** Run `make build/host-network-udp-test`. Expected: compile/link failure because public UDP bindings and production UDP dispatch are not implemented.
- [ ] **Step 3: Integrate UDP receive into `network.cpp`.** Own/reset the production `Table`, forward public bind/unbind calls, preserve existing local-destination filter, switch the IPv4 receive branch by protocol (1 stays byte-for-byte behaviorally equivalent for ICMP; 17 parses/checks UDP and dispatches its destination binding; other values are ignored). Add `[PASS] udp_ready` only after networking initialization succeeds. Keep diagnostics silent in hosted integration-test builds while preserving freestanding debug output.
- [ ] **Step 4: Run focused GREEN and existing network host tests.** Run `make build/host-network-udp-test && ./build/host-network-udp-test && make build/host-ipv4-test build/host-icmp-test && ./build/host-ipv4-test && ./build/host-icmp-test`. Expected: UDP callback dispatch cases pass and all current IPv4/ICMP behavior remains green.
- [ ] **Step 5: Commit.** `git add Makefile kernel/net/network.hpp kernel/net/network.cpp tests/host/network_udp_test.cpp && git commit -m "Dispatch IPv4 UDP to bound services"`.

### Task 4: UDP Transmit, Routing, and One Pending ARP Datagram

**Files:**
- Modify: `tests/host/network_udp_test.cpp`
- Modify: `kernel/net/network.hpp`
- Modify: `kernel/net/network.cpp`
- Modify: `Makefile` only if test linkage changes are needed

**Interfaces:**
- Consumes: Task 1 `udp::build`; Task 3 network status, fixed bindings, frame poll/dispatch; existing `ipv4::build/next_hop`, `arp::{lookup,build_request}`, `pit::uptime_seconds`, Ethernet and RTL8139 transmit.
- Produces: `network::send_udp` and single fixed pending-ARP state, exercised by Task 5.

- [ ] **Step 1: Extend host integration tests before transmit implementation.** Test validation for offline network, zero source/destination port, null/nonzero payload, 1472/1473 lengths, immediate cached-ARP transmit, protocol 17 and valid pseudo-header checksum on the captured packet, same-subnet versus gateway next-hop selection, first unresolved send accepted, a second unresolved send rejected without replacing bytes, a cached-mapping send still succeeds while the pending slot is occupied, pending transmission exactly once after matching next-hop learning, unrelated next-hop traffic ignored, timeout after one PIT-uptime second clears the slot, a later request can then claim it, failed ARP/RTL transmission cleanup, and simultaneous pending ICMP/UDP progress on one learned mapping.
- [ ] **Step 2: Run RED.** Run `make build/host-network-udp-test && ./build/host-network-udp-test`. Expected: failing assertions or compile error for missing `send_udp` and pending-ARP behavior.
- [ ] **Step 3: Implement transmit and pending state in `network.cpp`.** Validate inputs before touching shared state. Build UDP and IPv4 using fixed stack buffers. Route through `ipv4::next_hop()` and `arp::lookup()`. For cached ARP transmit immediately. Otherwise keep one fixed pending complete IPv4 packet, next hop, length, and `uptime_seconds()+1` deadline; send ARP and roll back the reservation if that send fails. Reject only a second unresolved send; allow cached sends to proceed without overwriting pending storage. On ARP processing, release only when the learned mapping matches the pending next hop and transmit it once; timeout clears state. Do not couple pending UDP state to the ICMP ping state.
- [ ] **Step 4: Run focused GREEN.** Run `make build/host-network-udp-test && ./build/host-network-udp-test`. Expected: all send, route, ARP, timeout, independence, and failure cleanup assertions pass.
- [ ] **Step 5: Commit.** `git add Makefile kernel/net/network.hpp kernel/net/network.cpp tests/host/network_udp_test.cpp && git commit -m "Send UDP through IPv4 and ARP"`.

### Task 5: Real UDP Echo Through RTL8139/QEMU and Full Regression

**Files:**
- Modify: `kernel/kernel.cpp`
- Modify: `Makefile`
- Modify: `tests/qemu_smoke.py`
- Modify: `tests/source_checks.py` only for narrow UDP architecture invariants

**Interfaces:**
- Consumes: Tasks 1–4 UDP codec, bindings, receive dispatch, and `send_udp`; current `LINUX95_QEMU_NETWORK_SELF_TEST`/network-test image convention.
- Produces: `--udp-network-test` QEMU proof with `[PASS] udp_tx` and `[PASS] udp_rx` from actual transmit and callback receipt.

- [ ] **Step 1: Add the QEMU harness expectation first.** Extend `tests/qemu_smoke.py` with mutually exclusive `--udp-network-test`. Start a local bounded UDP echo responder on port 40000 before QEMU; require it to receive the exact test payload and return it. Require ordered `[PASS] udp_tx`, `[PASS] udp_rx`, existing desktop/network markers, and fail on panic, malformed completion, QEMU reset, or missing response. Run `python3 tests/qemu_smoke.py --udp-network-test` before enabling production test traffic. Expected: RED because UDP transport/self-test markers and real echo response do not yet exist.
- [ ] **Step 2: Add a dedicated UDP self-test image rule.** Build a kernel object with a UDP-test-only compile define while retaining the existing network self-test and ping proof. Do not make the normal kernel auto-send test UDP. Ensure the `make test-qemu` prerequisites build all user ELFs/fixtures before the harness, with no races under parallel make.
- [ ] **Step 3: Add the test-only guest sender/binding.** In `kernel.cpp`, under the UDP-test-only define, bind guest port 40001 with a callback that checks peer `10.0.2.2:40000`, expected destination port, exact payload, and length before the receive proof marker. Send the fixed test payload to `10.0.2.2:40000`; emit no success marker on bind/send failure. In `network.cpp`, emit `[PASS] udp_tx` only after a real RTL8139 transmit succeeds (including release of a pending packet), and emit `[PASS] udp_rx` only after a valid parsed packet reaches the callback path; keep both one-shot.
- [ ] **Step 4: Run focused QEMU GREEN and inspect evidence.** Run `make build/linux95-udp-network-test.img && python3 tests/qemu_smoke.py --udp-network-test`. Expected: QEMU boots with RTL8139, transmits a valid IPv4 protocol-17 datagram through QEMU user networking, the local responder echoes it, the guest validates and dispatches the reply, and both required markers appear in causal order. Confirm no Internet access is required.
- [ ] **Step 5: Add narrow source checks.** Assert UDP protocol 17 and fixed limit/binding values, no DNS include/dependency in UDP transport files, no dynamic allocation in codec/binding/pending implementation, and that `make test-qemu` includes the real UDP mode. Keep checks structural and preserve all existing source-check ordering.
- [ ] **Step 6: Run full milestone verification.** Run `make clean && make all`, `make test`, `make test-qemu`, `python3 tests/qemu_smoke.py --udp-network-test`, `python3 tests/qemu_smoke.py --without-network`, `python3 tests/qemu_smoke.py --without-user-programs`, `python3 tests/qemu_smoke.py --process-self-test`, `python3 tests/qemu_smoke.py --process-fault-test`, and `python3 tests/qemu_smoke.py --process-preemption-test`. Then run `nm -u build/kernel.elf`, `git diff --check`, and `git status --short --branch`. Expected: all existing network, desktop, Ring 3, user-fault, missing-userspace, and preemption proofs remain green; no unresolved symbol; no diff-check output; only planned UDP files changed.
- [ ] **Step 7: Commit.** `git add Makefile kernel/kernel.cpp kernel/net/network.hpp kernel/net/network.cpp kernel/net/udp.hpp kernel/net/udp.cpp kernel/net/udp_bindings.hpp kernel/net/udp_bindings.cpp tests/host/udp_test.cpp tests/host/udp_bindings_test.cpp tests/host/network_udp_test.cpp tests/qemu_smoke.py tests/source_checks.py && git commit -m "Add generic IPv4 UDP networking"`.

## Final Acceptance

- UDP packet build/parse/checksum host tests pass, including checksum-zero acceptance, nonzero checksum rejection, odd payload, and all length boundaries.
- Fixed eight-entry binding tests pass, including duplicate/full/unbind/rebind behavior and metadata correctness.
- Production network polling integration tests prove protocol 17 dispatch and preserve protocol 1 ICMP behavior.
- Production network host integration proves immediate/cached send, next-hop route selection, bounded single pending ARP datagram, one-second timeout, matching-resolution release exactly once, and no corruption of ICMP state.
- Dedicated QEMU UDP test proves a real guest UDP transmit and host echo reply delivered back through RTL8139, IPv4, UDP checksum validation, and binding dispatch, without external Internet.
- `make test`, `make test-qemu`, existing preemption/Ring 3/fault/no-network modes, unresolved-symbol check, and diff check all pass.
- No DNS code or DNS-specific transport behavior is added.
