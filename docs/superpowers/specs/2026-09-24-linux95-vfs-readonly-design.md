# Linux95 Full Read-Only VFS Design

Date: 2026-09-24
Status: Design
Base branch: v1.0-dev

## Goal

Add a full read-only Virtual Filesystem layer to Linux95.

The VFS will sit between kernel callers and the FAT32 filesystem.

Future code such as the ELF loader and process system should use the VFS
instead of calling FAT32 directly.

## Architecture

Shell / future programs
        |
        v
       VFS
      /   \
     /     \
File FD   Directory handles
 table       table
   |           |
   +-----+-----+
         |
       FAT32
         |
       ATA disk

FAT32 remains the filesystem backend.

The VFS is read-only in this milestone.

## File Descriptor Table

Linux95 will use a global fixed-size file descriptor table.

Maximum entries:

- 64 file descriptor slots

Reserved descriptors:

- fd 0 = future stdin
- fd 1 = future stdout
- fd 2 = future stderr

Regular files begin at:

- fd 3

Descriptors are global because Linux95 does not yet have processes.

When processes are added later, descriptor ownership can move into
per-process tables.

## Directory Handle Table

Directory handles use a separate table from file descriptors.

Maximum entries:

- 32 directory handles

Directory handles are only used by:

- opendir()
- readdir()
- closedir()

File descriptors and directory handles are not interchangeable.

## Path Rules

Both absolute-looking and root-relative paths are accepted.

Examples:

/README.TXT
README.TXT

/DOCS/KERNEL.TXT
DOCS/KERNEL.TXT

Both forms resolve from the FAT32 root.

Linux95 does not have a current working directory in this milestone.

The VFS will not support:

- .
- ..
- chdir()
- cwd state
## File API

The VFS will provide these regular-file operations:

- open(path)
- read(fd, buffer, size, bytes_read)
- close(fd)
- stat(path, info)
- fstat(fd, info)

open() only opens regular files.

Attempting to open a directory as a regular file must fail.

Each open file descriptor stores:

- in-use state
- backend file identity or path
- current read offset
- file size

read() advances the descriptor's current offset.

Reading at EOF succeeds with zero bytes read.

close() releases the descriptor slot for reuse.

## FileStat

FileStat contains at least:

- bool is_directory
- uint32_t size

stat(path) queries an object by path.

fstat(fd) queries an already-open regular file.

## Directory API

The VFS will provide:

- opendir(path)
- readdir(dir_handle, entry, end)
- closedir(dir_handle)

opendir() only opens directories.

Attempting to open a regular file as a directory must fail.

Each directory handle stores enough state to continue iteration across
multiple readdir() calls.

readdir() returns one directory entry at a time.

When there are no more entries:

- the operation succeeds
- end is true

"." and ".." must never be exposed.

## DirectoryEntry

DirectoryEntry contains at least:

- char name[13]
- bool is_directory
- uint32_t size

The existing FAT32 backend remains DOS 8.3 only.

Long File Name support is not part of this milestone.

## Error Handling

The VFS should use explicit status values including:

- Ok
- NotMounted
- InvalidDescriptor
- InvalidHandle
- NotFound
- NotDirectory
- IsDirectory
- TooManyOpenFiles
- TooManyOpenDirectories
- IoError
- Corrupt
- Unsupported

open() and opendir() may use negative integer values for failure while
detailed filesystem errors remain represented through VFS status values
where practical.

## Read-Only Guarantee

This milestone must not expose filesystem mutation APIs.

Not included:

- write()
- create()
- mkdir()
- unlink()
- rename()
- truncate()
- file deletion
- directory deletion
- FAT allocation

No VFS code may call storage write operations.

## FAT32 Backend

The existing FAT32 implementation remains responsible for:

- mounting
- path lookup
- reading file data
- directory traversal
- cluster-chain traversal

The VFS adds:

- descriptor allocation
- directory handle allocation
- current read offsets
- iteration state
- stable filesystem-facing interfaces

Callers should no longer need FAT32-specific knowledge.

## Shell Integration

The existing shell commands will migrate to the VFS.

cat flow:

1. open file
2. repeatedly call read
3. close file descriptor

ls flow:

1. opendir path
2. repeatedly call readdir
3. closedir handle

fsinfo may continue using filesystem volume information.

## Testing

Host tests must cover:

- file descriptors start at 3
- descriptors 0, 1, and 2 remain reserved
- descriptor allocation
- descriptor reuse
- invalid descriptor rejection
- 64-descriptor exhaustion
- sequential read offsets
- EOF behavior
- stat()
- fstat()
- opening a directory as a file fails
- directory handle allocation
- directory handle reuse
- invalid directory handle rejection
- 32-handle exhaustion
- readdir iteration
- end-of-directory behavior
- opening a file as a directory fails
- root-relative paths
- leading-slash paths

QEMU self-tests should prove:

- VFS initialization succeeds
- VFS file open succeeds
- VFS file read succeeds
- VFS stat succeeds
- VFS directory open succeeds
- VFS readdir succeeds
- close operations succeed
- shell reaches ready state

Suggested markers:

[PASS] vfs_initialize
[PASS] vfs_file_open
[PASS] vfs_file_read
[PASS] vfs_stat
[PASS] vfs_directory_open
[PASS] vfs_readdir
[PASS] vfs_self_test

## Out of Scope

This milestone does not add:

- filesystem writes
- processes
- userspace
- syscalls
- ELF execution
- stdin/stdout/stderr implementation
- current working directory
- mount points
- multiple filesystem drivers
- FAT32 Long File Names
- permissions
- ownership
- symbolic links

## Future Direction

After this milestone, the Linux95 ELF loader can use stable
open/read/close/stat interfaces.

When process support arrives, the global file descriptor table can move
into per-process state.

Writable FAT32 can be implemented as a separate milestone later.
