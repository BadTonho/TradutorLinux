# Análise crítica do TradutorLinux (2026-10-03)

Baseada na leitura do roadmap, da estrutura do repositório, das métricas de
código e do trabalho das etapas R43 a R46. Onde há risco, ele é descrito como
risco, não como fato verificado.

## O que está bom

- **Disciplina de processo:** 793 commits, um commit por etapa, matriz de
  compatibilidade. A regra de falhar de forma controlada (módulo, símbolo e
  mecanismo no trace) funciona.
- **Honestidade nos docs:** estados "Parcial" e limitações registradas evitam
  prometer o que não existe.
- **Segurança:** W^X respeitado, memória do convidado validada com
  `read_guest_value`/`write_guest_memory`, entradas PE tratadas como hostis.
- **Testes:** cerca de 988 testes no CTest, com fixtures próprios sem CRT.
- **Escopo:** apenas PE32+ x86-64, sem emulação de CPU.

## Problemas críticos

### 1. Estado global da GUI sem sincronização [CONCLUÍDO na R47]
- `g_windows`, `g_classes` e `WindowSlot` eram globais sem sincronização uniforme.
- Nas etapas R45 e R46 a restrição de thread-affinity foi removida de
  `find_window_slot`, `find_class_slot`, `CreateWindowExA`, `DestroyWindow`,
  `GetDC` e outras para permitir operações cross-thread do launcher Rockstar.
- **Implementado na R47:**
  - Introduzido `std::recursive_mutex g_gui_state_mutex` protegendo `find_class_slot`,
    `find_window_slot`, `window_drawing_target`, todas as 28 APIs de janelas/classes em
    `window.cpp`, desenho/DCs em `misc.cpp` e despacho de mensagens em `message.cpp`.
  - Hierarquia estrita de locks: `g_sockets_mutex` antes de `g_gui_state_mutex` e
    `tl_WSAPumpAsyncSelect()` disparado fora do lock para evitar deadlocks com worker threads.
  - Liberação de lock em laços de espera (`GetMessageA`, `MsgWaitForMultipleObjectsEx`) antes
    de sleeps ociosos.
  - Adicionado preset `tsan` (`TL_ENABLE_TSAN`) no CMake (`CMakePresets.json`, `CMakeLists.txt`,
    `cmake/Sanitizers.cmake`).
  - Teste unitário de estresse multithread `ConcurrentMultiThreadWindowOperationsAreThreadSafe`
    com 4 workers criando, alterando e destruindo janelas em concorrência com peeker de mensagens.

### 2. Roadmap desatualizado
- `ROADMAP.md` termina na R29, e a tabela de apps ainda diz que o Rockstar sai
  com `ExitProcess(3)`. O projeto está na R46.
- `AGENTS.md` define o roadmap vigente como fonte de verdade; hoje ele não é.
- **Ação:** mover R25–R46 para `feitos/` e criar um roadmap novo com a fila real.

### 3. Artefatos versionados no git
- `Testing/Temporary/*` e `_CPack_Packages/**` (inclusive o binário
  `usr/bin/tradutorlinux`) estão rastreados apesar do `.gitignore`, que não
  retroage sobre arquivos já commitados.
- Também aparecem `tl_phase5_data.bin`, o `.deb` e `_tl_test` na raiz.
- **Ação:** `git rm -r --cached` neles.

### 4. Arquivos muito grandes
- `msvcrt.cpp` (2215 linhas), `process.cpp` (2063), `message.cpp` (2058);
  `runner.cpp`, `winapi.cpp` e `cxx_eh.cpp` perto de 1900.
- Cerca de 66 mil linhas em 96 arquivos `.cpp`. Encarece a revisão e esconde
  bugs como o item 1.

## Problemas de produto

### 5. Nenhum alvo do portfólio funciona de ponta a ponta

| App | Estado real |
|---|---|
| Rockstar | Janela e botões aparecem; nada funcional (rede, instalação) |
| Roblox | `RBXCRASH`, exit 3 |
| PuTTY | Para no `KEXDH_INIT`, sem SSH |
| HWiNFO64 e Rufus | Bloqueados por W^X do UPX |
| 7-Zip GUI e Notepad++ | Smokes falhando |
| ~12 binários | Rejeitados por serem 32-bit |

O projeto mede "janela abriu" e "imports 100%", que não equivalem a suporte
funcional. O caso mais sólido é o 7-Zip CLI.

### 6. Testes vermelhos no CTest completo
- Falharam 5: `Win32VersionTest` (passa isolado: flaky ou dependente de ordem),
  `seven_zip_gui_smoke`, `notepadpp_real_gui_smoke`, e os dois do `tl_shell`.
- Os dois do `tl_shell` foram corrigidos na R46.
- Os smokes do 7-Zip e do Notepad++ **não foram investigados**. O do Notepad++
  espera `exit 3` e `unsupported-cxx-handler-during-search`, e o aplicativo agora
  chega ao message loop, então o teste pode medir comportamento antigo. Não se
  sabe se a mudança de thread-affinity da R46 contribuiu.
- A R46 foi commitada com esses dois ainda vermelhos.

### 7. Muitos módulos são stubs [CONCLUÍDO / GetDlgItemText implementado e stubs auditados]
- `comdlg32`, `dwm`, `uxtheme`, `imm`, `setupapi`, `winmm` e `GetDlgItemText`
  (valores vazios). Eram decisões documentadas, mas o suporte real era menor do que a
  lista de exports sugeria.
- **Ações implementadas:**
  - `GetDlgItemTextA`, `GetDlgItemTextW`, `SetDlgItemTextA`, `GetDlgItemInt` e `SetDlgItemInt`
    em `src/runtime/dlls/user32/dialog.cpp` foram implementados de forma totalmente funcional,
    integrados a `tl_GetDlgItem` e ao gerenciamento de texto dos controles (`tl_GetWindowText` /
    `tl_SetWindowText`), com testes unitários em `tests/test_win32_gui.cpp`.
  - Stubs de `comdlg32` receberam rastreamento controlado (`diagnostics::TraceField` com
    mecanismo `stub` e status `unsupported`), respeitando a diretriz de falha controlada do `AGENTS.md`.
  - Módulos stubs (`uxtheme`, `dwmapi`, `imm32`, `setupapi`, áudio de `winmm`) permanecem
    explicitamente restritos e catalogados com `ExportSupport::Stub` no loader.

## O que precisa ser criado

1. Preset `tsan` e ao menos um teste de stress multi-thread da GUI.
2. Preset `sanitize` rodando de fato em CI (não foi encontrado pipeline).
3. Fixtures de integração que provem funcionalidade, não só abertura de janela:
   clique num botão do Rockstar gerando `WM_COMMAND`, e assertion automática de
   "zero `api-failure` no trace".
4. Roadmap novo. Decidir entre profundidade (um app de ponta a ponta, por
   exemplo o 7-Zip GUI) e largura (desbloquear mais binários).
5. Estratégia para x86 de 32 bits: cerca de metade do portfólio é rejeitada. A
   regra atual proíbe WOW64; é uma decisão de produto, não técnica.
6. Renderização real: o GDI desenha retângulos e texto estático; `GdipCreate*` é
   só ciclo de vida, sem pixels. Apps modernos exigem mais.

## Ordem recomendada

1. Corrigir a concorrência da GUI (item 1) e rodar TSAN.
2. Investigar os smokes vermelhos do 7-Zip e do Notepad++ (possível regressão).
3. Limpar o git (item 3) e reescrever o roadmap (item 2).
4. Escolher um app para levar de ponta a ponta.
