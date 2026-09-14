# Security Policy — TradutorLinux

Security and robustness are critical goals for **TradutorLinux**. Because the runtime parses raw Windows PE32+ executable binaries, handles system call emulation, and bridges ABI boundaries at runtime, we treat all security issues with high urgency.

---

## 1. Supported Versions

Active security updates are currently provided only for the latest development branch:

| Version | Supported |
| :--- | :--- |
| `main` (active development) | :white_check_mark: Yes |
| Prior / experimental tags | :x: No |

---

## 2. Threat Model & Boundaries

* **Hostile Binary Input:** The PE parser, image loader, relocation engine, import resolver, and resource unpackers treat all `.exe` and `.dll` inputs as **untrusted and potentially malicious**. Any vulnerability allowing memory corruption, out-of-bounds reads/writes, or integer overflow during binary loading is treated as a critical defect.
* **Compatibility Layer Scope:** TradutorLinux is a **native compatibility runtime**, not a sandboxing solution. Guest programs run under the same user privileges and environment as the host Linux user. Users requiring strict isolation against known malicious binaries must combine TradutorLinux with host-level sandboxing tools (e.g., containers, Bubblewrap, Firejail, or virtual machines).

---

## 3. Reporting a Vulnerability

If you discover a security vulnerability in TradutorLinux, please practice **responsible disclosure**:

> **Notice:** **Do not open public GitHub issues** for exploitable security bugs or critical memory corruption flaws.

### Channels:
1. **GitHub Security Advisory (Preferred):**
   - Navigate to the **Security** tab of the repository: [`https://github.com/BadTonho/TradutorLinux/security/advisories`](https://github.com/BadTonho/TradutorLinux/security/advisories).
   - Click **"Report a vulnerability"** to submit a private report.
2. **Direct Contact:**
   - Reach out privately to the maintainer on GitHub ([@BadTonho](https://github.com/BadTonho)).

### In your report, please include:
* A clear description of the vulnerability and its theoretical impact.
* Step-by-step reproduction instructions.
* A minimal proof-of-concept (PoC) binary or test sample, if available.
* Details on the tested environment (compiler, OS distribution, sanitizers used).

---

## 4. Response & Disclosure Timeline

* **Acknowledgment:** We aim to acknowledge receipt within 48 to 72 business hours.
* **Triage & Reproduction:** We will reproduce and assess the severity using memory sanitizers (`ASan`/`UBSan`).
* **Fix & Release:** A fix will be developed privately, verified with regression tests, and released in a coordinated update.
* **Attribution:** We will publish a security advisory giving full credit to the researcher (unless anonymity is requested).
