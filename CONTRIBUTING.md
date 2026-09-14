# Contributing to TradutorLinux

Welcome, and thank you for your interest in contributing to **TradutorLinux**!

TradutorLinux is an open-source, user-space Win32 compatibility runtime engineered to execute real Windows applications (PE32+ x86-64) natively on Linux x86-64, without virtual machines and without CPU emulation.

Our goal is progressive, reliable, and verified compatibility driven by real applications, automated tests, and reproducible diagnostics.

---

## 1. Project Architecture & Philosophy

The runtime operates in user space according to the following model:

```text
PE32+ x86-64 Program
       │ Imported Win32 calls
       ▼
PE Loader + Import Resolver
       │
       ▼
Compatibility Runtime
  ├─ Microsoft x64 ↔ System V AMD64 ABI Bridge
  ├─ Supported Win32 APIs (KERNEL32, USER32, GDI32, etc.)
  ├─ Handles, memory mapping, TLS, SEH, errors, and paths
  └─ Diagnostics and tracing (--trace / --report)
       │
       ▼
Linux / POSIX System Calls
```

### Key Principles:
* **Controlled Failures:** When an API or mechanism is not yet supported, it must fail in a controlled manner, reporting the module, symbol, mechanism, and next limitation in the trace log rather than crashing silently.
* **Evidence-Based Progress:** Every new API or behavior implemented must be backed by an automated test fixture and registered in the compatibility matrix (`docs/compatibilidade.md`).
* **Cleanroom Implementation:** Implementations are based on public specifications, official documentation (e.g., Microsoft Learn / MSDN), and empirical testing. Do not copy code or disassembly from other runtimes or proprietary Windows components.
* **Respect Architecture Boundaries:** Generic runtime logic belongs in the shared runtime (`src/runtime/`). Specific shims or behaviors exclusive to one application belong strictly in isolated extension targets (`compat/apps/<app-id>/`).

---

## 2. Technical Standards

* **Language:** Modern C++20 for runtime and tooling. C is reserved for binary structures and low-level ABI layout. Assembly is restricted to essential trampolines and context switches.
* **ABI Boundary:** The boundary between the Microsoft x64 ABI (guest) and the System V AMD64 ABI (Linux host) must remain explicit, minimal, and fully tested. C++ exceptions must never cross ABI boundaries.
* **I/O Separation:** Standard output (`stdout`) is strictly reserved for the guest Windows application. All runtime diagnostics, logs, and trace data must go to standard error (`stderr`) formatted according to [docs/diagnostico.md](docs/diagnostico.md).
* **Memory Safety:** Treat all PE files and image data as untrusted input. Validate offsets, alignments, section bounds, and integer operations before reading or mapping memory.

---

## 3. Building & Testing

The recommended development environment is **Linux x86-64** (such as Ubuntu 24.04 LTS).

### Dependencies
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build git gdb llvm clang-tidy clang-format cppcheck gcc-mingw-w64-x86-64-win32 libqt6widgets6 qt6-base-dev zlib1g-dev
```

### Build Presets
```bash
# Configure and build debug preset
cmake --preset debug
cmake --build --preset debug

# Run automated test suite
ctest --preset debug --output-on-failure
```

To run with AddressSanitizer and UndefinedBehaviorSanitizer:
```bash
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize --output-on-failure
```

---

## 4. How to Contribute

### 4.1. Reporting Issues
When submitting an issue or bug report:
1. State the target application, version, and whether it is a 64-bit PE binary (`PE32+`).
2. Run the application with `tradutorlinux --report <app.exe>` and `tradutorlinux --trace <app.exe>`.
3. Include the full diagnostic output from `stderr` along with your Linux distribution and environment information.

### 4.2. Submitting Pull Requests
1. **Fork and branch:** Create a feature or bugfix branch based on `main`.
2. **Keep it focused:** Each pull request should address a single API, feature, or bugfix.
3. **Include tests:** Any new function or change in behavior should have an accompanying test in `tests/`.
4. **Update documentation:** If an API or application status changes, update [docs/compatibilidade.md](docs/compatibilidade.md).
5. **Verify before submitting:** Ensure code compiles cleanly with warnings enabled and passes all CTest suites.
