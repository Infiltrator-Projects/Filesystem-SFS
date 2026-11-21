# SFS Common V2 migration status — 21 November 2026

The installed SFS.so uses the source-owned SFS0 checksum, root, bitmap,
object, directory and extent readers from this repository and exports only
`infiltrator_filesystem_api_v2`. Historical V1 workers, fixtures and
filesystem-specific Linux driver code remain as non-installed test/source
material; they do not provide a production ABI or fallback.

## Current implementation

- Hosted Common V2 PROBE, read-only MOUNT/UNMOUNT, STAT, LIST, READ and
  VOLUME.QUERY. No unqualified write operations are advertised.
- Shared freestanding and hosted C format engine, with a no-import firmware
  ELF carried in the canonical SO as `.infiltrator_boot`.
- Root/backup checksum and format validation, metadata bitmap checks,
  generation-protected sessions, path and object traversal, directory
  enumeration and extent-based data reads.
- Reject allocation bitmaps marking an active root, bitmap, administration,
  object, transaction-marker or extent-tree block free even if its checksum
  has been correctly recomputed.
- Production Common V2 test fixtures exercise valid nested directories and
  fragmented file reads, corrupt structures, 4K host logical-sector I/O
  and nonmutation. These tests do not independently prove a real-media
  OVMF kernel/initramfs boot.

## Outstanding release gates

The exact packaged SFS.so still requires the unmodified BOOT production
`validate-module` contract, a disposable OVMF boot reading an actual
EFI-stub kernel and initramfs, and the complete DEB/native RUN release
qualification on the same commit. A hosted/firmware ELF compile is not
BOOT qualification. The existing published v1.0.10 release is not declared
compliant simply because newer main source code has progressed. Do not
publish a BOOT-ready replacement until these checks pass.
