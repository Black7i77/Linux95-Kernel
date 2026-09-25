# Linux95 RTL8139 Networking Design

Date: 2026-09-25
Status: Approved design
Branch: v1.0-dev

## Goal

Add the first real networking subsystem to Linux95 using QEMU's emulated RTL8139 PCI Ethernet adapter.

The first networking milestone is intentionally small:

- PCI device discovery
- RTL8139 Ethernet driver
- Ethernet frame handling
- ARP
- IPv4
- ICMP Echo
- static IPv4 configuration
- terminal commands:
  - `ip`
  - `ping 10.0.2.2`

The milestone is complete when Linux95 can send a real ICMP Echo Request through QEMU's RTL8139 device and receive an Echo Reply from QEMU user-mode networking.

## Network Environment

QEMU will use user-mode networking with an RTL8139 device.

Initial static configuration:

- interface: rtl8139
- IPv4 address: 10.0.2.15
- netmask: 255.255.255.0
- gateway: 10.0.2.2
- NIC mode: polling
- MTU target: standard Ethernet 1500-byte payload

Expected QEMU networking arguments:

    -netdev user,id=net0
    -device rtl8139,netdev=net0

No host TAP or bridge configuration is required for this milestone.

## Architecture

Networking is divided into small layers:

    PCI
      |
      v
    RTL8139
      |
      v
    Ethernet
      |
      +---- ARP
      |
      +---- IPv4
              |
              v
             ICMP

Each layer owns one responsibility and communicates through narrow interfaces.

Expected source layout:

    kernel/
      pci/
        pci.cpp
        pci.hpp

      drivers/
        rtl8139.cpp
        rtl8139.hpp

      net/
        net_types.hpp
        ethernet.cpp
        ethernet.hpp
        arp.cpp
        arp.hpp
        ipv4.cpp
        ipv4.hpp
        icmp.cpp
        icmp.hpp

## PCI Layer

The PCI layer will provide only the functionality required by the first driver.

Responsibilities:

- PCI configuration-space reads and writes
- scan bus/device/function combinations
- locate devices by vendor/device information or class
- read BAR values
- expose command-register control required by RTL8139
- enable PCI I/O-space access
- enable bus mastering when required

The first version is not intended to be a complete PCI framework.

The PCI code must remain reusable by future drivers.

## RTL8139 Driver

The RTL8139 driver will:

- detect the QEMU RTL8139 PCI device
- determine its I/O base address
- enable PCI access
- reset the NIC
- read its hardware MAC address
- configure a receive buffer
- initialize transmit descriptors/registers
- transmit complete Ethernet frames
- poll the receive ring for frames
- acknowledge handled RTL8139 status conditions as required

The first driver version uses polling only.

No RTL8139 IRQ handler is included in this milestone.

Networking must not require NIC interrupts for `ping` to work.

## Ethernet Layer

The Ethernet layer will:

- represent MAC addresses
- parse Ethernet II headers
- validate minimum frame/header lengths before access
- inspect EtherType
- dispatch ARP frames to ARP
- dispatch IPv4 frames to IPv4
- construct outgoing Ethernet II frames
- reject frames that are malformed or irrelevant

Initially supported EtherTypes:

- ARP: 0x0806
- IPv4: 0x0800

## ARP Layer

ARP will provide enough address resolution for the static IPv4 environment.

Responsibilities:

- construct ARP requests
- receive and validate ARP packets
- answer ARP requests for 10.0.2.15
- resolve 10.0.2.2 to its Ethernet MAC address
- maintain a small fixed-size ARP cache
- expose lookup/resolution status to IPv4/ICMP

ARP cache storage should be fixed-size for the initial implementation.

Dynamic allocation is not required.

## IPv4 Layer

IPv4 will:

- represent IPv4 addresses
- build minimal IPv4 headers
- parse incoming IPv4 packets
- validate header lengths before reading fields
- support IPv4 header checksum generation
- validate incoming IPv4 header checksums where applicable
- reject fragmented packets in the first version
- dispatch protocol 1 packets to ICMP
- route traffic for off-subnet destinations through 10.0.2.2

The initial stack does not implement general IP forwarding or routing tables.

It uses the fixed interface configuration defined by this document.

## ICMP Layer

ICMP will implement Echo Request and Echo Reply.

Responsibilities:

- construct ICMP Echo Request packets
- calculate ICMP checksum
- identify Echo Reply packets
- track identifier and sequence number
- expose reply state to the terminal command
- support timeout reporting

Initial user-visible behavior:

    Linux95> ping 10.0.2.2
    64 bytes from 10.0.2.2: icmp_seq=1

If a reply does not arrive:

    Request timeout for 10.0.2.2

If ARP resolution cannot complete:

    ping: host unreachable

If networking is unavailable:

    ping: network unavailable

## Runtime Integration

Networking is an optional subsystem.

Failure to initialize networking must not prevent Linux95 from reaching the graphical desktop or VGA fallback shell.

Example startup markers:

    [PASS] pci_bus_ready
    [PASS] rtl8139_detected
    [PASS] rtl8139_initialized
    [PASS] ethernet_ready
    [PASS] arp_ready
    [PASS] ipv4_ready
    [PASS] icmp_ready

Failure examples:

    [WARN] pci_no_rtl8139
    [WARN] network_offline

The main runtime will periodically call:

    network::poll();

`network::poll()` will:

1. ask RTL8139 for received frames
2. pass complete frames to Ethernet
3. dispatch ARP or IPv4
4. allow ICMP reply state to advance

Polling must be bounded so networking cannot monopolize the desktop loop.

## Terminal Integration

Two commands are added in this milestone.

### ip

Example:

    Linux95> ip
    interface: rtl8139
    mac: 52:54:00:12:34:56
    ip: 10.0.2.15
    netmask: 255.255.255.0
    gateway: 10.0.2.2
    link: up

If the NIC is unavailable:

    interface: offline

### ping

Initial supported form:

    ping <IPv4 address>

Example:

    Linux95> ping 10.0.2.2

DNS names are not supported in this milestone.

The command operates asynchronously with the polling network stack rather than blocking the entire desktop event loop for long periods.

## Buffering and Memory

The first implementation will prefer fixed-size buffers.

Goals:

- avoid heap allocation in packet receive/transmit hot paths
- cap Ethernet frame sizes
- validate all received lengths before parsing
- prevent malformed packets from causing out-of-bounds reads
- keep ownership rules explicit

The RTL8139 receive buffer may require physically suitable memory and address translation consistent with the existing Linux95 memory subsystem.

The implementation plan must verify the exact DMA/address requirements before driver code is written.

## Error Handling

Malformed or unsupported network packets are dropped safely.

Networking errors do not panic the kernel unless an internal kernel invariant is violated.

Expected recoverable conditions include:

- RTL8139 not present
- malformed Ethernet frame
- malformed ARP
- malformed IPv4
- bad IPv4 checksum
- unsupported fragmentation
- unsupported IP protocol
- ARP timeout
- ICMP timeout
- receive buffer exhaustion

These conditions should fail locally and leave the desktop operational.

## Testing Strategy

Development follows TDD.

Host-side tests will cover logic that does not require real hardware.

Planned test areas:

### PCI Helpers

- configuration-address construction
- BAR interpretation helpers
- vendor/device matching helpers

### RTL8139 Helpers

- register-related helper logic
- receive-ring offset/wrap calculations
- frame length validation
- transmit-slot selection logic

Hardware register I/O itself is covered by QEMU integration rather than host unit tests.

### Ethernet

- valid frame parsing
- short frame rejection
- EtherType parsing
- outgoing frame construction
- MAC helpers

### ARP

- request construction
- reply parsing
- malformed packet rejection
- cache insert/update/lookup

### IPv4

- header construction
- checksum generation
- checksum validation
- malformed IHL/length rejection
- protocol dispatch helpers

### ICMP

- Echo Request construction
- checksum generation
- Echo Reply matching
- identifier/sequence handling

### QEMU Integration

QEMU tests will boot with:

    -netdev user,id=net0
    -device rtl8139,netdev=net0

Integration testing must prove:

- PCI finds the device
- RTL8139 initializes
- networking reaches ready state
- Linux95 transmits an ARP request
- Linux95 receives the ARP response
- Linux95 transmits ICMP Echo Request
- Linux95 receives a valid ICMP Echo Reply

The final integration target is a real successful ping to:

    10.0.2.2

## Out of Scope

The following are explicitly deferred:

- DHCP
- DNS
- UDP application API
- TCP
- sockets/POSIX socket API
- TLS
- HTTPS
- HTTP client
- Wolf Browser implementation
- Wi-Fi
- WPA2/WPA3
- USB networking
- RTL8139 IRQ mode
- IPv6
- IP fragmentation/reassembly
- general routing tables

These may be added in later networking phases.

## Future Direction

After this milestone is stable, planned follow-up phases are:

1. DHCP and DNS
2. TCP
3. HTTP
4. native Linux95 Wolf Browser
5. real hardware networking
6. Wi-Fi subsystem and terminal Wi-Fi commands

Wolf Browser will be built on top of the networking stack rather than embedding networking logic directly into the browser.

## Definition of Done

This phase is complete only when:

1. Linux95 boots normally with QEMU RTL8139 enabled.
2. PCI discovers the NIC.
3. RTL8139 initializes and reports its MAC address.
4. Ethernet transmit and receive work.
5. ARP resolves 10.0.2.2.
6. IPv4 packets can be sent and received.
7. ICMP Echo Request is transmitted.
8. ICMP Echo Reply is received and validated.
9. `ip` reports the static configuration.
10. `ping 10.0.2.2` reports a real reply.
11. existing Linux95 tests still pass.
12. networking failure does not prevent the desktop from booting.
