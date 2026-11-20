# SFS Common V2 migration status

The native source-owned SFS0 checksum/root/bitmap codec remains authoritative
in `native/filesystems/sfs/core/`. The installed library links that native
format code and Common V2 only. The V1 worker and filesystem fixture code
remain in this repository for independent, non-installed tests, not in SFS.so.

The hosted V2 provider probes real SFS0 roots, validates root and bitmap
checksums, mounts metadata-only read-only volumes, and queries real on-media
block/free counts. Multiple independent sessions now have non-reused opaque
handles, rather than a process-wide singleton. Stale-handle access fails.

**Not complete:** genuine SFS0 directory/object/extent traversal and V2
STAT/LIST/READ, a freestanding ELF using the same canonical engine,
production BOOT validation and OVMF kernel/initramfs reads are outstanding.
The CI DEB/RUN artifacts are not published as firmware-qualified releases.
No .ko substitutes are bundled.
