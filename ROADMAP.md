# Roadmap atual — TradutorLinux

## Estado do documento

Este é o roadmap de trabalho atual do TradutorLinux. Ele define as próximas
etapas de desenvolvimento e estabilização do runtime a partir de 2026-10-03 (R48 em diante).

O histórico completo de todas as 47 etapas anteriores está preservado em:
- [feitos/ROADMAP-R25-R47.md](feitos/ROADMAP-R25-R47.md) — Etapas R25 a R47 (2026-09-19 a 2026-10-03);
- [feitos/ROADMAP-R1-R24.md](feitos/ROADMAP-R1-R24.md) — Auditoria e ciclo R1 a R24 (2026-09-12 a 2026-09-15);
- [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md) — Fases fundacionais e marcos legados.

As regras de escopo continuam rigorosas: nenhuma capacidade deve ser declarada
sem teste de integração e registro na matriz de compatibilidade em
[`docs/compatibilidade.md`](docs/compatibilidade.md).

## Regras de execução

Cada etapa deve produzir:

1. implementação mínima no runtime ou no fixture de teste;
2. teste unitário ou de robustez;
3. fixture ou integração com aplicativo-alvo quando a mudança afetar uma API Win32 observável;
4. atualização da documentação técnica na pasta `docs/` e da matriz de compatibilidade;
5. validação reproduzível no Linux x86-64 com limites de recursos respeitados;
6. um commit próprio antes do início da etapa seguinte.

Uma exportação resolvida nunca pode ser tratada como suporte funcional. Quando a
operação ainda não existir, o runtime deve retornar uma falha controlada,
atualizar o erro correspondente (`GetLastError`) e registrar a limitação no
diagnóstico.

---

## Fila de trabalho atual

### R48 — Investigar e estabilizar os testes de fumaça (smokes) de 7-Zip e Notepad++

**Contexto:** Na auditoria recente do CTest completo, os testes de fumaça `seven_zip_gui_smoke`
e `notepadpp_real_gui_smoke` apresentaram falhas. Com a evolução do runtime nas etapas
R34–R47 (suporte a `WM_NCCREATE`, despacho LRESULT de 64 bits, sincronização com
`g_gui_state_mutex` e avanço no message loop), as expectativas antigas dos testes
(como encerramento antecipado com códigos de erro prévios) podem estar defasadas em relação
ao comportamento real das aplicações, ou pode haver regressões sutis que precisam ser sanadas.

**Tarefas:**
- [ ] Executar isoladamente `seven_zip_gui_smoke` e `notepadpp_real_gui_smoke` com trace detalhado;
- [ ] Determinar se a causa é mudança no ciclo de vida (aplicativo indo além do ponto esperado do smoke) ou regressão funcional;
- [ ] Ajustar as asserções e fixtures dos smokes para refletir o estado operacional atual com rigor;
- [ ] Garantir que ambos os testes passem de forma determinística e documentar no trace.

---

### R49 — Saneamento do repositório Git e remoção de artefatos rastreados

**Contexto:** Ao longo de compilações e execuções de testes, certos artefatos gerados
acabaram sendo commitados no repositório (`Testing/Temporary/*`, `_CPack_Packages/**`,
binários compilados como `usr/bin/tradutorlinux`, arquivos `.deb` e `_tl_test` na raiz).
O `.gitignore` não remove automaticamente arquivos que já estão no índice do Git.

**Tarefas:**
- [ ] Executar `git rm -r --cached` nos diretórios de build/teste indevidamente rastreados;
- [ ] Revisar e reforçar as regras do `.gitignore` para cobrir pastas temporárias de CTest e CPack;
- [ ] Assegurar que o working tree fique completamente limpo sem perda de código-fonte.

---

### R50 — Modularização e decomposição de arquivos críticos monolíticos

**Contexto:** Três arquivos centrais do runtime acumularam mais de 2.000 linhas cada:
`msvcrt.cpp` (2.215 linhas), `process.cpp` (2.063 linhas) e `message.cpp` (2.058 linhas),
além de `winapi.cpp` e `cxx_eh.cpp` próximos de 1.900 linhas. Essa concentração encarece
a manutenção, dificulta a revisão e aumenta o risco de concorrência.

**Tarefas:**
- [ ] Decompor `msvcrt.cpp` em sub-unidades lógicas (`msvcrt_file.cpp`, `msvcrt_string.cpp`, `msvcrt_memory.cpp`);
- [ ] Fatiar `process.cpp` separando criação de processos de gerenciamento de threads/tokens;
- [ ] Fatiar `message.cpp` separando filas de mensagens de dispatch e tradução;
- [ ] Garantir que a compilação continue limpa, sem warnings e com 100% dos testes unitários passando.

---

### R51 — Automação de verificação contínua e limites de teste

**Contexto:** Com a adição dos presets `tsan` e `sanitize`, é essencial dispor de um
script/procedimento seguro de validação pré-commit que use `ulimit -c 0` (evitando sobrecarga
do `systemd-coredump` em testes de sinal SEH) e limite o paralelismo a 2 jobs para não travar
máquinas de desenvolvimento modestas.

**Tarefas:**
- [ ] Criar script de conveniência `tools/run_checks.sh` com proteção `ulimit -c 0` e execução controlada;
- [ ] Documentar o fluxo de validação rápida por componente em `docs/desenvolvimento.md`.

---

### R52 — Levar o 7-Zip File Manager (`7zFM_x64.exe`) à funcionalidade interativa de ponta a ponta

**Contexto:** O 7-Zip File Manager já atinge resolução de 100% dos imports, inicializa
controles comuns (`SysListView32`, `SysTreeView32`, toolbars, menus dropdown e diálogos).
A meta desta etapa é viabilizar a navegação funcional em diretórios e abertura/extração
de arquivos diretamente pela interface gráfica sob X11.

**Tarefas:**
- [ ] Mapear o ciclo de duplo clique em pastas na list view (`LVM_GETITEMW` + navegação);
- [ ] Implementar as notificações pendentes de `WM_NOTIFY` / `NM_DBLCLK`;
- [ ] Validar extração de um arquivo `.zip` ou `.7z` de teste via interface gráfica;
- [ ] Atualizar a matriz de compatibilidade de aplicações comprovando suporte de ponta a ponta.

---

## Fora desta rodada

Continuam estritamente fora do escopo desta rodada:

- otimizações prematuras sem benchmark comprovado;
- suporte a binários PE32 de 32 bits (x86), ARM ou WOW64;
- emulação de drivers de kernel, serviços Windows, anticheat e DirectX nativo;
- substituição de stubs não requisitados por aplicativos-alvo.

---

## Auditoria de Erros e Bloqueios do Corpus Real (Atualizada em 2026-10-03)

Executada via `./build/debug/src/tradutorlinux` sobre os 27 alvos do corpus
`Aplicativos_Windows_Populares/`. A rodada cobre:
1. `--report`: análise estática de PE, seções, mitigação, recursos e imports;
2. execução direta: limites de segurança `--timeout 3 --cpu 3 --memory 512 --trace` em ambiente isolado;
3. `install`: tentativa de instalação com prefixos temporários isolados em `/tmp/` para pacotes e instaladores.

### Tabela de Resultados

| Aplicativo / Pacote | Análise (`--report`) | Execução (`run`) | Instalação (`install`) | Diagnóstico do Estado Atual |
|---|---|---|---|---|
| `7z_x64.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso funcional completo (CLI) |
| `7zFM_x64.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso sob display X11 (janela, toolbars, abas, menus); smoke sob investigação na R48 |
| `7z.dll` | exit 0 (100% imports) | — | — | DLL dependente válida |
| `winrar-x64-723.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso SFX |
| `WinRAR_x64.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso SFX |
| `putty_x64.exe` | exit 0 (100% imports) | exit 72 (`GuestTimeout`) | — | Banner e `SSH_MSG_KEXDH_INIT` validados localmente; timeout em loop ocioso sem transporte ativo |
| `notepad++.exe` | exit 0 (100% imports) | exit 0 | — | Inicialização headless e loop de mensagens válidos; smoke sob investigação na R48 |
| `Notepad++/notepad++.exe` | exit 0 (100% imports) | exit 0 | — | Inicialização headless válida |
| `Notepad++/updater/GUP.exe` | exit 0 (100% imports) | exit 255 (ExitProcess -1) | — | Sucesso na resolução PE64 + `libcurl.dll` side-by-side |
| `7-Zip/7zG.exe` | exit 0 (100% imports) | exit 0 | — | Operação gráfica do 7-Zip |
| `RTSS.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `RTSSHooks64.dll` | exit 0 (100% imports) | — | — | DLL x64 válida |
| `RobloxPlayerInstaller.exe` | exit 0 (100% imports) | exit 3 (`ExitProcess 3`) | exit 3 | Threads de worker inicializadas com stack reservation; interrupção no panic interno `RBXCRASH` |
| `Rockstar-Games-Launcher.exe` | exit 0 (100% imports) | Janela gráfica abre | — | Interface gráfica X11 abre via GDI+, botões e controles filhos registrados (avançou de exit 3 para GUI nas etapas R44–R47) |
| `Logitech_GHUB_x64.exe` | exit 0 (100% imports) | exit 1 (`ExitProcess 1`) | exit 1 | Descritores DACL validados; aborto por ausência de serviços de background Windows |
| `lghub_installer.exe` | exit 0 (100% imports) | exit 1 (`ExitProcess 1`) | exit 1 | Descritores DACL validados; aborto por ausência de serviços de background Windows |
| `7-Zip_x64_Installer.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 0 | Bootstrap 32-bit; payload PE64 registrado no install |
| `Notepad++_x64_Installer.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 0 | Bootstrap NSIS 32-bit; payload PE64 registrado no install |
| `Affinity x64.msix` | exit 0 (100% manifesto) | — | exit 0 | Sucesso na extração e cadastro do pacote MSIX de 1.45 GiB (R28) |
| `CapCut_*_installer.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `Creative_Cloud_Set-Up_7474.exe`| exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `EpicInstaller-*.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `Everything_Search_x64.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `CPU-Z_2.18_en.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `GPU-Z_2.70.0.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `HWMonitor_1.67.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `HWiNFO64.exe` | exit 0 (100% imports) | exit 4 (`map-failed: W^X`) | exit 0 (prepare) | Seções UPX tratadas estaticamente (R29); bloqueio por W^X na execução direta |
| `officedeploymenttool_*.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `RTSSSetup737.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 0 | Bootstrap 32-bit; payload PE64 registrado no install |
| `Rufus_x64.exe` | exit 0 (100% imports) | exit 4 (`map-failed: W^X`) | — | Seções UPX tratadas estaticamente (R29); bloqueio por W^X na execução direta |

---

### Detalhamento dos Erros por Causa-Raiz Técnica

#### 1. Rejeição Estrutural: Arquitetura 32-bit (x86 / Machine 0x14c) — Exit Code 5
- **Mensagem do Runtime:**
  ```text
  [tl][pe][error] parse-failed status="unsupported-architecture" detail="arquitetura de máquina 0x14c não suportada (esperado AMD64)"
  ```
- **Aplicativos afetados:** 12 binários (`CapCut`, `Creative Cloud`, `EpicInstaller`, `Everything Search`, `CPU-Z`, `GPU-Z`, `HWMonitor`, `Office Deployment`, `RTSS.exe`, e os bootstraps de instalação de `7-Zip`, `Notepad++` e `RTSS`).
- **Causa Técnica:** O cabeçalho COFF possui `Machine = 0x14c` (i386). O TradutorLinux tem como alvo estrito PE32+ (AMD64 0x8664). O comando `install` contorna instaladores cujo payload PE32+ seja descompactável para o prefixo, mas a execução direta do wrapper 32-bit é rejeitada por contrato.

#### 2. Packers / Seções UPX e Segurança W^X (`HWiNFO64.exe`, `Rufus_x64.exe`)
- **Análise Estática (Resolvida em R29):** O cabeçalho PE apontava diretórios de dados (exports em HWiNFO64 e `.pdata` em Rufus) para dentro de seções virtuais (`UPX0`, com `SizeOfRawData == 0`). O parser tolera diretórios opcionais não mapeados em disco sem falha de imagem malformada, alcançando 100% de resolução de imports (exit code 0).
- **Execução Direta (Bloqueio Controlado por W^X):** O entry point dessas imagens fica em páginas compactadas `UPX1` marcadas pelo packer com flags W+X (Read+Write+Execute). O runtime aplica estritamente a política W^X, mapeando essas seções como RW inicial e bloqueando a execução de código em memória gravável (`map-failed: entry point fora de uma página executável`, exit code 4).

#### 3. Limite de Pacote MSIX / AppX — Resolvido em R28
- **Status:** Resolvido na etapa R28. O parser ZIP64/MSIX suporta streaming de arquivos grandes até 4 GiB total e 2 GiB por entrada, permitindo extração integral e cadastro do `Affinity x64.msix` no catálogo de instalação com exit code 0.

#### 4. Executáveis do Portfólio Gráfico e Interativo
- **Rockstar-Games-Launcher.exe:**
  - **Evolução:** Nas etapas R44–R47, avançou de `ExitProcess(3)` para a abertura de janela gráfica X11 completa com inicialização de GDI+, controles de botões filhos e sincronização thread-safe via `g_gui_state_mutex`.
- **RobloxPlayerInstaller.exe:**
  - **Evolução:** Resolução de 100% de imports e suporte a `STACK_SIZE_PARAM_IS_A_RESERVATION` em `CreateThread` (R32). Interrupção no panic interno `RBXCRASH: FatalRuntimeError (RSL - panic: e374e9c-Worker,28)` quando workers de telemetria tentam conexões externas.
- **Logitech_GHUB_x64.exe / lghub_installer.exe:**
  - **Evolução:** Suporte a descritores DACL e diretórios padrão de DLLs (R30). Aborta com código 1 pela ausência de serviços de background (`advapi32!OpenSCManagerW`).
- **putty_x64.exe:**
  - **Evolução:** Validação de `SSH_MSG_KEXDH_INIT` via listener local próprio no smoke de teste (R25). Em execução direta isolada, aguarda conexão de rede em loop ocioso até o timeout do hospedeiro (`72`).

---

## Referências

- [PROJETO.md](PROJETO.md) — Missão, escopo e limites do produto;
- [docs/compatibilidade.md](docs/compatibilidade.md) — Matriz geral de compatibilidade;
- [docs/compatibilidade-runtime.md](docs/compatibilidade-runtime.md) — Contratos do runtime;
- [docs/arquitetura/aplicativos-bloqueios.md](docs/arquitetura/aplicativos-bloqueios.md) — Histórico de bloqueios e evidências de rede/GUI;
- [docs/arquitetura/unwinding-x64.md](docs/arquitetura/unwinding-x64.md) — Contrato de unwinding e C++ EH;
- [feitos/ROADMAP-R25-R47.md](feitos/ROADMAP-R25-R47.md) — Registro histórico das etapas concluídas R25 a R47;
- [feitos/ROADMAP-R1-R24.md](feitos/ROADMAP-R1-R24.md) — Registro histórico das etapas concluídas R1 a R24;
- [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md) — Histórico legado do projeto.
