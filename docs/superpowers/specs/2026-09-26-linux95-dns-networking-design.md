# Linux95 DNS Networking Design

**Date:** 2026-09-26
**Target branch:** `dns-networking`
**Repository:** `Black7i77/Linux95-Kernel`
**Status:** Draft for review; implementation planning requires explicit approval.

## 1. Purpose

Linux95 has a polling RTL8139 driver and generic Ethernet, ARP, IPv4, ICMP,
and UDP layers. This milestone adds a small kernel DNS resolver above the
existing UDP API. DNS is a consumer of UDP, not a special path through the
network stack, and its kernel API remains reusable by later kernel services.

DNS v1 resolves hostnames to IPv4 A records, supports CNAME chains, and
provides a terminal command for lookup and for configuring the DNS server.
While a terminal lookup appears to wait for its answer, the resolver is
asynchronous: network and normal kernel/desktop polling continue while the
query is pending.

## 2. Goals

1. Add `kernel/net/dns.hpp` and `kernel/net/dns.cpp` as a dedicated resolver.
2. Layer DNS queries and responses on the existing UDP bind/send/receive API.
3. Expose a reusable asynchronous lookup API with a single outstanding lookup.
4. Support IPv4 A records, up to eight distinct results, and bounded CNAME
   following.
5. Add terminal commands `dns <hostname>`, `dnsserver`, and
   `dnsserver <IPv4>`.
6. Use configurable DNS server state, defaulting to `10.0.2.3`.
7. Parse untrusted DNS packets with fixed storage, strict bounds, and bounded
   compression-pointer traversal.
8. Prove an actual lookup over RTL8139, Ethernet, ARP, IPv4, UDP, and DNS in
   QEMU.
9. Preserve existing networking, desktop, Ring 3, preemptive scheduling, and
   no-network fallback behavior.

## 3. Non-goals

DNS v1 does not add:

- DNS over TCP, including fallback for truncated UDP responses;
- AAAA records or IPv6;
- DNS caching;
- DHCP-based DNS-server discovery;
- multiple outstanding resolver lookups;
- a general socket subsystem;
- dynamic allocation for resolver state;
- DNS or general networking syscalls for Ring 3 processes.

The generic UDP layer remains protocol-agnostic. No DNS behavior is added to
the UDP codec or to the IPv4 parser.

## 4. Existing architecture to preserve

The network path is:

```text
RTL8139 -> Ethernet -> ARP
                   -> IPv4 -> ICMP
                            -> UDP -> fixed kernel port bindings
```

The desktop host polls `network::poll()` from its normal event loop. UDP
callbacks receive a borrowed payload valid only for the callback duration.
The UDP transmit API uses the existing IPv4 routing and ARP behavior,
including its bounded pending-transmit behavior. DNS must bind a UDP port
and use these APIs; it must not bypass them or add another driver, packet
path, ARP cache, or IPv4 builder.

DNS response processing and timeout progression are driven from the normal
Ring 0 polling path. Each loop polls the network first (so the UDP callback
can consume responses), then advances `dns::poll()` to handle retry and
queued CNAME follow-up sends. The resolver must not busy-wait, disable
interrupts while waiting, or monopolize the desktop loop. The PIT monotonic
tick source provides timeout measurement; each attempt deadline is two
seconds using the actual configured tick rate, not an assumed wall-clock
frequency. A timeout starts only after the network API accepts that
question's send.

## 5. Resolver architecture and public contract

Implement the resolver in:

```text
kernel/net/dns.hpp
kernel/net/dns.cpp
```

The public interface should be narrow and usable without the terminal. Its
conceptual operations are:

```cpp
enum class Status;

Status begin_lookup(const char* hostname);
void poll();
Status lookup_status();
size_t result_count();
bool result_address(size_t index, net::Ipv4Address& out);

net::Ipv4Address server();
Status set_server(const net::Ipv4Address& address);
```

Exact names/signatures may follow repository conventions, but the behavior
below is binding. `begin_lookup` returns `Busy` without changing the active
lookup if another lookup is pending. On a successful start it returns
`Pending`; the first query send is attempted immediately and subsequent
retry/CNAME send progression is driven by `poll()`. The caller observes
completion through status/results. Result
storage is owned by the resolver and remains valid until the next lookup is
started or the result is explicitly cleared. The API does not return
pointers into a received UDP frame.

If no network interface is online, startup completes as
`NetworkUnavailable`. A transport send failure also completes as
`NetworkUnavailable`; the resolver must not claim a request was sent when
`network::send_udp` rejected it. The timeout for an attempt begins once
`network::send_udp` returns true, including when UDP accepted the datagram
while ARP resolution is pending.

The resolver uses one fixed state record, fixed-size hostname/question
buffers, fixed-size packet/parser scratch storage, and an eight-address
result array. No heap allocation is used for resolver state or packet
parsing. Since this kernel is single-core, the API and UDP callback are
serialized through the existing Ring 0 polling path; no resolver lock may
be held across polling or waiting.

Recommended externally observable states are:

```text
Idle
Pending
Success
InvalidName
NotFound
ServerFailure
MalformedResponse
TruncatedResponse
CnameLoopOrLimit
TimedOut
NetworkUnavailable
Busy
```

`Busy` is a start/configuration result and need not replace the current
lookup's `Pending` state. A new lookup may start after the prior lookup has
reached a terminal state. Starting a new lookup clears the previous result
set. If no network interface is online, startup completes as
`NetworkUnavailable`. A transport send failure also completes as
`NetworkUnavailable`; the resolver must not claim a request was sent when
`network::send_udp` rejected it.

Changing the DNS server while a lookup is pending returns `Busy` and leaves
the configured server and active transaction unchanged. This prevents a
single in-flight transaction from changing its expected peer. The server
change command reports that no change was made. Server changes are accepted
between lookups.

## 6. Query format and transport

Every question is a standard DNS query with:

- `QR = 0`, `Opcode = 0`, `RD = 1`;
- `QDCOUNT = 1`, `ANCOUNT = 0`, `NSCOUNT = 0`, `ARCOUNT = 0`;
- `QTYPE = A`, `QCLASS = IN`;
- destination UDP port `53`;
- fixed client UDP port `53000`;
- a 16-bit transaction ID incremented for each transmitted attempt.

Retries and CNAME follow-up questions each receive a fresh transaction ID.
This makes delayed replies from earlier attempts fail the active-ID check.
The resolver sends no EDNS options in v1 and accepts at most a 512-byte DNS
message in one UDP datagram. A larger or otherwise invalid DNS payload is
rejected as malformed; a response with `TC = 1` is reported as
`TruncatedResponse` and is not retried over TCP.

The DNS server is initialized to IPv4 `10.0.2.3`. DNS queries use
`network::send_udp(server, 53000, 53, ...)`. The resolver registers one UDP
binding on port 53000 during initialization. If that port cannot be bound,
resolver initialization fails and lookups report `NetworkUnavailable`;
other UDP users and the network stack remain operational.

## 7. Hostname encoding and response validation

Host input is a nonempty ASCII hostname whose labels contain only letters,
digits, or hyphens, with no leading or trailing hyphen. Each label is 1
through 63 bytes and the full encoded name is at most 255 wire bytes,
including label-length octets and the terminating root label. One final dot
is accepted and normalized away for encoding; empty interior labels,
non-ASCII input, invalid characters, invalid label lengths, and names that
exceed the wire limit are rejected before sending with `InvalidName`; no
packet is transmitted. ASCII case is preserved on the wire and comparisons
are case-insensitive. Internationalized
names/IDNA conversion are not part of v1.

Before using a response, the resolver validates:

1. UDP source IPv4 equals the currently configured DNS server.
2. UDP source port is 53 and destination port is 53000.
3. The DNS header is present and the transaction ID equals the active
   question's current ID.
4. `QR = 1`, `Opcode = 0`, and `TC = 0`.
5. `QDCOUNT` is exactly one, and the decoded question name, type A, and class
   IN match the active question (name comparison is ASCII
   case-insensitive).
6. All declared question and resource-record fields fit within the DNS
   message and each record's RDATA length.

Responses that do not match the active transaction or expected peer are
ignored without changing resolver state or extending its deadline. A
matching response with invalid structure or question is completed as
`MalformedResponse`. This separates unrelated/stale datagrams from a
malformed answer to the active question.

`TC = 1` is checked after the minimum header and transaction/peer checks and
before parsing sections; a matching truncated response ends as
`TruncatedResponse`. `RCODE = 3` (NXDOMAIN), or a structurally valid answer
with neither a usable A record nor a CNAME, completes as `NotFound`.
`RCODE = 2` (SERVFAIL) and other nonzero response codes complete as
`ServerFailure`. Other response flags such as AA and RA do not determine
acceptance. UDP checksum and IP-level validation remain the responsibility
of the existing lower layers.

## 8. Compression-safe DNS name parser

All DNS names, including compressed owner names, CNAME targets, and the
question name, are decoded by one bounded parser. The parser must:

- check every label byte and pointer byte against the message length;
- reject reserved label encodings and labels longer than 63 bytes;
- enforce a maximum decoded wire name of 255 bytes;
- preserve the caller's post-name cursor correctly when a compression
  pointer is encountered;
- detect repeated pointer offsets/loops;
- cap pointer jumps at a fixed maximum (16 jumps per decoded name);
- reject any pointer target outside the DNS message.

No malformed name may cause an out-of-bounds read, unbounded traversal, or
kernel panic. Compression offsets are relative to the beginning of the DNS
message, not to the UDP payload's surrounding memory.

## 9. A records and CNAME behavior

The resolver returns up to eight distinct IPv4 addresses. Only A records
(`TYPE = A`, `CLASS = IN`, `RDLENGTH = 4`) belonging to the final name in
the active CNAME chain are collected. Duplicate addresses are ignored.
Additional-section A records are not trusted as answers. The entire DNS
message is still structurally bounds-checked, including sections not used
for address selection.

CNAME processing follows one alias chain. A CNAME target is followed
case-insensitively. Links are resolved by matching each CNAME owner to the
current name, independent of resource-record wire order. CNAME RDATA must
decode to exactly one name within its declared RDATA boundary. The resolver
records visited names in fixed storage, detects revisiting any name, and
stops safely as `CnameLoopOrLimit` on a loop. At most four CNAME links may
be followed. If the response contains a CNAME chain and final A record(s),
those final addresses complete the lookup without another question. If it
contains a CNAME but no final A record, the resolver automatically queues a
new standard A/IN question for the terminal CNAME target; `dns::poll()`
sends it on the next normal polling turn. Each follow-up is a new DNS
question with its own two-attempt timeout budget and fresh transaction IDs.
A fifth required link fails as `CnameLoopOrLimit`.

Conflicting CNAME targets for the same owner, a CNAME and A data for the
same owner, invalid CNAME RDATA, or an alias chain that cannot be resolved
unambiguously is rejected as `MalformedResponse`. If a valid response
contains more than eight unique final A records, the first eight in answer
wire order are returned; the parser continues validating the remaining
records before reporting `Success`.

## 10. Retries, timeout, and single-lookup rule

Each individual DNS question—including each CNAME follow-up—gets two
attempts total. An attempt has a two-second deadline measured from the
accepted UDP send. On the first timeout, the resolver sends exactly one
retry with a new transaction ID. On the second timeout, the lookup ends as
`TimedOut`. A CNAME follow-up starts a new question and therefore gets its
own two attempts. A valid response, terminal DNS response code, malformed
matching response, or matching truncated response ends the current
question without retry.

There is one active lookup across the resolver. A concurrent/reentrant
`begin_lookup` returns `Busy` and cannot overwrite buffers, transaction
IDs, retry counters, visited aliases, results, or deadlines belonging to
the active lookup. After completion, another lookup may begin normally.

## 11. Terminal behavior

Add these commands:

```text
dns <hostname>
dnsserver
dnsserver <IPv4>
```

`dnsserver` prints the configured server. `dnsserver <IPv4>` parses exactly
four dotted-decimal octets in the range 0 through 255 and changes the
configured server only if no lookup is pending. It does not probe
reachability; an unusable or unreachable configured address fails through
normal lookup statuses. Invalid syntax leaves the current server unchanged
and prints a concise usage/error message. The output always shows the
actual configured address. The API returns `Busy` if a lookup is pending.

`dns <hostname>` begins an asynchronous resolver lookup and displays the
result only when that lookup completes. From the user's point of view the
command remains active until success or failure and then prints a result
and returns to the normal prompt. Internally, the terminal must represent
this as pending command state rather than spin or sleep in the command
handler. While pending, the normal desktop/event loop continues to poll
network frames, advance DNS retries/timeouts, service input and other
desktop work, run the scheduler, and redraw as needed. The terminal may
decline additional command input while its lookup is pending, but it must
not stall the kernel or desktop host. A second kernel API caller still
receives `Busy`.

For success, print each returned IPv4 address (up to eight). For failure,
print a stable concise status message. At minimum distinguish:

- name not found;
- DNS server failure;
- timeout;
- malformed response;
- truncated response (TCP fallback unsupported);
- CNAME loop/limit;
- network unavailable;
- lookup already in progress/busy.

Do not print fake success from query submission; terminal success requires
the matching response to be parsed and accepted.

## 12. Error and failure behavior

Malformed packets, wrong peers, stale IDs, and invalid names must not panic
the kernel. Wrong peer and stale transaction packets are ignored. Malformed
responses matching the active peer and transaction fail that lookup as
`MalformedResponse`. Interface-down or rejected UDP sends fail safely as
`NetworkUnavailable`. Retry exhaustion is `TimedOut`. None of these
conditions changes IPv4, ARP, ICMP, or UDP global behavior.

DNS initialization failure, including inability to reserve UDP port 53000,
does not make network initialization or desktop startup fatal. The
`dnsserver` command may still show/configure the stored server address;
lookups report `NetworkUnavailable` until DNS initialization is healthy.

## 13. Testing and verification

Use strict test-driven development: for each implementation task, add and
run a failing test before the corresponding production change, observe
RED, then implement the smallest change and run the focused test GREEN.
Preserve the repository's Makefile ordering contracts and ensure the new
host test targets participate in `make test`.

Host tests must cover:

- hostname validation and wire encoding;
- query header, transaction ID, A/IN question, RD bit, and bounds;
- one and multiple A records, deduplication, and the eight-address cap;
- valid DNS compression pointers and correct cursor advancement;
- out-of-bounds, looping, reserved, and excessive-depth pointers;
- CNAME in-response chain, CNAME follow-up query, and final A records;
- four-hop limit, fifth-link rejection, and CNAME loop detection;
- exactly two attempts, per-attempt timeout, retry transaction ID, and
  final timeout;
- mismatched transaction IDs, wrong DNS source IP, wrong source UDP port,
  wrong question name/type/class, QR/opcode errors, and unrelated datagrams;
- malformed label, section, RDATA, and resource-record lengths;
- `TC = 1` truncated response behavior;
- NXDOMAIN, SERVFAIL/other error RCODEs, and valid NODATA;
- Busy behavior and DNS-server change behavior while a lookup is pending;
- no result-array overflow and fixed-buffer boundaries.

QEMU verification must issue a real lookup to `10.0.2.3` and prove a valid
answer traversed:

```text
RTL8139 -> Ethernet -> ARP -> IPv4 -> UDP -> DNS
```

The QEMU test must require a completed DNS result, not merely a transmitted
query. It should use a stable public test hostname but must not hard-code
one returned address, because DNS answers can vary. If the QEMU user-mode
network DNS forwarder is unavailable, the test must fail with useful
diagnostics rather than report success. Existing QEMU tests for ICMP, UDP,
no-network fallback, Ring 3 processes, and preemptive scheduling must
continue to pass.

Run the DNS proof as a fresh test boot with an empty ARP cache. Require
evidence that the request resolved the DNS server's link-layer next hop
through ARP, that the DNS request was accepted for transmission, and that a
matching response produced at least one parsed A result. A test-only
`[PASS] dns_lookup` marker is emitted only after that completed resolver
success; initialization, query submission, or a raw UDP callback alone is
not success evidence. Keep test diagnostics tied to the real production
send/receive path rather than adding a test-only DNS parser or network
shortcut.

## 14. Compatibility and preservation requirements

DNS v1 must preserve:

- RTL8139 operation and non-fatal no-network fallback;
- Ethernet, ARP, IPv4 routing, ICMP, and generic UDP behavior;
- desktop/window/input processing while a lookup is pending;
- Ring 3 userspace and syscall behavior;
- preemptive scheduling and host context restoration;
- the existing UDP receive callback lifetime and fixed binding capacity.

No DNS lookup may block the network polling loop or require changes to
Ring 3 process scheduling. DNS server configuration belongs to the kernel
resolver and is independent of DHCP or userspace.

## 15. Acceptance criteria

DNS v1 is ready for review when:

1. A host caller can start and poll a DNS lookup without terminal
   dependencies.
2. A second simultaneous lookup returns `Busy` without corrupting the
   active one.
3. The resolver strictly validates peer, transaction, question, flags,
   sections, names, and lengths using bounded fixed storage.
4. A and CNAME answers yield up to eight distinct IPv4 results; loops and
   more than four alias links fail safely.
5. Each DNS question gets at most two attempts with two seconds per
   attempt; timeout completion is observable.
6. `dns <hostname>`, `dnsserver`, and `dnsserver <IPv4>` work as specified
   while the desktop/network loop continues.
7. QEMU observes a completed real DNS lookup over the existing network
   path.
8. Existing networking, desktop, Ring 3, preemption, and no-network tests
   remain green, and malformed inputs do not panic the kernel.
