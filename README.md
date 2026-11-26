# Filesystem-SFS

## 1. Purpose and format scope

Original SmartFilesystem (SFS0), distinct from SFS2. This repository owns the format-specific implementation of `SFS.so` for InfiltratorOS.

## 2. On-disk architecture and invariants

Allocation trees and object/container metadata belong to the original SFS0 layout. Validate metadata identity and allocation bounds before reading; do not interpret SFS2 allocation roots as SFS0.

## 3. Common V2 implementation and capabilities

The current provider declares probe, mount, unmount, stat, directory enumeration, file read and format callbacks. Probe preserves native I/O and allocation failure status rather than converting it to an unrecognised format. The canonical entry point is `infiltrator_filesystem_api_v2`. Consumers must negotiate the actual profile and operation masks and use bounded, partition-relative host I/O. A source-level callback does not by itself establish that every format variant or host profile is qualified.

## 4. Integrity, mutation, allocation and recovery

Do not infer writable namespace or transactional relocation from a format callback. Formatting must be independently qualified; historical engines do not constitute exported V2 capabilities. For destructive operations, require the applicable exclusive writable host, durability contract and format-specific validation. Never silently substitute a kernel or FUSE driver or treat a rejected or unavailable operation as success. Preserve original disk-format semantics; relocation and recovery are separate from ordinary file I/O.

## 5. Build, installation and release

The target installation is `/System/Filesystems/SFS.so` and `/System/Resources/Documentation/Filesystem-SFS.pdf`. The project release policy is a portable amd64 DEB and a CPU-native source-first RUN, with GitHub's automatic source archive; do not treat this policy statement as verification of the current installer payload. The release workflow is the authoritative executable build and package specification. Historical adapters and test artifacts are not additional production filesystem authorities.

## 6. Qualification and limitations

[Recorded qualification workflow](https://github.com/Infiltrator-Projects/Filesystem-SFS/actions/runs/45516221620) · [Current workflows](https://github.com/Infiltrator-Projects/Filesystem-SFS/actions?query=branch%3Amain). A successful workflow establishes the tested scope only, not complete original-format parity, physical-media crash safety, or production BOOT/kernel integration. Use disposable images for destructive qualification.

## 7. Technical references and provenance

- [Provider implementation](module/sfs_module.c)
- [Native design](native/filesystems/sfs/DESIGN.md)
- [Migration and source history](MIGRATION_STATUS.md)
- [Original-format reference source](docs/SFS.pdfstreams) and [PDF](docs/SFS.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

The original-format reference and historical migration notes describe different evidence from the current production provider. When descriptions disagree, the current exported descriptor, implementation, executable tests and exact-revision release evidence take precedence.

The [engineering reference](docs/SFS-TECHNICAL-REFERENCE.md) adds field-level original-format guidance. Earlier material is preserved in the PDF.
