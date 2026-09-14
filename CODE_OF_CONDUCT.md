# Code of Conduct — TradutorLinux

## 1. Purpose & Philosophy

TradutorLinux is a serious engineering project focused on developing a cleanroom Win32 compatibility runtime for Linux x86-64.

Our community is strictly centered around **code, architecture, systems programming, and technical excellence**. We value concrete engineering, reproducible benchmarks, and working implementations over bureaucracy and off-topic discussion.

---

## 2. Technical Standards & Conduct

All contributors, maintainers, and participants are expected to adhere to the following principles:

### 2.1. Code First, No Off-Topic Noise
* Keep issues, pull requests, and commit discussions **strictly technical**.
* Discussions must focus on C++20 architecture, PE32+ loader internals, ABI translation, Win32 API contracts, memory safety, and performance.
* Off-topic commentary, political debates, personal drama, and bikeshedding are not welcome and will be closed or deleted.

### 2.2. Cleanroom Development Integrity
* **Zero tolerance for copied code.** Never copy, extract, or transcribe source code or disassemblies from Wine, ReactOS, or proprietary Microsoft components.
* Contributions must be cleanroom implementations based on official public specifications (e.g., Microsoft Learn / MSDN), documented system calls, and independent reverse engineering verified by regression test fixtures.
* Submissions violating intellectual property or cleanroom rules will be rejected immediately.

### 2.3. Evidence-Based Technical Debate
* Technical disagreements must be settled using objective facts, code, and reproducible evidence:
  * Minimal reproducer fixtures (`.exe` test samples).
  * Assembly / ABI traces and calling convention conformance.
  * `ctest` test outputs, sanitizer reports (ASan, UBSan), or valgrind logs.
* Do not make unsupported claims about compatibility without a test fixture and a corresponding matrix entry in `docs/compatibilidade.md`.

### 2.4. Respect for Architecture & Scope
* Do not bypass project architectural boundaries. Generic runtime code belongs in `src/runtime/`; application-specific shims belong isolated in `compat/apps/<app-id>/`.
* Respect the scope of the active roadmap phase. Do not introduce ad-hoc hacks, incomplete stub sweeps, or bloated dependencies without an explicit target application or approved design.

### 2.5. Constructive & Direct Reviews
* Code reviews should be rigorous, honest, direct, and focused entirely on code quality, correctness, security, and performance.
* Feedback should target the code and architecture, never the person.

---

## 3. Enforcement

Maintainers ([@BadTonho](https://github.com/BadTonho)) reserve the right to:
1. Edit, close, or delete any issues, comments, or pull requests that violate these standards or derail technical discussions.
2. Reject pull requests that fail cleanroom requirements, lack tests, or introduce undocumented architectural regressions.
3. Block or restrict participants who repeatedly engage in disruptive, non-technical, or hostile behavior.
