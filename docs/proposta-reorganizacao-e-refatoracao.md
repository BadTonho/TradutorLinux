# Proposta de Reorganização e Refatoração Modular — TradutorLinux

## 1. Contexto e Motivação

O TradutorLinux evoluiu de forma rápida ao longo das 13 fases do seu roadmap, passando de um runtime mínimo de console com apenas 3 APIs importadas (`GetStdHandle`, `WriteFile`, `ExitProcess`) para um subsistema abrangente com suporte a dezenas de bibliotecas Win32, PE loader completo, unwinding SEH x86-64, rede, registro persistente e GUI X11.

Com esse crescimento veloz, a estrutura de código tornou-se excessivamente plana e acumulou monólitos que dificultam a manutenção e elevam o tempo de compilação:
1. **`src/runtime/` possui 42 arquivos soltos na raiz**, misturando infraestrutura profunda de baixo nível com dezenas de DLLs Win32 de alto nível.
2. **`kernel32.cpp` atingiu 275 KB (~6.900 linhas)** e **`user32.cpp` atingiu 158 KB (~4.150 linhas)**, acumulando responsabilidades díspares no mesmo arquivo.
3. **`include/tradutorlinux/runtime/winapi.hpp` atingiu 146 KB**, atuando como um megacabeçalho que força o pré-processador a ler centenas de declarações não relacionadas em cada arquivo `.cpp`.
4. **`legacy_winapi.cpp` (77 KB) e `kernel32.cpp` (275 KB)** sobrepõem funções da mesma DLL (`KERNEL32.dll`), criando divisão artificial e confusão de manutenção.
5. **`src/loader/module.cpp` (1.874 linhas)** concentra mais de 1.500 linhas apenas em tabelas estáticas de exportação de todas as DLLs do runtime.
6. **`src/cli.cpp` (90 KB)** mistura parsing de argumentos, formatação de relatórios (`--report`), gerador de manifesto, catálogo de aplicativos e motor de instalação.

---

## 2. Diagnóstico dos Maiores Arquivos

| Arquivo Atual | Tamanho | Responsabilidades Misturadas | Destino Proposto |
|---|---|---|---|
| `src/runtime/kernel32.cpp` | ~275 KB | Arquivos, processos, threads, mutexes, memória, heap, tempo, console, fibers | Subpasta `src/runtime/dlls/kernel32/` dividida em módulos temáticos |
| `src/runtime/user32.cpp` | ~158 KB | Message loop X11, janelas, desenho de controles (edit, button, listview), caixas de diálogo | Subpasta `src/runtime/dlls/user32/` dividida em mensageria, janelas e controles |
| `include/.../winapi.hpp` | ~146 KB | Protótipos de KERNEL32, USER32, GDI32, SHELL32 e structs Win32 | Modularizar em `include/tradutorlinux/win32/` por DLL, mantendo `winapi.hpp` como guarda-chuva |
| `src/cli.cpp` | ~90 KB | Parser CLI, exportador JSON `--report`, catálogo de apps, instalador | Separar em `src/cli/options.cpp`, `report.cpp`, `runner.cpp` |
| `src/runtime/legacy_winapi.cpp` | ~77 KB | Funções legadas da KERNEL32 (arquivos, diretórios, paths) | Extinguir, unificando com `kernel32/file.cpp` |
| `src/loader/module.cpp` | ~143 KB | Registro de módulos + 1.500 linhas de tabelas estáticas de exports | Cada DLL mantém sua própria tabela `get_*_exports()`; `module.cpp` fica com ~100 linhas |

---

## 3. Estrutura Proposta

### 3.1. Reorganização de `src/runtime/`

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
   - Atualmente, alterar uma única linha em uma função de arquivo (ex.: `tl_MoveFileA`) aciona a recompilação de `kernel32.cpp` (275 KB) e `legacy_winapi.cpp` (77 KB), gerando carga pesada de CPU.
   - Com arquivos divididos por área, apenas `file.cpp` (~15 KB) é reconstruído em frações de segundo.
2. **Localização Imediata do Código**:
   - Rede $\to$ `src/runtime/dlls/net/`.
   - Criptografia $\to$ `src/runtime/dlls/crypto/`.
   - Threads e Concorrência $\to$ `src/runtime/dlls/kernel32/thread.cpp` e `sync.cpp`.
   - Janelas e Eventos $\to$ `src/runtime/dlls/user32/window.cpp` e `message.cpp`.
3. **Eliminação de Código Duplicado**:
   - Extingue `legacy_winapi.cpp`, agrupando a implementação de cada API em seu módulo legítimo.
4. **Descentralização do Registro de DLLs**:
   - Em vez de um `module.cpp` com quase 2.000 linhas, cada DLL expõe sua tabela estática independente (`get_kernel32_exports()`, `get_user32_exports()`), tornando simples adicionar ou atualizar APIs sem mexer no loader.
5. **Facilidade para Testes Unitários Focados**:
   - O monólito `tests/test_win32.cpp` (129 KB) pode ser quebrado em suítes específicas (`test_file.cpp`, `test_sync.cpp`, `test_memory.cpp`), permitindo que a máquina de desenvolvimento execute somente os testes afetados pela alteração.

---

## 5. Roteiro Gradual de Migração (Passo a Passo)

Para não desestabilizar o repositório e evitar builds pesados, a refatoração deve ser executada em fases independentes:

* **Fase 1 — Agrupamento de DLLs Independentes**:
  - Mover `ws2_32.cpp`, `wininet.cpp`, `iphlpapi.cpp`, `mpr.cpp` $\to$ `src/runtime/dlls/net/`.
  - Mover `crypt32.cpp`, `wintrust.cpp` $\to$ `src/runtime/dlls/crypto/`.
  - Mover `ole32.cpp`, `oleaut32.cpp` $\to$ `src/runtime/dlls/com/`.
  - Atualizar caminhos no `CMakeLists.txt`.
* **Fase 2 — Infraestrutura Core e SEH**:
  - Mover arquivos de bootstrap para `src/runtime/core/`.
  - Mover arquivos de exceção para `src/runtime/seh/`.
  - Atualizar caminhos no `CMakeLists.txt`.
* **Fase 3 — Descentralização de `module.cpp`**:
  - Extrair as tabelas de exports para cada DLL respectiva.
* **Fase 4 — Modularização de `kernel32.cpp` e Unificação de `legacy_winapi.cpp`**:
  - Criar a subpasta `src/runtime/dlls/kernel32/` e dividir por funcionalidade (`file.cpp`, `process.cpp`, `thread.cpp`, `sync.cpp`, `memory.cpp`, `time.cpp`).
* **Fase 5 — Modularização de `user32.cpp`**:
  - Criar a subpasta `src/runtime/dlls/user32/` e separar `message.cpp`, `window.cpp`, `controls/`, `dialog.cpp`.
* **Fase 6 — Modularização de Cabeçalhos e CLI**:
  - Dividir `winapi.hpp` e `cli.cpp` conforme a proposta.
