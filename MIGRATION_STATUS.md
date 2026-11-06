# SFS source consolidation — 6 November 2026

This repository now contains every source and design file under `native/filesystems/sfs/` in central Filesystems main at `a253fb724ce1fd760a361219281effb6abd71367`. The existing independent SFS portable core was byte-identical to the central source and retained. Missing Linux VFS adapter, historical ASFS implementation, and Windows adapter documentation were imported with the original source/provenance intact.

The local `standalone/kernel.json` no longer references a downloaded Filesystems revision. The `sfs.ko` build now compiles this repository's own Linux implementation. The `SFS.so` build continues to use the independent canonical source and regression tests.

Runtime, kernel-module and media correctness must still be tested against the exact new main commit. Central Filesystems source deletion requires detaching its CMake, test and packaging dependencies first, not silently breaking the manager.
