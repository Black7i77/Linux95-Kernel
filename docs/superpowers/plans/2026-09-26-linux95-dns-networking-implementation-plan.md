# Linux95 DNS Networking Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Every task uses strict TDD RED → GREEN and ends with a reviewable commit.

**Goal:** Add a bounded, asynchronous kernel DNS resolver over Linux95's existing UDP stack, with A/CNAME lookup and terminal configuration/lookup commands.

**Architecture:** `kernel/net/dns.cpp` owns fixed resolver state and consumes the existing UDP bind/send/receive APIs. Pure DNS name/query/response routines are host-testable; resolver state is tested with fake network and PIT providers. The terminal represents a lookup as a pending command while the existing Ring 0 event loop continues polling network, resolver, input, scheduler, and desktop work.

**Tech Stack:** Freestanding C++17, fixed-size buffers, existing `network::send_udp` and UDP port bindings, PIT ticks, host C++ tests, Python QEMU smoke harness, QEMU RTL8139 user-mode networking.

**Spec:** `docs/superpowers/specs/2026-09-26-linux95-dns-networking-design.md`

## Global Constraints

- Work only in `/tmp/linux95-dns-networking` on branch `dns-networking`; do not touch other worktrees, push, merge, publish, delete branches, or delete worktrees.
- DNS v1 supports IPv4 A records only, returns at most 8 distinct addresses, follows at most 4 CNAME links, and allows only one outstanding lookup.
- DNS server default is `10.0.2.3`; DNS uses UDP destination port 53 and fixed client port 53000.
- Each DNS question gets 2 attempts total, with a 2-second deadline per attempt; each CNAME follow-up is a new question with its own budget and fresh transaction ID.
- Use fixed buffers only for resolver state; do not add dynamic allocation, DNS caching, DHCP DNS discovery, TCP fallback, AAAA/IPv6, multiple lookups, or a general socket subsystem.
- Parse untrusted DNS messages with strict bounds and compression-pointer loop/depth protection; malformed input must not panic the kernel.
- DNS must remain layered over the existing UDP API; preserve RTL8139, Ethernet, ARP, IPv4, ICMP, UDP, desktop, Ring 3, preemptive scheduling, and no-network fallback behavior.
- Terminal `dns <hostname>` must appear synchronous to the user but stay asynchronous internally; do not block the normal network/kernel/desktop polling loop.
- Resolver initialization or lookup failure is non-fatal to normal Linux95 boot and networking.
- Use strict TDD for every task: add the focused test before production behavior, run it RED for the intended missing behavior, implement the smallest change, and rerun it GREEN.
- Preserve `test-preemption-source` as the first prerequisite/order contract of `make test`; add tests after it without changing existing ordering.
- Keep QEMU test-only DNS startup and markers out of ordinary kernel boot.

## Review Focus

- Compression pointer targets at the end of a packet, self-referential pointers, and pointer chains exceeding the fixed jump bound must fail safely — Task 1 host tests.
- A matching transaction from the configured peer with malformed question/RR/RDATA bounds must fail the active lookup, while stale IDs and wrong peers must be ignored without extending deadlines — Tasks 2–3 host tests.
- CNAME records in arbitrary wire order, repeated names, conflicting targets, a fifth link, and loops across follow-up responses must not return unrelated A records or exceed the four-link limit — Tasks 2–3 host tests.
- A busy resolver, a changing server while pending, timeout/retry boundary ticks, and transaction-ID wrap must not corrupt active lookup state — Task 3 host tests.
- Invalid/maximum-length terminal input, no-network startup, and resolver-port binding failure must leave the prompt and normal desktop/network service usable — Tasks 3–4 host tests and Task 5 QEMU regression tests.

---

## File Map

- `kernel/net/dns.hpp`, `kernel/net/dns.cpp`: public resolver/status/name API, pure DNS message functions, fixed resolver state, UDP callback, retry/CNAME state machine.
- `kernel/arch/pit.hpp`, `kernel/arch/pit.cpp`: expose the actual configured PIT tick frequency so two-second DNS deadlines are exact if PIT configuration changes.
- `kernel/kernel.cpp`: initialize DNS after the network stack; DNS failure is logged but non-fatal. Test-only DNS boot query is compile-guarded.
- `kernel/gui/desktop.cpp`: advance resolver polling alongside the normal network/event loop.
- `kernel/terminal/shell_session.hpp`, `kernel/terminal/shell_session.cpp`: DNS callbacks and pending-command lifecycle; parse `dns` and `dnsserver` commands without blocking.
- `kernel/terminal/shell.cpp`: expose DNS commands/help and poll the resolver in VGA fallback mode.
- `kernel/gui/terminal_app.cpp`: connect the terminal session to the kernel DNS API.
- `kernel/net/network.cpp`: only the minimal compile-guarded QEMU diagnostic needed to prove ARP resolution of the DNS server's next hop.
- `Makefile`: compile resolver, add host test targets, preserve current test ordering, and add a dedicated DNS QEMU test image/mode.
- `tests/host/dns_message_test.cpp`: hostname encoding, query construction, and compression-safe name decoding.
- `tests/host/dns_response_test.cpp`: bounded response, A-record, CNAME, and status parsing.
- `tests/host/dns_resolver_test.cpp`: async state machine, fake transport/PIT, retry and peer validation.
- `tests/host/shell_session_test.cpp`: DNS command dispatch, pending prompt behavior, result formatting, and server configuration.
- `tests/qemu_smoke.py`: real DNS self-test mode and assertions; existing modes remain unchanged.
- `tests/source_checks.py`: enforce normal-boot isolation and key DNS safety/integration properties.

## Shared Interfaces

These names/types are the plan contract for the implementation tasks; internal helper names may differ when they do not cross task boundaries.

In `kernel/net/dns.hpp`:

```cpp
enum class Status {
    Idle,
    Pending,
    Success,
    InvalidName,
    NotFound,
    ServerFailure,
    MalformedResponse,
    TruncatedResponse,
    CnameLoopOrLimit,
    TimedOut,
    NetworkUnavailable,
    Busy,
};

struct Name {
    uint8_t wire[255];
    uint16_t length;
};

enum class ParseDisposition {
    Ignored,
    Complete,
    Followup,
};

struct ParsedResponse {
    ParseDisposition disposition;
    Status status;
    Name followup_name;
    net::Ipv4Address addresses[8];
    uint8_t address_count;
    Name visited_names[5];
    uint8_t visited_count;
    uint8_t cname_hops;
};

bool encode_hostname(const char* hostname, Name& out);
bool names_equal(const Name& left, const Name& right);
bool decode_name(const uint8_t* message, size_t length,
                 size_t& cursor, Name& out);
bool build_query(uint16_t transaction_id, const Name& question,
                 uint8_t* output, size_t capacity, size_t& output_length);

ParsedResponse parse_response(const uint8_t* message, size_t length,
                              uint16_t expected_id,
                              const Name& expected_question,
                              const Name* visited_names,
                              size_t visited_count);

bool initialize();
Status begin_lookup(const char* hostname);
void poll();
Status lookup_status();
size_t result_count();
bool result_address(size_t index, net::Ipv4Address& out);
net::Ipv4Address server();
Status set_server(const net::Ipv4Address& address);
```

`parse_response` is pure and receives DNS payload only; its caller validates UDP peer/ports first. `Ignored` means the transaction ID did not match and carries `Idle`; `Complete` carries a terminal resolver status (`Success`, `NotFound`, `ServerFailure`, `MalformedResponse`, or `TruncatedResponse`); `Followup` carries `Pending`, `followup_name`, and updated cumulative `visited_names`/`cname_hops`. A question mismatch for a matching ID is `Complete` + `MalformedResponse`. The parser carries up to five visited names (initial query name plus four links) so loops spanning separate response packets are detected. Public result addresses are copied out; no API exposes borrowed receive-buffer memory.

The `Name` representation is DNS wire-format labels including the final zero root label, with `length` in `[1, 255]`. Hostname equality is ASCII case-insensitive over label bytes. Compression parsing limits are fixed by the spec: decoded wire length ≤255 and at most 16 pointer jumps per name.

The resolver stores at most eight final unique A records. `begin_lookup` returns `Busy` without altering active state, `InvalidName` without transmitting for invalid input, `NetworkUnavailable` if DNS/network setup cannot send, otherwise `Pending`. Attempts use incrementing `uint16_t` transaction IDs; wraparound is defined modulo 2^16.

## Task 1: DNS Hostname, Query, and Compression-Safe Name Codec

**Files:** Create `kernel/net/dns.hpp`, `kernel/net/dns.cpp`, `tests/host/dns_message_test.cpp`; modify `Makefile`.

**Produces:** `dns::Name`, `dns::encode_hostname`, `dns::names_equal`, `dns::decode_name`, `dns::build_query`, and the shared `dns::Status` declaration.

- [ ] Add the host test source and a Makefile target `build/host-dns-message-test`; add its runner to `make test` without moving `test-preemption-source` from first position.
- [ ] Cover valid normal/trailing-dot hostnames, case-preserving wire encoding, empty/invalid/non-ASCII/overlong labels, total wire length, and output bounds.
- [ ] Cover exact DNS query header (`RD=1`, `QDCOUNT=1`, all other counts zero), transaction ID, A/IN question, root terminator, and insufficient output capacity.
- [ ] Cover uncompressed names, valid compressed name cursor advancement, pointer target at packet end, out-of-bounds pointer, self-loop, multi-pointer loop, reserved label encodings, >16 jumps, truncated labels, and >255 decoded bytes.
- [ ] Run `make build/host-dns-message-test && ./build/host-dns-message-test` before production functions exist. Expected RED: compile failure because the DNS codec API is missing, not because the target/test fixture is absent.
- [ ] Implement only the fixed `Name` codec/query builder in `kernel/net/dns.cpp`; do not add resolver/network behavior in this task.
- [ ] Rerun `make build/host-dns-message-test && ./build/host-dns-message-test`; expected GREEN for all codec cases.
- [ ] Run `make test` and verify the preemption source check remains first and all existing tests remain green.
- [ ] Commit as `Add DNS name and query codec`.

## Task 2: Strict DNS Response Parsing and A/CNAME Semantics

**Files:** Modify `kernel/net/dns.hpp`, `kernel/net/dns.cpp`, `tests/host/dns_response_test.cpp`, and `Makefile`.

**Consumes:** Task 1 `Name`, `decode_name`, and case-insensitive comparison.

**Produces:** `dns::ParsedResponse` and pure `dns::parse_response(...)` with no network or clock dependencies.

- [ ] Write response fixtures and host tests before parser implementation. Cover valid A answer, several A records, duplicate elimination, first-eight cap while validating remaining records, and A owner selection for the queried/final canonical name.
- [ ] Cover in-message CNAME chains independent of RR wire order, CNAME with no final A returning `Pending` + follow-up target, and final A reached inside the same response.
- [ ] Cover prior-name loop, in-message loop, four-link success, fifth-link failure, conflicting CNAME owner targets, CNAME+A conflict, malformed CNAME RDATA, wrong question name/type/class, wrong QR/opcode, NXDOMAIN, SERVFAIL/other RCODE, and valid NODATA.
- [ ] Cover section counts, compressed owner/question/RDATA names, RDATA length boundaries, truncated fixed RR fields, TC=1, and structurally invalid authority/additional records.
- [ ] Run `make build/host-dns-response-test && ./build/host-dns-response-test` before response parsing exists. Expected RED: the missing `parse_response` API/behavior is identified by compile/runtime assertions.
- [ ] Implement a bounded parser over at most 512 DNS bytes. Parse all declared sections safely; only select A records from the answer section and only for the terminal name in the CNAME chain. Preserve ignored-vs-malformed response distinction required by the shared interface.
- [ ] Rerun the focused response test; expected GREEN, including all malformed packet cases.
- [ ] Run both DNS host targets and `make test`.
- [ ] Commit as `Parse DNS responses and CNAME records`.

## Task 3: Asynchronous Resolver State, Retries, and Server Configuration

**Files:** Modify `kernel/net/dns.hpp`, `kernel/net/dns.cpp`, `kernel/arch/pit.hpp`, `kernel/arch/pit.cpp`; create `tests/host/dns_resolver_test.cpp`; modify `Makefile`.

**Consumes:** Task 1 codec and Task 2 response parser; existing `network::bind_udp_port`, `network::send_udp`, `network::status`, and UDP callback lifetime.

**Produces:** `initialize`, `begin_lookup`, `poll`, result/status accessors, `server`, and `set_server` from the shared interface. Add `pit::ticks_per_second()` returning the actual configured PIT frequency for exact deadlines.

- [ ] Create host tests with fake network providers (capturing the registered callback and sent datagrams) and a fake PIT tick source before implementing resolver state.
- [ ] Test default server `10.0.2.3`, port registration at 53000, queries to server port 53, incrementing transaction IDs, one active lookup, `Busy`, invalid hostname/no transmission, result lifetime/reset, and server-change Busy/accepted behavior.
- [ ] Test exact first attempt/retry timing at 2 seconds, only one retry, final timeout at the second deadline, fresh transaction ID on retry, wraparound, CNAME follow-up with a fresh per-question attempt budget, and no deadline extension for ignored packets.
- [ ] Test source IP, source port, destination port, transaction ID, question, and DNS status mapping; wrong peer/stale ID are ignored, matching malformed/truncated/NXDOMAIN/SERVFAIL complete appropriately.
- [ ] Test offline interface, UDP bind failure, initial send failure, and retry-send failure all complete safely as `NetworkUnavailable` without corrupting resolver state.
- [ ] Run `make build/host-dns-resolver-test && ./build/host-dns-resolver-test` before resolver implementation. Expected RED: resolver APIs/state behavior are missing while fake dependencies allow the test to link once declarations are introduced.
- [ ] Implement a fixed resolver record and callback over the existing UDP API. `begin_lookup` sends attempt one; `poll` advances retry/queued-follow-up work; callback validates peer/ports before calling the pure parser. Use PIT ticks and actual tick rate, no heap or blocking wait.
- [ ] Implement non-fatal initialization and server configuration; do not change network initialization semantics.
- [ ] Rerun resolver test; expected GREEN for busy, peer validation, CNAME, retry, timeout, and offline cases.
- [ ] Run all three DNS host tests and `make test`.
- [ ] Commit as `Add asynchronous kernel DNS resolver`.

## Task 4: Kernel/Terminal Integration Without Blocking the Event Loop

**Files:** Modify `kernel/kernel.cpp`, `kernel/gui/desktop.cpp`, `kernel/gui/terminal_app.cpp`, `kernel/terminal/shell.cpp`, `kernel/terminal/shell_session.hpp`, `kernel/terminal/shell_session.cpp`, `tests/host/shell_session_test.cpp`, and `Makefile` as needed.

**Consumes:** Task 3 DNS initialize/poll/status/result/server APIs.

**Produces:** boot-integrated non-fatal resolver setup; terminal commands `dns <hostname>`, `dnsserver`, and `dnsserver <IPv4>`; asynchronous pending-command UX.

- [ ] Extend shell-session tests first with fake DNS callbacks. Cover command recognition/usage, server display/update, invalid IPv4 unchanged, server-change Busy, DNS begin Busy/InvalidName/offline, and formatting of one/multiple results and every terminal status.
- [ ] Assert `dns <hostname>` does not immediately print success or return a prompt while pending; `ShellSession::poll()` continues pending state, prints the terminal result once, then restores the prompt. Ignore/decline new command input during the pending command without losing kernel polling.
- [ ] Add an input-boundary test for a valid maximum-length DNS presentation name. Set command capacity to at least 260 bytes (including `dns ` prefix and NUL) so the terminal does not impose a shorter limit than the resolver.
- [ ] Run `make build/host-shell-session-test && ./build/host-shell-session-test` before terminal integration. Expected RED: DNS callbacks/command state and pending prompt behavior are missing.
- [ ] Add DNS callbacks to `ShellSession` and wire them in both graphical `TerminalApp` and VGA fallback shell. Add help text and exact DNS status output.
- [ ] Initialize DNS after `network::initialize()` and treat failure as non-fatal. Keep the stored default/configurable server visible even when network/DNS is unavailable.
- [ ] Advance `dns::poll()` after `network::poll()` in the desktop and VGA loops; ensure each loop continues normal input, scheduler, redraw, and network service while DNS is pending.
- [ ] Rerun focused shell tests GREEN; run `make test` and source checks.
- [ ] Commit as `Integrate DNS with terminal and kernel polling`.

## Task 5: Real QEMU DNS Proof and Full Regression Matrix

**Files:** Modify `kernel/kernel.cpp`, `kernel/net/network.cpp`, `kernel/net/dns.cpp`, `Makefile`, `tests/qemu_smoke.py`, and `tests/source_checks.py`.

**Consumes:** Complete resolver and terminal integration from Tasks 1–4.

**Produces:** Dedicated `--dns-network-test` QEMU mode proving a real A lookup through the existing stack; production boot remains free of DNS self-test traffic.

- [ ] Extend `tests/qemu_smoke.py` first with a mutually exclusive `--dns-network-test` mode. Initially boot the existing network-test image and require an actual query-accepted diagnostic, cold-cache ARP resolution of `10.0.2.3`, and `[PASS] dns_lookup` only after a matching response parses to at least one A record. Do not require a fixed returned IP.
- [ ] Run `make build/linux95-kernel-network-test.img && python3 tests/qemu_smoke.py --dns-network-test` before adding DNS auto-query or a DNS test image. Expected RED: the existing network-only image boots but fails specifically because the required DNS markers/completed lookup are absent.
- [ ] Add a dedicated DNS test kernel/image Makefile variant using `LINUX95_QEMU_DNS_SELF_TEST`; only that build may automatically call `begin_lookup("example.com")`. Add a source check proving ordinary kernel boot does not auto-start DNS.
- [ ] Add test-only diagnostics tied to real events: query marker after UDP accepts the send, ARP marker only after cold-cache resolution of the DNS server next hop transmits the pending UDP packet, and success marker only after the matching DNS callback accepts a valid response with at least one address. Do not add alternate/test-only packet parsing or transport.
- [ ] Give the DNS QEMU mode a bounded deadline long enough for retries, require completed success rather than query submission, retain useful log output on timeout, and do not weaken normal QEMU mode expectations.
- [ ] Add `python3 tests/qemu_smoke.py --dns-network-test` to `make test-qemu`, preserving existing normal-network, UDP, no-network, and preemption coverage/order.
- [ ] Run focused `python3 tests/qemu_smoke.py --dns-network-test`; expected GREEN with RTL8139, ARP, UDP query, and real answer evidence.
- [ ] Run clean complete verification: `make clean && make all && make test && make test-qemu`, then `python3 tests/qemu_smoke.py --dns-network-test`, `python3 tests/qemu_smoke.py --process-self-test`, `python3 tests/qemu_smoke.py --process-fault-test`, `python3 tests/qemu_smoke.py --without-user-programs`, and `python3 tests/qemu_smoke.py --process-preemption-test`.
- [ ] Run `nm -u build/kernel.elf` (must be empty), `git diff --check` (must be clean), and `git status --short --branch` (only intended DNS milestone changes; preserve pre-existing untracked files).
- [ ] Commit as `Verify real DNS lookup over RTL8139`.

## Final Review Checklist

- Confirm each spec goal/non-goal maps to the tasks above; in particular DNS remains independent of DNS-specific behavior in UDP, and no TCP/AAAA/cache/DHCP/multi-lookup/socket work slipped in.
- Confirm every implementation task has a genuine focused RED before production implementation and a focused GREEN afterward.
- Confirm retry and CNAME follow-up use fresh transaction IDs and per-question budgets, while total CNAME links and result count remain bounded.
- Confirm terminal waiting is represented as pending state and does not block normal desktop/network/preemption service.
- Confirm test-only DNS autostart/markers are absent from ordinary `build/kernel.elf` boot behavior.
- Confirm `make test` retains `test-preemption-source` as its first prerequisite and every new host test is included.
- Inspect QEMU debug log: ARP resolution belongs to fresh DNS traffic, DNS success follows a real parsed response, and no panic/reset/network regression occurred.
- Re-run the entire clean verification matrix if any review fix changes code.
