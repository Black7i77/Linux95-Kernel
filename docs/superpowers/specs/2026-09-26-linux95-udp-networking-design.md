# Linux95 UDP Networking Design

**Date:** 2026-09-26
**Target branch:** `v1.0-dev`
**Repository:** `Black7i77/Linux95-Kernel`
**Status:** Design approved in conversation; written-spec review required before implementation planning.

## 1. Purpose

Linux95 already has a polling RTL8139 network driver, Ethernet framing, ARP, IPv4, and ICMP echo support. The graphical host loop continues to poll networking while Ring 3 processes are cooperatively or preemptively scheduled.

The next networking milestone adds a **generic IPv4 UDP transport layer**. UDP must be reusable by later protocols, especially DNS, rather than being implemented as a DNS-specific shortcut.

Success means Linux95 can build, transmit, receive, parse, checksum, validate, and dispatch IPv4 UDP datagrams while preserving all existing RTL8139, ARP, IPv4, ICMP, desktop, no-network, Ring 3, and preemptive-scheduling behavior.

## 2. Goals

1. Add a reusable UDP packet layer under `kernel/net/`.
2. Support UDP transmit and receive over the existing IPv4 stack.
3. Use IPv4 protocol number `17` for UDP while preserving ICMP protocol `1`.
4. Implement full IPv4 UDP checksum generation and verification.
5. Accept an incoming UDP checksum field of zero, as permitted for IPv4 UDP.
6. Reject malformed UDP packets safely without panicking the kernel.
7. Provide a small fixed table of UDP port bindings for kernel network services.
8. Reuse the existing IPv4 next-hop decision and ARP cache.
9. Support one pending UDP transmit while ARP resolution is in progress.
10. Add host unit tests, network integration tests, and a QEMU proof using real UDP traffic.

## 3. Non-goals

This milestone does **not** add:

- DNS;
- TCP;
- DHCP;
- IPv6;
- IP fragmentation or reassembly;
- UDP fragmentation support;
- Unix/BSD sockets;
- dynamic socket allocation;
- per-process networking syscalls;
- user-space UDP sockets;
- multiple queued unresolved-ARP UDP transmits;
- ICMP Port Unreachable generation for unbound UDP ports;
- multicast or broadcast UDP APIs beyond what the existing Ethernet/IP layer naturally supports;
- a general asynchronous network task system.

DNS is explicitly deferred to the next networking milestone after generic UDP is proven.

## 4. Existing architecture to preserve

Linux95 networking currently has the following layering:

```text
RTL8139
   -> Ethernet
      -> ARP
      -> IPv4
          -> ICMP
```

The existing `network::poll()` loop receives a bounded number of Ethernet frames from RTL8139 and dispatches ARP or IPv4 traffic. The IPv4 parser validates header length, total length, fragmentation state, and header checksum. The IPv4 builder already accepts a generic protocol number and builds normal non-fragmented IPv4 packets.

Existing routing support provides:

- local IPv4 address `10.0.2.15`;
- netmask `255.255.255.0`;
- gateway `10.0.2.2`;
- `ipv4::next_hop()` for local-subnet versus gateway routing;
- ARP cache lookup and request/reply handling;
- a polling receive path;
- Ethernet transmission through RTL8139.

UDP extends this architecture. It does not introduce a second network stack, second ARP implementation, or second IPv4 builder.

## 5. Chosen architecture

The selected architecture is:

```text
RTL8139
   -> Ethernet
      -> ARP
      -> IPv4
          -> ICMP
          -> UDP
              -> fixed kernel port-binding table
                  -> future DNS and other kernel services
```

Outgoing flow:

```text
service payload
   -> UDP header/checksum
   -> IPv4 protocol 17
   -> ipv4::next_hop()
   -> ARP lookup
      -> cached: transmit now
      -> unresolved: hold one pending UDP datagram, send ARP request
   -> Ethernet
   -> RTL8139
```

Incoming flow:

```text
RTL8139 frame
   -> Ethernet validation
   -> IPv4 validation
   -> protocol 17
   -> UDP validation/checksum
   -> destination-port binding lookup
   -> invoke matching callback
```

## 6. UDP packet layer

Add:

```text
kernel/net/udp.hpp
kernel/net/udp.cpp
```

The packet layer is independent of RTL8139 and ARP. Its job is only to understand UDP framing and checksums.

A parsed view is conceptually:

```cpp
struct DatagramView {
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t* payload;
    uint16_t payload_length;
};
```

The API should expose narrow operations for:

```text
build UDP datagram
parse UDP datagram
calculate IPv4 UDP checksum
verify IPv4 UDP checksum
```

The UDP header is the standard 8-byte header:

```text
0..1  source port
2..3  destination port
4..5  UDP length
6..7  checksum
8..   payload
```

All UDP multi-byte fields are network byte order.

## 7. UDP checksum semantics

### 7.1 Outgoing packets

Every outgoing Linux95 UDP datagram uses a proper UDP checksum computed over:

```text
IPv4 source address
IPv4 destination address
zero byte
protocol = 17
UDP length
UDP header with checksum field zeroed
UDP payload
```

This is the IPv4 pseudo-header checksum required by UDP.

If the computed one's-complement checksum numerically becomes zero, the transmitted checksum field must be encoded as `0xFFFF` so it is not mistaken for the IPv4 "checksum omitted" value.

### 7.2 Incoming packets

For incoming IPv4 UDP:

- checksum field `0` is accepted;
- nonzero checksum must verify successfully against the IPv4 pseudo-header;
- a failed nonzero checksum causes the packet to be dropped;
- checksum failure never panics the kernel and never invokes a port callback.

## 8. Packet validation

The UDP parser rejects a packet when any of the following is true:

- input pointer is null;
- IPv4 payload is shorter than 8 bytes;
- UDP length is less than 8 bytes;
- UDP length is greater than the enclosing IPv4 payload length;
- checksum is nonzero and invalid;
- any required length conversion would overflow its fixed-width type.

If the enclosing IPv4 payload contains bytes after the declared UDP length, those bytes are ignored by the UDP view rather than treated as UDP payload.

A valid payload length is:

```text
udp_length - 8
```

## 9. MTU and payload limits

The current network layer uses a maximum IPv4 packet length of 1500 bytes and does not support IP fragmentation.

With a normal 20-byte IPv4 header and an 8-byte UDP header:

```text
1500 IPv4 bytes
- 20 IPv4 header
-  8 UDP header
----------------
1472 maximum UDP payload bytes
```

UDP v1 therefore rejects transmit requests with payloads larger than **1472 bytes**.

Incoming fragmented IPv4 packets remain rejected by the existing IPv4 parser. UDP does not add reassembly.

## 10. Port binding model

UDP v1 uses a fixed table of **8 kernel bindings**:

```cpp
constexpr size_t kMaxUdpBindings = 8;
```

Each entry conceptually contains:

```text
in_use
local_port
callback
callback_context
```

A callback receives enough information for a protocol such as DNS to validate its peer and copy the response it needs:

```text
source IPv4 address
source port
destination port
payload pointer
payload length
callback context
```

The payload pointer is valid only for the duration of the callback because it refers to the current receive-frame buffer. A consumer that needs the data after the callback must copy it.

### 10.1 Binding rules

- binding an unused nonzero port succeeds when a table slot is available;
- binding a port that is already bound fails;
- binding port zero fails;
- binding with a null callback fails;
- unbinding an existing port succeeds;
- unbinding a port that is not bound reports failure without side effects;
- receive traffic to an unbound UDP port is ignored safely;
- no ICMP Port Unreachable response is generated in v1.

The table uses no dynamic allocation.

## 11. Public network API

The public kernel-network API will expose these responsibilities (minor C++ type aliases may vary without changing semantics):

```cpp
bool bind_udp_port(uint16_t port,
                   UdpReceiveCallback callback,
                   void* context);

bool unbind_udp_port(uint16_t port);

bool send_udp(const net::Ipv4Address& destination,
              uint16_t source_port,
              uint16_t destination_port,
              const uint8_t* payload,
              uint16_t payload_length);
```

`send_udp()` is asynchronous with respect to ARP resolution: returning success means the request was accepted for immediate transmit or for the single pending-ARP slot. It does not guarantee that a peer will reply.

## 12. IPv4 dispatch integration

The existing IPv4 receive path currently recognizes ICMP protocol `1`.

It becomes a protocol dispatcher:

```text
protocol 1  -> existing ICMP handling
protocol 17 -> UDP handling
other       -> ignore
```

ICMP behavior must remain unchanged.

UDP processing occurs only for IPv4 packets addressed to the local Linux95 IPv4 address according to the existing receive policy.

## 13. Outgoing routing and ARP

For every UDP send:

1. validate network-online state, ports, payload pointer, and payload length;
2. build the UDP datagram including checksum using the local and destination IPv4 addresses;
3. build the IPv4 packet with protocol `17`;
4. compute the next hop using the existing `ipv4::next_hop()`;
5. query the existing ARP cache;
6. if a MAC is cached, transmit the Ethernet/IPv4/UDP packet immediately;
7. otherwise reserve the single pending-UDP slot and transmit an ARP request for the next hop;
8. when a later ARP reply populates the cache, transmit the pending UDP datagram;
9. clear the pending slot after successful transmit or timeout/failure.

The gateway and same-subnet routing rules are therefore identical to ICMP.

## 14. Pending ARP transmit state

UDP v1 supports **one unresolved-ARP pending datagram**.

The pending state must be fixed-size and allocation-free. It conceptually stores:

```text
active
next-hop IPv4 address
destination IPv4 address
source port
destination port
payload bytes and payload length
deadline
```

The implementation may instead store a fully built IPv4 packet if that keeps the state smaller or simpler. Whichever representation is chosen, it must fit fixed static storage and preserve the outgoing checksum/identification semantics.

Rules:

- if no pending entry exists, an unresolved send may claim it;
- while the pending slot is occupied, a second UDP send that also requires unresolved ARP is rejected as busy;
- an immediate send whose ARP mapping is already cached may still proceed if doing so does not corrupt shared temporary buffers;
- pending UDP ARP resolution uses a **1 second** timeout measured with the existing PIT uptime clock;
- timeout clears the slot;
- receiving a suitable ARP reply causes the queued datagram to transmit exactly once;
- unrelated ARP traffic must not accidentally release or transmit the pending datagram;
- UDP pending-ARP state is independent of the existing ICMP ping state, so an ARP reply may legitimately advance both operations when it resolves the next hop required by both.

## 15. Buffering and allocation rules

UDP v1 uses fixed stack/static buffers consistent with the existing network implementation.

The receive callback must not retain pointers to the receive frame.

The IRQ path remains lightweight. UDP packet parsing and callbacks happen from the existing polling network path, not directly from RTL8139 IRQ context.

No heap allocation is required for:

- UDP build/parse;
- UDP binding table;
- pending UDP transmit state;
- receive dispatch.

## 16. Error handling

No malformed packet or normal network error should panic Linux95.

Transmit requests fail cleanly when:

- networking is offline;
- source port is zero;
- destination port is zero;
- payload pointer is null while payload length is nonzero;
- payload exceeds 1472 bytes;
- UDP build fails;
- IPv4 build fails;
- Ethernet/RTL8139 transmit fails;
- a new unresolved-ARP datagram cannot claim the pending slot.

Receive packets are ignored when:

- IPv4 validation fails;
- destination is not local according to existing policy;
- UDP header/length validation fails;
- nonzero UDP checksum fails;
- destination UDP port is unbound.

These paths must leave ICMP and later packets functional.

## 17. Diagnostics

Milestone diagnostics should be one-shot evidence rather than per-packet logging.

Expected proof markers may include:

```text
[PASS] udp_ready
[PASS] udp_tx
[PASS] udp_rx
```

Host tests should provide more granular named assertions for UDP build, parse, checksum, binding, and malformed-packet behavior.

Normal production traffic should not flood the debug console with a line for every UDP packet.

## 18. Testing strategy

### 18.1 Host unit tests

Add focused UDP tests covering at least:

```text
[PASS] udp_build
[PASS] udp_parse
[PASS] udp_checksum
[PASS] udp_zero_checksum_ipv4
[PASS] udp_bad_checksum_rejected
[PASS] udp_port_binding
[PASS] udp_unbound_port_ignored
```

Tests should include:

- network-byte-order port encoding;
- empty payload;
- odd-length payload checksum;
- maximum 1472-byte payload build where practical;
- too-short header rejection;
- UDP length below 8 rejection;
- UDP length beyond enclosing IPv4 payload rejection;
- nonzero valid checksum acceptance;
- nonzero invalid checksum rejection;
- zero IPv4 UDP checksum acceptance;
- duplicate binding rejection;
- bind-table capacity behavior;
- unbind/rebind behavior.

### 18.2 Network integration tests

Integration tests should prove:

- IPv4 protocol `17` reaches UDP;
- IPv4 protocol `1` still reaches the existing ICMP path;
- a datagram reaches exactly the callback bound to its destination port;
- traffic to an unbound port is ignored;
- callback metadata reports the correct source IP, source port, destination port, and payload;
- invalid UDP packets do not invoke callbacks;
- pending ARP transmit is released only by resolution of the correct next hop.

### 18.3 QEMU proof

Add an automated QEMU networking proof that uses actual packets through the RTL8139 device and QEMU user networking.

The proof must demonstrate both directions:

```text
Linux95 -> real UDP packet transmitted
peer/QEMU-side responder -> real UDP reply
Linux95 -> reply parsed and delivered to bound UDP port
```

The QEMU harness will start a local UDP echo responder on host port `40000`. Linux95 will bind guest UDP port `40001`, send its test datagram from `40001` to `10.0.2.2:40000` through QEMU user networking, and require the echo reply to return to the `40001` binding. The test must not depend on Internet access.

Required milestone evidence:

```text
[PASS] udp_tx
[PASS] udp_rx
```

The QEMU test must fail on panic, double fault, triple fault/reset, malformed test completion, or missing required UDP markers.

## 19. Regression requirements

After UDP is added, all existing important paths must still pass:

- normal Linux95 QEMU boot;
- graphical desktop initialization;
- RTL8139 detection/initialization;
- Ethernet readiness;
- ARP gateway resolution;
- existing ICMP echo reply from `10.0.2.2`;
- no-network boot with missing RTL8139 non-fatal;
- Ring 3 process self-tests;
- user-fault isolation;
- missing-userspace boot path;
- preemptive scheduling/non-yielding process proof;
- `nm -u build/kernel.elf` produces no undefined symbols;
- `git diff --check` is clean.

No regression may require weakening an existing test merely to make UDP pass.

## 20. Files expected to change

The implementation is expected to touch a focused set of files such as:

```text
kernel/net/udp.hpp                    new
kernel/net/udp.cpp                    new
kernel/net/udp_bindings.hpp           new fixed binding registry
kernel/net/udp_bindings.cpp           new fixed binding registry
kernel/net/network.hpp                UDP public API/binding types
kernel/net/network.cpp                IPv4 protocol dispatch, binding table, UDP send/ARP integration
Makefile                              compile/link/test integration
tests/host/udp_test.cpp               new UDP packet/checksum tests
tests/host/udp_bindings_test.cpp      new binding/dispatch registry tests
tests/qemu_smoke.py                   real UDP QEMU proof
```

Test code should follow the existing host-test and QEMU-smoke structure; any filename change must preserve the same coverage and Makefile integration.

The milestone should avoid unrelated refactoring of Ethernet, ARP, IPv4, ICMP, process scheduling, GUI, storage, or filesystem code.

## 21. Acceptance criteria

UDP v1 is complete only when all of the following are true:

1. `udp.cpp/.hpp` provide isolated build/parse/checksum behavior.
2. Outgoing IPv4 UDP uses protocol `17` and a proper pseudo-header checksum.
3. Incoming nonzero UDP checksums are verified.
4. Incoming IPv4 UDP checksum zero is accepted.
5. Malformed UDP lengths/checksums are safely dropped.
6. Maximum UDP payload is 1472 bytes; oversized sends fail cleanly.
7. Eight fixed kernel UDP bindings are supported without dynamic allocation.
8. Duplicate bindings fail and unbound destination ports are ignored safely.
9. Incoming datagrams dispatch to the correct callback with correct endpoint metadata.
10. UDP send uses existing `ipv4::next_hop()` and ARP cache logic.
11. One unresolved-ARP UDP datagram can be queued and later transmitted after ARP resolution.
12. A second unresolved-ARP send cannot corrupt or overwrite the pending datagram.
13. Real UDP transmit and receive are proven in QEMU.
14. Existing ARP and ICMP networking remains functional.
15. Existing no-network and preemptive-scheduling test modes remain functional.
16. No panic, double fault, triple fault/reset, or unresolved kernel symbol is introduced.
17. `git diff --check` passes.
18. DNS is not coupled into the UDP transport implementation.

## 22. Future work after this milestone

Once generic UDP is merged and verified, the next networking milestone may add DNS as a UDP client, likely using:

```text
DNS resolver
   -> temporary/local UDP binding
   -> UDP destination port 53
   -> existing IPv4/ARP/Ethernet/RTL8139 stack
```

TCP remains a later and substantially larger milestone.
