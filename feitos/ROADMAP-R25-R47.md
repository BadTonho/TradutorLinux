# Roadmap histórico — Etapas R25 a R47 (2026-09-19 a 2026-10-03)

## Estado do documento

Este arquivo preserva integralmente o histórico das etapas R25 a R47 do TradutorLinux,
concluídas entre 2026-09-19 e 2026-10-03. Ele é um documento de arquivo histórico
e complementa os roadmaps anteriores arquivados em:
- [ROADMAP-R1-R24.md](ROADMAP-R1-R24.md) — Auditoria e ciclo R1 a R24 (2026-09-12 a 2026-09-15);
- [ROADMAP-LEGADO.md](ROADMAP-LEGADO.md) — Fases fundacionais e marcos legados.

As próximas etapas em andamento estão registradas no [ROADMAP.md](../ROADMAP.md) principal.

---

## Histórico das Etapas Concluídas (R25 a R47)

### R25 — Definir chave de host de teste e contrato de assinatura para o PuTTY ✓
- **Commit:** `9d34fc2` (2026-09-19)
- **Escopo:** O listener de teste valida o banner, troca `KEXINIT` com algoritmos negociados, aguarda o pacote `SSH_MSG_KEXDH_INIT` (tipo 30) do PuTTY com `parse_kexdh_init_packet` (mpint `e` de 256–258 bytes), encerra com `SSH_MSG_DISCONNECT` e observa o diálogo `PuTTY Fatal Error` no X11.
- **Validação:** Smoke `putty_ssh_local_probe` reportando recepção de `KEXDH_INIT` e encerramento controlado.

---

### R26 — Despacho polimórfico e ampliação de cleanups C++ FH4 ✓
- **Commit:** `386f83a` (2026-09-19)
- **Escopo:** Análise e suporte a até 16 ações na cadeia de terminação e destruição C++ FH4 do Notepad++. Alinhamento estrito de 16 bytes na pilha, preservação de não-voláteis e proteção contra ciclos infinitos.
- **Validação:** Smoke `notepadpp_fh4_headless_smoke` executando a cadeia estendida sem atingir `fh4-cleanup-limit`.

---

### R27 — Expansão do modelo de controles comuns Win32 (`SysListView32` / `COMCTL32`) ✓
- **Commit:** `e2939ee` (2026-09-19)
- **Escopo:** Tratamento das mensagens `LVM_*` (`LVM_GETITEMCOUNT`, `LVM_GETITEMW`, `LVM_SETITEMSTATE`, inserção, ordenação e seleção) no controle lógico de lista em `src/runtime/gui_controls.cpp`, com cópia protegida via `write_guest_memory`.
- **Validação:** Testes em `tests/test_gui_controls.cpp` e `tests/test_win32_gui.cpp`.

---

### R28 — Suporte a pacotes MSIX de grande porte e validação de Affinity x64 ✓
- **Commit:** `99c5b99` (2026-09-19)
- **Escopo:** Ampliação dos limites seguros de descompressão MSIX para 4 GiB total, 2 GiB por entrada e 20.000 entradas em `src/package/msix.cpp`. Tratamento de streams DEFLATE vazios com buffer auxiliar seguro na zlib e Rust.
- **Validação:** Extração e cadastro completo das 1284 entradas de `Affinity x64.msix` no catálogo de instalação.

---

### R29 — Tolerância no leitor PE para diretórios de dados em seções virtuais UPX ✓
- **Commit:** `866b7ba` (2026-09-19)
- **Escopo:** Tolerância no `pe_reader.cpp` para executáveis compactados com UPX onde diretórios de dados (como exports ou `.pdata`) apontam para seções virtuais com `SizeOfRawData == 0` (`UPX0`).
- **Validação:** `--report` em `HWiNFO64.exe` e `Rufus_x64.exe` com 100% de imports resolvidos e execução direta rejeitada de forma controlada por W^X (exit 4).

---

### R30 — Suporte a `SetDefaultDllDirectories`, segurança DACL e execução segura para Logitech G HUB ✓
- **Commit:** `ddc9d2c` (2026-09-19)
- **Escopo:** Implementação de `SetDefaultDllDirectories` em `kernel32`, manipulação básica de descritores de segurança DACL (`advapi32`) e validação de caminhos para o instalador LGHub.
- **Validação:** Testes unitários de DACL e DLL directories passando 100%.

---

### R31 — Implementar `rpcrt4.dll` com `UuidCreate`, `AppPolicyGet` em `kernel32` e suporte a GUI do Rockstar Games Launcher ✓
- **Commit:** `1dc1dd2` (2026-09-19)
- **Escopo:** Implementação do módulo `rpcrt4.dll` exportando `UuidCreate` e `UuidToStringA/W`. Adição de `AppPolicyGetProcessTerminationMethod` e `AppPolicyGetShowDeveloperDiagnostic` em `kernel32`.
- **Validação:** Desbloqueio da inicialização do Rockstar Games Launcher.

---

### R32 — Aceitar `STACK_SIZE_PARAM_IS_A_RESERVATION` em `CreateThread` e inicializar threads do RobloxPlayerInstaller ✓
- **Commit:** `be00769` (2026-09-23)
- **Escopo:** Suporte à flag `STACK_SIZE_PARAM_IS_A_RESERVATION` (`0x00010000`) em `CreateThread`, alocando a reserva solicitada pelo runtime do Roblox e inicializando suas threads de worker.
- **Validação:** Inicialização das threads concorrentes de `RobloxPlayerInstaller.exe`.

---

### R33 — Alinhar expectativas das matrizes nativa e de instalação aos avanços de Roblox, Rockstar e LGHub ✓
- **Commit:** `93f09fc` (2026-09-23)
- **Escopo:** Atualização dos scripts de validação de matriz (`verify_popular_apps_native.cmake` e `verify_popular_apps_install.cmake`) com os códigos de saída e comportamentos reais observados após as etapas R30–R32.
- **Validação:** CTest passando 100% nas matrizes de conformidade.

---

### R34 — Suportar `WM_NCCREATE` no ciclo de vida Win32 e registro antecipado de child windows ✓
- **Commit:** `00b5315` (2026-09-23)
- **Escopo:** Emissão determinística de `WM_NCCREATE` antes de `WM_CREATE` no ciclo de vida de `CreateWindowExA/W`. Registro antecipado do slot de janela para permitir que procedures filhas consultem seu próprio `HWND` durante `WM_NCCREATE`.
- **Validação:** Testes de ciclo de vida de criação de janelas em `tests/test_win32_gui.cpp`.

---

### R35 — Despachar `SendMessage` com `LRESULT` 64-bit para child wndprocs, expandir slots de janelas e permitir hierarquia aninhada ✓
- **Commit:** `aa1d657` (2026-09-23)
- **Escopo:** Garantir retorno de 64 bits (`LRESULT` / `std::intptr_t`) no despacho de `SendMessageA/W` para procedures filhas. Expansão da capacidade da tabela de slots de janelas e suporte a hierarquias profundas de controles pais/filhos.
- **Validação:** Testes de hierarquia aninhada e despacho de mensagens em `test_win32_gui.cpp`.

---

### R36 — Suportar templates de diálogo estendidos `DLGTEMPLATEEX`, identificadores 32-bit e containers aninhados ✓
- **Commit:** `e16bb3c` (2026-09-23)
- **Escopo:** Parser de templates binários `DLGTEMPLATEEX` (assinatura `0xFFFF0001`), permitindo IDs de controles e estilos de 32 bits, menus customizados e criação recursiva de controles contidos.
- **Validação:** Testes unitários em `tests/test_win32_dialog.cpp`.

---

### R37 — Implementar controle lógico `SysTabControl32` e gerenciamento de abas Win32 ✓
- **Commit:** `3171e96` (2026-10-02)
- **Escopo:** Implementação do controle lógico `SysTabControl32` gerenciando abas (`TCM_INSERTITEMA/W`, `TCM_GETITEMA/W`, `TCM_SETCURSEL`, `TCM_GETCURSEL`, `TCM_GETITEMCOUNT`) em `src/runtime/gui_controls.cpp`.
- **Validação:** Testes cobrindo abas em `tests/test_gui_controls.cpp`.

---

### R38 — Corrigir terminação nula de `GetUserDefaultLocaleName` e adicionar suporte WinNls de localidade ✓
- **Commit:** `60eafe1` (2026-10-02)
- **Escopo:** Correção da escrita de terminador nulo UTF-16 em `GetUserDefaultLocaleName` e inclusão de APIs de NLS (`GetLocaleInfoEx`, `IsValidLocaleName`).
- **Validação:** Testes de localidade e conformidade de buffers protegidos.

---

### R39 — Suportar handlers FH4 puros sem flags GS no desempilhamento C++ SEH ✓
- **Commit:** `109e419` (2026-10-02)
- **Escopo:** Reconhecimento e despacho seguro de descritores de desempilhamento FH4 sem o cookie de segurança GS, permitindo desempilhamento de código compilado com `/EHs` moderno sem canário ativo.
- **Validação:** Testes de unwinding e C++ EH em `tests/test_seh.cpp`.

---

### R40 — Permitir registro de classes com procedimentos de janela nativos e do grafo de módulos em `RegisterClassEx` ✓
- **Commit:** `bc63c96` (2026-10-02)
- **Escopo:** Validação flexível e segura de `lpfnWndProc` em `RegisterClassExA/W`, aceitando ponteiros nativos do runtime (como procedimentos de diálogo padrão) e procedimentos exportados pelo grafo de DLLs convidadas carregadas.
- **Validação:** Testes em `tests/test_win32_gui.cpp`.

---

### R41 — Suportar flags de acesso expandidas e criação de pastas pai em `CreateFile` ✓
- **Commit:** `6aada47` (2026-10-02)
- **Escopo:** Suporte a `FILE_APPEND_DATA` (`0x0004`) e criação automática de diretórios intermediários quando solicitado por instaladores ao criar logs aninhados.
- **Validação:** Teste `Win32FileTest.CreateFileSupportsFileAppendDataAndAutomaticParentDirectories`.

---

### R42 — Implementar API `VERSION.dll` com extração de recursos PE e suporte a consultas de versão ✓
- **Commit:** `a807f1d` (2026-10-02)
- **Escopo:** Implementação funcional de `GetFileVersionInfoSizeA/W`, `GetFileVersionInfoA/W` e `VerQueryValueA/W` lendo recursos reais de versão (`RT_VERSION`) de arquivos PE em disco.
- **Validação:** Teste com binário real `notepad++.exe` em `tests/test_win32.cpp` (`Win32VersionTest`).

---

### R43 — Implementar módulo `msftedit.dll` e suporte a `LoadStringW` em modo ponteiro read-only ✓
- **Commit:** `0a0eaa1` (2026-10-02)
- **Escopo:** Criação do módulo stub/exportador `msftedit.dll` (RichEdit 4.1) e suporte à convenção de `LoadStringW` com `cchBufferMax == 0` (retornando ponteiro direto somente-leitura para o recurso no módulo).
- **Validação:** Testes unitários de `msftedit` e `LoadStringW`.

---

### R44 — Implementar ciclo de vida e alocação de memória GDI+ e alcançar interface gráfica do Rockstar Launcher ✓
- **Commit:** `f18154e` (2026-10-02)
- **Escopo:** Implementação de `gdiplus.dll` cobrindo inicialização (`GdiplusStartup`, `GdiplusShutdown`), alocador de memória (`GdipAlloc`, `GdipFree`) e ciclo de vida básico de bitmaps/gráficos. Permitiu a exibição da janela gráfica do Rockstar Games Launcher.
- **Validação:** Execução do executável real do Rockstar com abertura de janela X11.

---

### R45 — Permitir memory DCs em `FillRect` e chamadas cross-thread em `InvalidateRect` ✓
- **Commit:** `adcf3b6` (2026-10-02)
- **Escopo:** Suporte a desenho em Device Contexts de memória (`CreateCompatibleDC`) dentro de `FillRect` e permissão para threads secundárias chamarem `InvalidateRect` sem rejeição por thread affinity.
- **Validação:** Testes em `tests/test_win32_gdi.cpp` e `tests/test_win32_gui.cpp`.

---

### R46 — Permitir criação de janelas filhas e consultas GUI cross-thread e validar existência de arquivo em `CreateProcess` ✓
- **Commit:** `f9a1c60` (2026-10-02)
- **Escopo:** Remoção de restrições arbitrárias de afinidade de thread para operações read-only e criação de botões filhos por worker threads. Validação rigorosa de existência de arquivo executável em `CreateProcessA/W` e `ShellExecuteExW`.
- **Validação:** Testes em `tests/test_win32_gui.cpp` e `tests/test_win32_external.cpp`.

---

### R47 — Sincronizar estado global da GUI com mutex recursivo e adicionar preset `tsan` ✓
- **Commit:** `3da6481` (2026-10-03)
- **Escopo:** Introdução de `std::recursive_mutex g_gui_state_mutex` protegendo o estado global de janelas, classes, DCs e despacho de mensagens contra data races entre a thread principal e workers. Hierarquia estrita com `g_sockets_mutex` e liberação em esperas ociosas. Adição do preset de ThreadSanitizer `tsan` no CMake.
- **Validação:** Teste unitário de estresse multithread `Win32GuiTest.ConcurrentMultiThreadWindowOperationsAreThreadSafe` com 4 workers e peeker simultâneos; 647 testes unitários passando.
