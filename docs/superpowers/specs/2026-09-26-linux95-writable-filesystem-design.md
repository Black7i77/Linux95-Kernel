# Linux95 Writable FAT32 Filesystem v1 Design

## Status and goal

This specification defines a first usable writable filesystem milestone for
Linux95. It adds basic mutations to the existing FAT32 filesystem through
the writable Test/slave disk while retaining the current FAT32 read path,
VFS read APIs, desktop, networking, Ring 3, and preemptive-scheduling
behavior. The writable VFS and filesystem operations are reusable by a
future Nano-style editor; implementing that editor is not part of this
milestone.

This is an architectural design awaiting implementation planning and user
approval. It does not authorize production-code implementation.

## Existing Linux95 architecture

The current filesystem backend is `kernel/filesystem/fat32.cpp` with public
declarations in `fat32.hpp`; BPB, cluster, and directory-entry helpers live
in `fat32_helpers.hpp`. `filesystem.cpp/.hpp` forwards read operations to
the FAT32 backend. `vfs.cpp/.hpp` provides bounded read-only file descriptors
and directory handles, with a path capacity of 128 bytes. The terminal's
`shell.cpp` routes `ls` and `cat` through VFS. Its command parser separates a
command word from the remaining argument string, and `ShellSession` stores
at most 259 command bytes plus the terminator.

The current FAT32 mount is explicitly performed against
`storage::DiskId::Test`, which maps to the ATA slave. The Boot/master disk is
`storage::DiskId::Boot`. `storage::disk_write_allowed()` returns true only
for `DiskId::Test`, and `storage::write_sector()` rejects Boot writes before
calling ATA. The existing mount/read implementation uses Test-disk reads.
The current chain reader uses the first FAT's location; writable FAT32 must
retain validated BPB geometry and use a consistent FAT-copy policy for both
read and mutation operations.

## Architecture and ownership

The data path is:

```text
Terminal command
    -> writable VFS operation
        -> filesystem operation
            -> FAT32 mutation backend
                -> storage::write_sector(DiskId::Test, ...)
                    -> ATA slave
```

The shell must never construct or modify FAT entries, clusters, directory
sectors, or raw LBAs. VFS exposes operation-oriented functions and forwards
to the filesystem facade. The filesystem facade forwards to the existing
FAT32 implementation plus a focused FAT32 mutation module. The preferred
module is `kernel/filesystem/fat32_write.cpp/.hpp`; it may share private
helpers or retained geometry with `fat32.cpp`, but it must not become a
second filesystem implementation or duplicate the read-only lookup/data
path unnecessarily.

The mutation module owns low-level FAT32 mutation: free-cluster discovery,
allocation, chain link/EOC/free operations, directory-slot lookup and
growth, short-entry create/update/delete, directory initialization,
file-chain data writes, and operation-specific cleanup/rollback. Public VFS
and filesystem headers must not expose cluster numbers, FAT entry values,
LBAs, or directory-sector offsets. Existing read API names and behavior
remain compatible.

All chain and directory traversals are bounded by the mounted volume's
validated cluster count. FAT links outside the data-cluster range, loops,
reserved/bad values in a live chain, invalid directory first clusters, and
other malformed metadata return `Corrupt`; no malformed chain may cause an
unbounded walk.

## Disk safety and FAT geometry

Every mutation is implicitly and permanently bound to
`storage::DiskId::Test`. No public filesystem or VFS mutation accepts a disk
selector. The FAT32 mutation module must use `storage::write_sector` with
`DiskId::Test` only; it must not write through the Boot ID, ATA drive
selection, or an unvalidated generic device path. The existing storage
boundary remains defense in depth: `write_sector(Boot, ...)` is rejected,
and this guard must remain directly tested. Existing Boot/master reads and
all normal boot behavior remain unchanged.

Mount retains validated geometry from the Test BPB and makes the same
geometry available to the reader and writer: FAT begin, FAT count, sectors
per FAT, mirroring/active-FAT flags, first data sector, cluster count,
sectors per cluster, and root cluster. Reject invalid or out-of-range BPB
geometry before exposing the volume as mounted. If mirroring is disabled,
the reader follows the BPB-selected active FAT. For writable v1, require
BPB mirroring enabled; reject mutation with `Unsupported` when a mounted
volume disables mirroring, because updating inactive FAT copies would
violate FAT32 active-FAT semantics. On supported writable volumes, every
FAT copy reported by the BPB is active and each FAT mutation must update all
copies. Do not report success if copies intentionally diverge. If one copy
write fails after another succeeded, return `IoError` and make a bounded
best-effort rollback/repair of copies whose outcomes are known. Stop
dependent publication if consistency cannot be established. Device failure
may still leave copies divergent; v1 does not promise repair after arbitrary
hardware failure.

Use checked geometry arithmetic for FAT and cluster LBAs, FAT entry offsets,
byte counts, cluster counts, and file sizes before reads, writes, allocation,
or narrowing. Reject corrupt/unsupported geometry rather than wrapping.
FSInfo is not required as an allocation hint: free clusters may be found by
a bounded FAT scan. If FSInfo is encountered, v1 does not depend on or
update its hints; FAT contents remain authoritative.

## Path and 8.3 name rules

Paths are volume-rooted and never relative to a current working directory;
there is no `cd`. To match existing path handling and command examples, an
optional leading slash is accepted, but both `DOCS/NOTES.TXT` and
`/DOCS/NOTES.TXT` resolve from the Test volume root. Paths cannot select a
volume or escape it. Empty paths, empty components, repeated separators,
trailing separators (except the root path for read-only directory access),
and components `.` or `..` are invalid for mutation. On-disk `.` and `..`
entries are still created and retained where FAT directory structure
requires them.

Each user component is an ASCII FAT short name: a base of 1-8 bytes and an
optional extension of 1-3 bytes separated by at most one dot. Names accept
ASCII lowercase, uppercase, or mixed case. Comparison is
case-insensitive using the existing ASCII short-name comparison convention;
new entries are canonicalized to uppercase on disk, with the base padded
to 8 bytes and extension padded to 3 bytes using spaces. Do not add LFN,
Unicode normalization, or a new locale-dependent case-folding system.

Reject an empty base, an empty extension after a dot, multiple dots,
overlong base/extension, non-ASCII/control/space bytes, path separators in
a component, and FAT-forbidden short-name characters. The accepted
non-alphanumeric punctuation bytes are `$`, `%`, `'`, `-`, `_`, `@`, `~`,
backtick, `!`, `(`, `)`, `{`, `}`, `^`, `#`, and `&`. The forbidden set is
`" * + , / : ; < = > ? [ \\ ] |`; dot is only the single base/extension
separator. No other punctuation is accepted. Invalid components return
`InvalidName` before any sector mutation.

The maximum complete API path is 127 bytes plus NUL, matching the existing
`vfs::kPathCapacity == 128`. Each canonical displayed short name fits the
existing 13-byte `Entry::name` storage. The existing shell command buffer
allows at most 259 input bytes including command and data; longer input is
not accepted by the shell. Filesystem/VFS byte APIs are not restricted to
terminal command length.

## Public operation semantics

The filesystem API and VFS API provide equivalent operation-oriented
functions. Names can follow repository conventions, with these semantic
forms:

```cpp
Status touch(const char* path);
Status write_file(const char* path, const uint8_t* data, size_t size);
Status mkdir(const char* path);
Status remove(const char* path);
Status copy_file(const char* source, const char* destination);
Status move(const char* source, const char* destination);
```

The VFS exports the same operations and forwards to the filesystem layer.
The arbitrary-byte write API accepts any byte sequence, including NUL and
non-text bytes. A null data pointer is valid only when size is zero. The
filesystem supports the FAT32 32-bit file-size limit and must reject larger
requests with `Unsupported` before allocation or I/O. VFS operations do
not change or weaken existing read-only descriptors, offsets, or directory
handles.

### `touch`

`touch FILE.TXT` creates a zero-length regular file if the final path is
absent. If it already names a regular file, return `Ok` and leave its bytes,
size, cluster chain, and directory metadata unchanged. If it names a
directory, return `IsDirectory`. A missing parent returns `NotFound` or
`NotDirectory` as appropriate; do not implicitly create parents.

### `write`

`write_file(path, data, size)` replaces the entire regular file, creating
it if absent. It supports zero-length files, arbitrary bytes, multi-cluster
files, growth, and shrink/replacement. Directories are rejected with
`IsDirectory`. Terminal `write PATH DATA` is a text-friendly wrapper over
this API; it does not constrain the filesystem API to text.

Use copy-on-write ordering: validate path/name/parent and size, allocate a
new chain, write all new data and complete FAT links, publish/update the
directory entry with the checked first-cluster and 32-bit size, and free the
old chain last. For a zero-length replacement, publish size and first
cluster zero before freeing the previous chain. If failure occurs before
publication, leave the old entry/chain authoritative and reclaim the new
chain best-effort. A cleanup failure returns `IoError`. If freeing the old
chain fails after publication, keep the new version authoritative and
return `IoError`; do not restore an uncertain old chain.

### `mkdir`

`mkdir PATH` requires an existing directory parent and an absent target. It
allocates a directory cluster, clears every sector, writes valid `.` and
`..` entries (with `..` referring to the parent, root according to FAT32
root conventions), then publishes the parent entry last. If preparation
fails, clean the unpublished cluster/chain best-effort. If parent
publication fails, return `IoError` and clean up only where write outcomes
are known. Existing destinations return `AlreadyExists`; do not overwrite
or merge.

If adding a directory entry requires extending a full directory chain,
allocate and clear the new cluster completely before linking it into the
existing chain or making any entry visible. Establish its EOC state before
linking; only after linking succeeds may the new slot be used. If a
pre-link step fails, reclaim the unpublished cluster best-effort. If entry
publication later fails after linking, attempt bounded best-effort unlink
and reclamation only where link/write outcomes are known. Never expose
stale entries from a reused cluster.

### `rm`

`remove(path)` removes regular files and empty directories only. A
directory is empty when it has no live entries other than `.` and `..`.
Reject non-empty directories with `DirectoryNotEmpty`; reject root, `.`,
and `..` with `Unsupported`/`InvalidName` as appropriate. No recursive
deletion. Mark/delete the parent entry before freeing the object's chain.
If entry deletion fails, do not free data. If chain freeing fails after
deletion, return `IoError`; do not resurrect metadata pointing at partially
freed clusters.

### `cp`

`copy_file(source, destination)` copies regular files and arbitrary bytes,
including multi-cluster files. Directories and recursive copies are
unsupported. Destination parent must exist and destination must be absent;
an existing destination returns `AlreadyExists`, never overwrite. Allocate
an unpublished destination chain and stream source bytes into it using
bounded buffers. Complete data and FAT links before publishing the
destination entry. Any pre-publication failure leaves the destination
absent and frees newly allocated clusters best-effort; the source remains
unchanged.

### `mv`

`move(source, destination)` operates only within the Test volume. It moves
files between directories or renames files in place. A directory may be
renamed only within its current parent; moving it to another parent returns
`Unsupported` in v1. Destination must be absent; identical source and
destination paths return `AlreadyExists`, not success or a topology error.
No cross-volume move or overwrite is supported. Reject moving root, `.`,
or `..`, and reject any self/invalid topology with `Unsupported` before
metadata mutation. Do not allow a directory rename operation to alter its
parent relation.

Moves update directory metadata only and never rewrite or free object data.
For a file moved between parents, write the destination entry before
deleting the source entry; if a later write fails, make bounded best-effort
rollback of known metadata changes. For same-parent file or directory
rename, update the existing short entry's name in place where possible.
Preserve cluster, size, and unrelated entry fields. Since v1 disallows
cross-parent directory movement, a directory's `..` entry remains
unchanged. Rollback failure returns `IoError`; v1 cannot guarantee recovery
from torn sector writes or power loss.

## Terminal command behavior

The commands are `touch PATH`, `mkdir PATH`, `rm PATH`,
`cp SOURCE DESTINATION`, `mv SOURCE DESTINATION`, and
`write PATH DATA`. Every command automatically uses the Test volume; there
is no disk argument. Paths are root-based as described above. No recursive
or force-overwrite flags are accepted.

The shell parser recognizes the command first. Single-path commands require
exactly one non-empty path argument; `cp` and `mv` require exactly two path
tokens. Extra tokens are usage errors except for `write`, whose remainder
is data. For `write`, parse the first whitespace-delimited token after the
command as PATH; the remaining text after the path/data separator is passed
as bytes without tokenization, preserving embedded spaces and punctuation.
No new quoting or escaping grammar is added. No data after PATH means an
empty payload and therefore an empty file. Leading whitespace used to
separate command, path, and data is syntax; subsequent payload bytes are
preserved as entered. The shell input capacity remains its current 260-byte
fixed buffer, so a single command is bounded by that existing capacity.

On success print a concise operation-specific confirmation, such as
`created`, `written N bytes`, `directory created`, `removed`, `copied`, or
`moved`; `touch` on an existing regular file reports success without
implying modification. On error, print the command name and a clear reason:
not mounted, I/O error, invalid filesystem/corrupt chain, not found,
already exists, invalid 8.3 name/path, no space, directory not empty,
read-only volume, not a directory, is a directory, unsupported operation,
or invalid API handle/descriptor where relevant. Wrong argument count prints
the exact command usage. Terminal messages are presentation only; status
semantics are defined by the public APIs.

## Status and error mapping

Retain existing `filesystem::Status` values where present and extend it
without renumbering assumptions in existing code. At minimum callers can
distinguish:

| Semantic result | Status meaning |
| --- | --- |
| `Ok` | Operation completed, including no-op success for existing regular-file `touch`. |
| `NotMounted` | Test FAT32 volume is absent or not mounted. |
| `IoError` | Required device read/write, FAT update, rollback, or metadata I/O failed. |
| `InvalidFilesystem` | BPB/geometry is not a supported FAT32 volume. |
| `NotFound` | Source/final path component does not exist, or a required parent is missing. |
| `AlreadyExists` | Creation/move/copy requires an absent destination but it exists. |
| `InvalidName` | Invalid 8.3 name, path syntax, or forbidden `.`/`..` user component. |
| `NoSpace` | Insufficient free clusters for a complete new chain or directory growth. |
| `DirectoryNotEmpty` | `rm` targeted a directory with live children. |
| `ReadOnly` | A mounted volume/boundary is genuinely read-only; not used for absent Test media. |
| `NotDirectory` | A path's required parent component is not a directory. |
| `IsDirectory` | A file-only operation targeted a directory. |
| `Corrupt` | Invalid/cyclic FAT chain or malformed on-disk directory state. |
| `Unsupported` | Unsupported BPB mode, size beyond FAT32 limit, cross-parent directory move, or other out-of-scope operation. |
| `InvalidDescriptor` / `InvalidHandle` | Existing VFS handle errors; writable path operations do not invent handles. |

Use repository enum spelling consistent with these semantics. Disk-full is
`NoSpace`; read-only Boot requests are `ReadOnly`. Missing or unmounted
Test media is specifically `NotMounted`. Do not return success after a
required write failure or continue dependent mutation after a failed
required FAT/directory/data write. Validation that can be done before
writing should be completed before the first sector mutation. If cleanup
fails, report `IoError` while retaining the safest known state.

## Failure handling and non-journaled limits

The filesystem is not journaled. Ordering reduces avoidable corruption but
does not provide full crash, power-loss, or torn-sector atomicity.

- **Failed allocation/FAT write:** update every FAT copy supported by the
  mounted mirrored geometry. On a partial-copy failure, report `IoError`,
  attempt bounded rollback for confirmed writes, stop publication, and
  reclaim newly allocated clusters only where chain ownership is known.
- **Disk full:** return `NoSpace` before publishing. Existing file data
  remains authoritative for replacement; a copy destination remains absent.
- **Failed data-sector write:** do not publish an incomplete chain. Free
  the unpublished chain best-effort; retain the previous version.
- **Failed directory-sector write:** do not report success. Before
  publication, old file/destination state remains authoritative. If the
  storage API cannot determine whether a sector write took effect, do not
  make unsafe assumptions during rollback; return `IoError` and avoid
  freeing clusters that may now be referenced.
- **Full directory:** grow it using the zero-before-link sequence above.
  If allocation fails, return `NoSpace`; if any I/O fails, return
  `IoError` and do not use the new slot until linking succeeded.
- **Corrupt chain:** stop traversal at the first invalid link or after the
  geometry-derived bound; return `Corrupt`, do not loop, and do not free
  clusters based on an untrusted chain unless a bounded ownership-safe
  cleanup is possible.
- **Partial `write`/`cp`:** keep the old file or absent destination
  authoritative until all data and chain metadata are ready. Cleanup is
  bounded and best-effort. Never free a chain if publication may have made
  it live. A cleanup failure may leak clusters; preserving visible data is
  preferable to risking data loss.

All byte-size, sector-count, cluster-count, and LBA arithmetic is checked
before use. FAT directory file size is 32-bit; reject larger public writes
with `Unsupported` before allocation and without truncating `size_t`.

## Testing and acceptance

Implementation uses strict TDD RED -> GREEN for each task: first add
focused host/QEMU expectations and run them to demonstrate the missing
behavior; then implement the smallest change; rerun focused tests to GREEN
before proceeding. Host FAT tests use a bounded fake sector device capable
of observing sectors/FAT copies and injecting read/write failures at
selected operations. Tests assert statuses as well as disk contents,
chains, directory entries, and preservation behavior.

Focused tests must cover:

- 8.3 validation, uppercase on-disk encoding, case-insensitive lookup,
  invalid characters, `.`/`..`, and path-capacity limits;
- empty-file `touch`, no-op behavior on an existing file, and rejection of
  a directory target;
- arbitrary-byte write/readback including NUL bytes, zero-length files,
  multi-cluster write/readback, growth, shrink/replacement, and checked
  size arithmetic;
- FAT chain allocation, link/EOC, freeing, bounded corrupt-chain
  rejection, disk-full preservation, and directory-full behavior;
- every FAT copy updated on supported mirrored volumes and partial FAT
  write-failure/rollback behavior;
- `mkdir`, zeroed cluster, valid `.`/`..`, parent requirements, and refusal
  of existing destinations;
- deleting files, deleting empty directories, refusing non-empty
  directories, and refusing root/dot entries;
- file-only multi-cluster `cp`, exact bytes, destination-exists rejection,
  and unpublished destination/cleanup behavior on injected failures;
- file move between directories, same-parent directory rename,
  destination-exists rejection, refusal of cross-parent directory moves,
  and invalid/root/self move rejection;
- injected failures on FAT sectors, data sectors, directory sectors,
  allocation, cleanup, and rollback wherever the host fake-device seam
  permits;
- terminal argument/usage parsing, remainder-as-data preservation,
  success/error messages, and malformed commands;
- write then independent readback from the mounted Test filesystem.

QEMU integration uses the disposable Test/slave FAT32 image, never the
Boot/master image, to perform a useful sequence such as touch, arbitrary
write, read/stat, mkdir, copy, move, and remove. The kernel test must verify
the actual on-disk results by reading them back through the normal
filesystem/VFS path. After QEMU exits, an independent fixture inspection
may additionally verify persisted image contents; the test must not
recreate/reset the Test image before this persistence check. The QEMU path
or source checks must prove that mutation calls are Test-only and that a
Boot write is rejected by the storage guard.

The complete existing regression suite remains required: host memory,
storage, filesystem/VFS, shell, graphics, network RTL8139/Ethernet/ARP/IPv4/
ICMP/UDP/DNS, Ring 3, preemptive scheduling, desktop/input, no-network
fallback, freestanding/relocation checks, and QEMU modes. `nm -u
build/kernel.elf` must have no unresolved symbols and `git diff --check`
must be clean.

## Explicitly out of scope

- FAT long filenames.
- Recursive `rm`.
- Recursive directory copy.
- Moving directories between different parents.
- Journaling or full crash/power-loss atomicity.
- Filesystem repair/fsck.
- Permissions/ownership.
- Timestamps.
- Symbolic links or hard links.
- Multiple writable mounts.
- Boot/master writes.
- Nano/editor implementation itself.
