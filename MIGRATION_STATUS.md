# SFS0 migration — 2026-10-30

Canonical SFS0 core and design copied unmodified from Filesystems ca2fa1210c3c1f2b6f52cd15b3da6dfa83d453ad. The Defragmenter SFS native engine and relocation worker, which **also handle SFS2**, are copied unchanged into `migration/defragmenter/native/` from Defragmenter commit 30af892d8897d3aeaeefeac1bfcdb9620a0a5c72.

SFS2 is a separate disk format and has a different independent repository: `Filesystem-SFS2`. Do **not** automatically reuse SFS0 disk structures for SFS2. The shared Defragmenter source in this directory is a transitional reference; split correctly and verify variant-specific structures before releasing either format.

Not yet a standalone SFS.so, Common ABI adopter, or BOOT/kernel-qualified driver. Original active sources remain untouched.
