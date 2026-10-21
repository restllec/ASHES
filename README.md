# ASHES

Visual Studio 2026 C++ solution containing `Ashes.exe` and two x86 DLLs:

- `AshesAgent.dll` is manually mapped into the suspended MTA launcher and installs the child-process hook.
- `AshesClient.dll` is manually mapped by the bootstrap agent into `gta_sa.exe` and owns the runtime loader, Lua bridge, and network hooks.

Build `DarkFlame.sln` with `Release | x86`.

Both manually mapped DLLs install a PDB-aware crash handler. Crash traces and minidumps go to the `CrashDumps` folder, with manual-map addresses resolved to function names and source lines.

The required x86 MinHook sources are vendored under `Agent/third_party/minhook`; the solution has no external library dependency.

