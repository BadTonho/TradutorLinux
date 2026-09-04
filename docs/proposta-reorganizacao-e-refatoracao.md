# Proposta de Reorganização e Refatoração Modular — TradutorLinux

## 1. Contexto e Motivação

O TradutorLinux evoluiu de forma rápida ao longo das 13 fases do seu roadmap, passando de um runtime mínimo de console com apenas 3 APIs importadas (`GetStdHandle`, `WriteFile`, `ExitProcess`) para um runtime com loader PE, unwinding SEH x86-64, rede, registro persistente e GUI X11 experimental.

A reorganização principal já foi aplicada. O estado atual é modular em `src/runtime/core`, `src/runtime/seh`, `src/runtime/dlls` e `src/cli`; os pontos que ainda justificam manutenção são:
1. `src/runtime/dlls/kernel32/` e `src/runtime/dlls/user32/` ainda podem ganhar subdivisões internas quando houver um alvo concreto.
2. A suíte Win32 já foi separada por domínio em `test_win32_apps.cpp`,
   `test_win32_external.cpp`, `test_win32_gui.cpp`, `test_win32_security.cpp` e
   `test_win32_stubs.cpp`; `tests/test_win32.cpp` ainda concentra os testes
   comuns remanescentes e é o maior arquivo da suíte.
3. Algumas famílias legadas permanecem diretamente em `src/runtime/` (`gdi32.cpp`, `shell32.cpp`, rede e outras) e só devem ser movidas junto com testes e dependências reais.
4. `include/tradutorlinux/win32/` já separa tipos e contratos por família; `winapi.hpp` permanece como superfície de compatibilidade.
5. O próximo trabalho de refatoração deve priorizar testes e famílias ainda grandes, sem repetir a migração já concluída.

---

## 2. Diagnóstico dos Maiores Arquivos

| Arquivo Atual | Tamanho | Responsabilidades Misturadas | Destino Proposto |
|---|---|---|---|
| `src/runtime/dlls/kernel32/*.cpp` | módulos temáticos | Arquivos, processos, threads, memória, tempo e console | Dividir somente quando uma API-alvo justificar |
| `src/runtime/dlls/user32/*.cpp` | módulos temáticos | Mensageria, janelas, menus, diálogos e clipboard | Dividir controles quando houver contrato e teste próprios |
| `include/tradutorlinux/win32/` | headers por família | Tipos e contratos ABI Win32 | Manter `winapi.hpp` apenas como superfície compatível |
| `src/cli/*.cpp` | `options`, `report`, `runner` | CLI, relatório, catálogo e execução | Manter separação e evitar retorno a um monólito |
| `tests/test_win32.cpp` | maior fonte de testes remanescentes | Testes comuns de Win32 | Dividir apenas os domínios que ainda trouxerem benefício de manutenção |
| `src/loader/module.cpp` | registro de módulos | Registro e lookup de exports | Manter tabelas junto das DLLs, como já ocorre |

---

## 3. Estrutura Proposta

### 3.1. Reorganização de `src/runtime/`

Nota: a árvore abaixo preserva a proposta original para fins de histórico. A
reorganização correspondente já existe em grande parte; não recriar caminhos
que já foram migrados. O restante deve ser executado apenas quando houver um
alvo, teste e benefício de manutenção claramente identificados.

```text
src/runtime/
├── core/                     # Infraestrutura profunda de bootstrap e contexto
│   ├── guest_context.cpp     # Gerenciamento de GuestContext
│   ├── guest_entry.S         # Trampolim assembly de troca de pilha e ABI
│   ├── memory_validator.cpp  # Validação de limites de memória (/proc/self/maps)
│   ├── environment.cpp       # Bloco de variáveis de ambiente Win32
│   ├── runtime_context.hpp   # Definições internas de contexto e threads
│   └── winapi.cpp            # Despacho central do entry point
│
├── seh/                      # Subsistema estruturado de exceções x86-64
│   ├── unwind.cpp            # Parser de .pdata/.xdata e RtlVirtualUnwind
│   ├── unwind_capture.S      # RtlCaptureContext em assembly
│   └── seh.S                 # Trampolim de despacho SEH
│
├── dlls/                     # Implementações de DLLs organizadas por família
│   ├── kernel32/             # Fim do monólito de 275 KB
│   │   ├── file.cpp          # CreateFile, ReadFile, WriteFile, MoveFile, FindFirstFile...
│   │   ├── process.cpp       # CreateProcess, ExitProcess, OpenProcess, Toolhelp...
│   │   ├── thread.cpp        # CreateThread, TlsAlloc, GetExitCodeThread...
│   │   ├── sync.cpp          # Mutex, Event, Semaphore, SRWLock, CondVar, WaitFor*...
│   │   ├── memory.cpp        # VirtualAlloc, VirtualProtect, HeapAlloc, GlobalAlloc...
│   │   ├── time.cpp          # GetTickCount64, Sleep, GetSystemTimeAsFileTime...
│   │   └── exports.cpp       # Tabela de exportações da KERNEL32
│   │
│   ├── user32/               # Fim do monólito de 158 KB
│   │   ├── message.cpp       # GetMessage, PeekMessage, SendMessage, PostMessage...
│   │   ├── window.cpp        # CreateWindowEx, RegisterClassEx, ShowWindow, SetWindowPos...
│   │   ├── controls/         # Controles lógicos nativos (Button, Edit, Listview, etc.)
│   │   │   ├── button.cpp
│   │   │   ├── edit.cpp
│   │   │   └── listview.cpp
│   │   ├── dialog.cpp        # DialogBox, CreateDialog, dialog_template.cpp
│   │   └── exports.cpp       # Tabela de exportações da USER32
│   │
│   ├── gdi/                  # Subsistema gráfico 2D
│   │   ├── gdi32.cpp
│   │   └── gdiplus.cpp
│   │
│   ├── net/                  # Rede, sockets e internet
│   │   ├── ws2_32.cpp        # WinSock2 (sockets, poll, getaddrinfo)
│   │   ├── wininet.cpp       # WinINet (HTTP/HTTPS, cache, URLs)
│   │   ├── iphlpapi.cpp
│   │   └── mpr.cpp
│   │
│   ├── system/               # Sistema operacional, registro e segurança
│   │   ├── advapi.cpp        # Registro Win32 (RegOpenKey, RegSetValue, etc.)
│   │   ├── security.cpp      # SIDs, DACLs, Tokens de segurança
│   │   ├── ntdll.cpp
│   │   └── psapi.cpp
│   │
│   ├── shell/                # Desktop, temas e diálogos comuns
│   │   ├── shell32.cpp
│   │   ├── shlwapi.cpp
│   │   ├── comctl32.cpp
│   │   ├── comdlg32.cpp
│   │   ├── uxtheme.cpp
│   │   ├── dwmapi.cpp
│   │   └── imm32.cpp
│   │
│   ├── com/                  # Component Object Model
│   │   ├── ole32.cpp
│   │   └── oleaut32.cpp
│   │
│   ├── crypto/               # Criptografia e validação de confiança
│   │   ├── crypt32.cpp
│   │   └── wintrust.cpp
│   │
│   └── crt/                  # C Runtime
│       └── msvcrt.cpp
```

### 3.2. Reorganização de Cabeçalhos (`include/tradutorlinux/`)

Criar subpastas em `include/tradutorlinux/win32/`:
- `include/tradutorlinux/win32/types.hpp`: tipos fundamentais Win32 (`HWND`, `HANDLE`, `DWORD`, `BOOL`, `RECT`, `POINT`).
- `include/tradutorlinux/win32/kernel32.hpp`: protótipos de `KERNEL32.dll`.
- `include/tradutorlinux/win32/user32.hpp`: protótipos de `USER32.dll`.
- `include/tradutorlinux/win32/gdi32.hpp`: protótipos de `GDI32.dll`.
- `include/tradutorlinux/runtime/winapi.hpp`: arquivo guarda-chuva que inclui os módulos acima, garantindo **retrocompatibilidade imediata** com qualquer código existente.

---

## 4. Benefícios Práticos da Refatoração

1. **Velocidade de Compilação Drasticamente Superior**:
   - A divisão atual permite alterar APIs de arquivo em `src/runtime/dlls/kernel32/file.cpp` sem recompilar um monólito histórico.
   - Os domínios principais da suíte Win32 já foram extraídos para arquivos
     próprios; `tests/test_win32.cpp` permanece como núcleo comum e pode ser
     reduzido em uma manutenção futura, caso isso traga benefício mensurável.
2. **Localização Imediata do Código**:
   - Rede $\to$ `src/runtime/dlls/net/`.
   - Criptografia $\to$ `src/runtime/dlls/crypto/`.
   - Threads e Concorrência $\to$ `src/runtime/dlls/kernel32/thread.cpp` e `sync.cpp`.
   - Janelas e Eventos $\to$ `src/runtime/dlls/user32/window.cpp` e `message.cpp`.
3. **Eliminação de Código Duplicado**:
   - A antiga sobreposição de `legacy_winapi.cpp` foi eliminada; novas APIs devem continuar entrando no módulo da família correspondente.
4. **Descentralização do Registro de DLLs**:
   - As tabelas de exportação já ficam junto dos módulos de cada DLL, enquanto o loader mantém apenas registro e lookup.
5. **Facilidade para Testes Unitários Focados**:
   - A separação por domínio já cobre aplicativos, APIs externas, GUI,
     segurança e stubs. O arquivo `tests/test_win32.cpp` ainda pode receber
     uma divisão adicional por assunto, mas deixou de ser a única suíte para
     esses subsistemas.

---

## 5. Histórico da reorganização

As fases abaixo registram somente o histórico da migração. As fases 1–7 foram
aplicadas conforme a estrutura atual do projeto e não são instruções para mover
novamente os mesmos arquivos. Qualquer manutenção futura, inclusive uma nova
subdivisão motivada por um alvo concreto, deve ser registrada no
[backlog consolidado do `ROADMAP.md`](../ROADMAP.md#backlog-consolidado).

* **Fase 1 — Agrupamento de DLLs Independentes (concluída)**:
  - Mover `ws2_32.cpp`, `wininet.cpp`, `iphlpapi.cpp`, `mpr.cpp` $\to$ `src/runtime/dlls/net/`.
  - Mover `crypt32.cpp`, `wintrust.cpp` $\to$ `src/runtime/dlls/crypto/`.
  - Mover `ole32.cpp`, `oleaut32.cpp` $\to$ `src/runtime/dlls/com/`.
  - Atualizar caminhos no `CMakeLists.txt`.
* **Fase 2 — Infraestrutura Core e SEH (concluída)**:
  - Mover arquivos de bootstrap para `src/runtime/core/`.
  - Mover arquivos de exceção para `src/runtime/seh/`.
  - Atualizar caminhos no `CMakeLists.txt`.
* **Fase 3 — Descentralização de `module.cpp` (concluída)**:
  - Extrair as tabelas de exports para cada DLL respectiva.
* **Fase 4 — Modularização de `kernel32.cpp` e Unificação de `legacy_winapi.cpp` (concluída)**:
  - Criar a subpasta `src/runtime/dlls/kernel32/` e dividir por funcionalidade (`file.cpp`, `process.cpp`, `thread.cpp`, `sync.cpp`, `memory.cpp`, `time.cpp`).
* **Fase 5 — Modularização de `user32.cpp` (concluída parcialmente)**:
  - Criar a subpasta `src/runtime/dlls/user32/` e separar `message.cpp`, `window.cpp`, `controls/`, `dialog.cpp`.
* **Fase 6 — Modularização de Cabeçalhos e CLI (concluída)**:
  - Dividir `winapi.hpp` e `cli.cpp` conforme a proposta.
* **Fase 7 — Separação da suíte Win32 (concluída)**:
  - Extrair os testes de aplicativos, APIs externas, GUI, segurança e stubs,
    preservando helpers compartilhados, fixtures e regressões existentes.
