# Roadmap atual — TradutorLinux

## Estado do documento

Este é o roadmap de trabalho atual do TradutorLinux. Ele define as próximas
etapas de desenvolvimento e estabilização do runtime a partir de 2026-09-19.

O histórico das etapas concluídas está preservado em:
- [feitos/ROADMAP-R1-R24.md](feitos/ROADMAP-R1-R24.md) — Auditoria e ciclo R1 a R24 (2026-09-12 a 2026-09-15);
- [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md) — Fases fundacionais e marcos legados.

As regras de escopo continuam rigorosas: nenhuma capacidade deve ser declarada
sem teste de integração e registro na matriz de compatibilidade em
[`docs/compatibilidade.md`](docs/compatibilidade.md).

## Regras de execução

Cada etapa deve produzir:

1. implementação mínima no runtime ou no fixture de teste;
2. teste unitário ou de robustez;
3. fixture ou integração com aplicativo-alvo quando a mudança afetar uma API
   Win32 observável;
4. atualização da documentação técnica na pasta `docs/` e da matriz de
   compatibilidade;
5. validação reproduzível no Linux x86-64;
6. um commit próprio antes do início da etapa seguinte.

Uma exportação resolvida nunca pode ser tratada como suporte funcional. Quando a
operação ainda não existir, o runtime deve retornar uma falha controlada,
atualizar o erro correspondente (`GetLastError`) e registrar a limitação no
diagnóstico.

---

## Fila de trabalho atual

### ~~R25~~ — Definir chave de host de teste e contrato de assinatura para o PuTTY ✓

**Contexto:** A etapa R24 concluiu a seleção nominal de algoritmos comuns do
`SSH_MSG_KEXINIT` entre o cliente PuTTY e o listener do smoke. O próximo passo do
transporte SSH exige que o servidor forneça uma chave de host válida para o
algoritmo negociado e prepare a troca Diffie-Hellman (`SSH_MSG_KEXDH_INIT`). O
fixture deve manter a execução controlada sem introduzir cifras reais,
autenticação ou código específico do PuTTY no runtime.

**Tarefas:**

- [x] definir uma chave de host de teste determinística compatível com os
  algoritmos comuns negociados em R24 (ex.: `rsa-sha2-256` ou `ssh-ed25519`);
- [x] estruturar a resposta do servidor do smoke para empacotar o blob de chave
  pública de host com enquadramento SSH válido;
- [x] receber e validar o primeiro pacote `SSH_MSG_KEXDH_INIT` (tipo 30) emitido
  pelo cliente PuTTY após o processamento da chave de host;
- [x] responder com encerramento controlado via `SSH_MSG_DISCONNECT` e verificar
  a exibição do diálogo de erro esperado;
- [x] atualizar os contratos em `docs/arquitetura/aplicativos-bloqueios.md` e a
  matriz em `docs/compatibilidade-aplicativos.md`.

**Critério de aceite:** ✓ O listener valida o banner, troca `KEXINIT` com os
algoritmos selecionados, aguarda o `SSH_MSG_KEXDH_INIT` do PuTTY com
`parse_kexdh_init_packet` (mpint `e` de 256–258 bytes), encerra com
`SSH_MSG_DISCONNECT` e observa o diálogo `PuTTY Fatal Error` no X11. Smoke
`putty_ssh_local_probe` (test 647) passou em 2026-09-19 reportando
`KEXDH_INIT received, guest-timeout 72 (limitation recorded)` — Evidência E46.

---

### ~~R26 — Despacho polimórfico e ampliação de cleanups C++ FH4~~ ✓ Concluído

**Contexto:** As etapas R6 e R7 implementaram decodificação host-side de
cabeçalhos FH4, despacho de catches tipados e execução do prefixo seguro de até
quatro cleanups de término observados no Notepad++. O aplicativo possui uma
cadeia mais longa de destruição observada (até 13 estados) que anteriormente acionava
o limite protetivo `fh4-cleanup-limit`.

**Tarefas:**

- [x] analisar os estados subsequentes da cadeia de cleanups do frame-alvo com
  fixture reproduzível de C++ EH e inspecionar o matching de `target_frame`;
- [x] validar com segurança os funclets de destruição seguintes, assegurando o
  alinhamento estrito de 16 bytes da pilha, a preservação de registradores não-voláteis
  e a delimitação ao `target_state` (`try_low`);
- [x] estender o limite seguro de cleanups de 4 para 16 ações (profundidade observada),
  mantendo a proteção contra ciclos infinitos e mapas corrompidos;
- [x] verificar a integridade da stack no retorno via `catchret` e a transição
  estável para o fluxo seguinte do Notepad++;
- [x] registrar os novos eventos e limites em `docs/arquitetura/unwinding-x64.md`,
  `docs/diagnostico-runtime.md`, `docs/compatibilidade-aplicativos.md` e `docs/compatibilidade.md`.

**Critério de aceite:** O smoke `notepadpp_fh4_headless_smoke` executa a
cadeia estendida de cleanups (`fh4-termination-cleanup`) sem atingir `fh4-cleanup-limit`, mantendo exit code
`0`, ausência de `guest-signal 71` e sem corrupção do frame do chamador (validado com CTest 635: Passed).

---

### R27 — Expansão do modelo de controles comuns Win32 (`SysListView32` / `COMCTL32`)

**Contexto:** O 7-Zip File Manager (`7zFM_x64.exe`) e outros utilitários Win32
dependem de controles comuns de lista para exibição e navegação de itens. O
runtime possui modelo lógico para `SysTreeView32`, toolbars e caixas de diálogo,
mas a interação avançada de listagem exige tratamento das mensagens básicas
`LVM_*` para evitar consultas sem resposta ou ponteiros nulos.

**Tarefas:**

- [ ] inventariar as mensagens `LVM_*` consumidas pelo 7-Zip File Manager no fluxo
  de visualização de pastas (`LVM_GETITEMCOUNT`, `LVM_GETITEMW`, `LVM_SETITEMSTATE`);
- [ ] implementar tratamento controlado dessas mensagens no controle lógico de
  lista do runtime (`src/runtime/gui_controls.cpp`);
- [ ] assegurar que cópias de texto e estruturas `LVITEMW` utilizem a primitiva
  segura de memória convidada (`write_guest_memory`);
- [ ] adicionar testes unitários cobrindo o controle em `tests/test_gui_controls.cpp`;
- [ ] atualizar a documentação de controles em `docs/arquitetura/gui-x11.md`.

**Critério de aceite:** O controle de lista responde às mensagens de contagem e
obtenção de item sem expor memória inválida do host; o smoke visual do 7-Zip sob
Xvfb continua terminando com sucesso (`exit 0`).

---

## Fora desta rodada

Continuam estritamente fora do escopo desta rodada:

- otimizações sem benchmark reproduzível;
- grandes refatorações estruturais ou reescrita de subsistemas estáveis;
- suporte a binários PE32 de 32 bits (x86), ARM ou WOW64;
- emulação de drivers de kernel, serviços Windows, anticheat e DirectX nativo;
---

## Auditoria de Erros e Bloqueios do Corpus Real (2026-09-19)

Executada via `./build/debug/src/tradutorlinux` sobre os 27 alvos do corpus
`Aplicativos_Windows_Populares/`. A rodada cobriu:
1. `--report`: análise estática de PE, seções, mitigação, recursos e imports;
2. execução direta: limites de segurança `--timeout 3 --cpu 3 --memory 512 --trace` em ambiente isolado;
3. `install`: tentativa de instalação com prefixos temporários isolados em `/tmp/` para pacotes e instaladores.

### Tabela de Resultados

| Aplicativo / Pacote | Análise (`--report`) | Execução (`run`) | Instalação (`install`) | Diagnóstico do Erro Principal |
|---|---|---|---|---|
| `7z_x64.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso funcional |
| `7zFM_x64.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso sob display / `CreateWindowExA` requer backend gráfico |
| `7z.dll` | exit 0 (100% imports) | — | — | DLL dependente válida |
| `winrar-x64-723.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso SFX |
| `WinRAR_x64.exe` | exit 0 (100% imports) | exit 0 | — | Sucesso SFX |
| `putty_x64.exe` | exit 0 (100% imports) | exit 72 (`GuestTimeout`) | — | `clock_nanosleep` timeout aguardando transporte de rede/diálogo |
| `notepad++.exe` | exit 0 (100% imports) | exit 0 | — | Inicialização headless válida / `CreateWindowExA` requer display |
| `Notepad++/notepad++.exe` | exit 0 (100% imports) | exit 0 | — | Inicialização headless válida |
| `Notepad++/updater/GUP.exe` | exit 0 (100% imports) | exit 255 (ExitProcess -1) | — | Sucesso na resolução PE64 + libcurl.dll side-by-side |
| `7-Zip/7zG.exe` | exit 0 (100% imports) | exit 0 | — | Operação gráfica do 7-Zip |
| `RTSS.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `RTSSHooks64.dll` | exit 0 (100% imports) | — | — | DLL x64 válida |
| `RobloxPlayerInstaller.exe` | exit 0 (100% imports) | exit 3 (`ExitProcess 3`) | exit 3 | `RBXCRASH: FatalRuntimeError (RSL - panic: e374e9c-Worker,28)` |
| `Rockstar-Games-Launcher.exe` | exit 0 (100% imports) | exit 3 (`ExitProcess 3`) | — | `ExitProcess(3)` explícito do convidado |
| `Logitech_GHUB_x64.exe` | exit 0 (100% imports) | exit 1 (`ExitProcess 1`) | exit 1 | `failed stage="setup" exit-code="1"` |
| `lghub_installer.exe` | exit 0 (100% imports) | exit 1 (`ExitProcess 1`) | exit 1 | `failed stage="setup" exit-code="1"` |
| `7-Zip_x64_Installer.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 0 | Bootstrap 32-bit; payload PE64 registrado no install |
| `Notepad++_x64_Installer.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 0 | Bootstrap NSIS 32-bit; payload PE64 registrado no install |
| `Affinity x64.msix` | exit 4 (`malformed`) | exit 4 | exit 4 | `failed stage="package-parse"`: limite descompactado de 512 MiB |
| `CapCut_*_installer.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `Creative_Cloud_Set-Up_7474.exe`| exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `EpicInstaller-*.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `Everything_Search_x64.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `CPU-Z_2.18_en.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `GPU-Z_2.70.0.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `HWMonitor_1.67.exe` | exit 5 (`unsupported-arch`) | exit 5 | — | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `HWiNFO64.exe` | exit 4 (`malformed`) | exit 4 | — | `parse-failed status="malformed" detail="diretório de exports fora da imagem"` |
| `officedeploymenttool_*.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 5 | Arquitetura 32-bit x86 (`0x14c`) não suportada |
| `RTSSSetup737.exe` | exit 5 (`unsupported-arch`) | exit 5 | exit 0 | Bootstrap 32-bit; payload PE64 registrado no install |
| `Rufus_x64.exe` | exit 4 (`malformed`) | exit 4 | — | `parse-failed status="malformed" detail="diretório de exceções fora da imagem (RVA 0xc5000 size 0x4ae8)"` |

---

### Detalhamento dos Erros por Causa-Raiz Técnica

#### 1. Rejeição Estrutural: Arquitetura 32-bit (x86 / Machine 0x14c) — Exit Code 5
- **Mensagem do Runtime:**
  ```text
  [tl][pe][error] parse-failed status="unsupported-architecture" detail="arquitetura de máquina 0x14c não suportada (esperado AMD64)"
  ```
- **Aplicativos afetados:** 12 binários (`CapCut`, `Creative Cloud`, `EpicInstaller`, `Everything Search`, `CPU-Z`, `GPU-Z`, `HWMonitor`, `Office Deployment`, `RTSS.exe`, e os bootstraps de instalação de `7-Zip`, `Notepad++` e `RTSS`).
- **Causa Técnica:** O cabeçalho COFF possui `Machine = 0x14c` (i386). O TradutorLinux tem como alvo estrito PE32+ (AMD64 0x8664). O comando `install` consegue contornar instaladores cujo payload PE32+ seja descompactável para o prefixo, mas a execução direta do wrapper 32-bit é rejeitada por contrato.

#### 2. Rejeição de Packers / Seções UPX Anômalas — Exit Code 4
- **HWiNFO64.exe:**
  ```text
  [tl][pe][error] parse-failed status="malformed" detail="diretório de exports fora da imagem"
  ```
  - **Causa:** O cabeçalho PE aponta o diretório de exports para um RVA pertencente à seção virtual `UPX0` (`SizeOfRawData = 0`), sem bytes físicos no arquivo. O leitor rejeita com segurança para evitar dereferência nula.
- **Rufus_x64.exe:**
  ```text
  [tl][pe][error] parse-failed status="malformed" detail="diretório de exceções fora da imagem (RVA 0xc5000 size 0x4ae8)"
  ```
  - **Causa:** Tabela `.pdata` aponta para RVA virtual `0xc5000` em seção sem dados no arquivo e seção `UPX1` marcada com permissões W+X simultâneas, violando a política de memória W^X.

#### 3. Limite de Pacote MSIX / AppX — Exit Code 4
- **Affinity x64.msix:**
  ```text
  [tl][install][error] failed stage="package-parse" prefix="/tmp/.../pfx" app-id="test_affinity_x64"
  erro: pacote MSIX / AppX inválido ou não suportado
  ```
  - **Causa:** O parser ZIP64/MSIX impõe um limite máximo de segurança descompactado de 512 MiB para contenção contra abusos de descompressão (ZIP bomb). O arquivo do Affinity excede esse limite e requer streaming ou ajuste do validador.

#### 4. Erros de Inicialização e Término do Convidado (Exit Codes 3 e 1)
- **RobloxPlayerInstaller.exe:**
  - **Relatório de Imports:** 100% resolvidos (430/430).
  - **Erro observado:**
    ```text
    RBXCRASH: FatalRuntimeError (RSL - panic: e374e9c-Worker,28)
    [tl][runtime][info] ExitProcess symbol="ExitProcess" exit-code="3" status="success" mechanism="guest-transfer"
    [tl][install][error] failed stage="setup" exit-code="3"
    ```
  - **Causa:** O worker interno do instalador da Roblox falha ao consultar componentes de sessão/rede ou slots de TLS específicos, disparando o panic `RBXCRASH` interno do binário Windows.
- **Rockstar-Games-Launcher.exe:**
  - **Relatório de Imports:** 100% resolvidos (338/338).
  - **Erro observado:** Encerramento imediato do entry point com `ExitProcess(3)` sem mensagem adicional de erro.
- **Logitech_GHUB_x64.exe / lghub_installer.exe:**
  - **Relatório de Imports:** 100% resolvidos (114/114).
  - **Erro observado:**
    ```text
    [tl][runtime][info] ExitProcess symbol="ExitProcess" exit-code="1" status="success" mechanism="guest-transfer"
    [tl][install][error] failed stage="setup" exit-code="1"
    ```
  - **Causa:** O instalador detecta falta de serviços de background (`advapi32!OpenSCManagerW` / gerenciador de serviços) ou caminhos de diretório de instalação não inicializados e aborta com código 1.

#### 5. Dependências Dinâmicas Ausentes / Lado a Lado — Resolvido
- **Notepad++/updater/GUP.exe:**
  - **Status:** 100% resolvido (153/153 imports).
  - **Resolução Técnica:** O validador de imports (`loader::inspect_imports`) agora inspeciona DLLs convidadas presentes lado a lado no diretório do executável (`libcurl.dll`). Com as exportações de criptografia (`Normaliz.dll`, `bcrypt.dll`, `WLDAP32` e `CRYPT32`), a cadeia de `libcurl.dll` e `GUP.exe` resolve integralmente tanto em `--report` quanto em execução direta.

#### 6. Timeout de Conexão e Espera de Mensagens — Exit Code 72
- **putty_x64.exe:**
  - **Erro observado:**
    ```text
    [tl][process][error] terminated category="guest-timeout" timeout-ms="3000" timeout-samples="13" timeout-pe-samples="0" timeout-host-samples="13" host-symbol="clock_nanosleep"
    ```
  - **Causa:** Em execução direta sem servidor SSH local ou sem interação GUI, a thread principal entra no loop ocioso de espera (`clock_nanosleep` / `GetMessageA`) até o timeout do hospedeiro (`72`).

---

## Referências

- [PROJETO.md](PROJETO.md) — Missão, escopo e limites do produto;
- [docs/compatibilidade.md](docs/compatibilidade.md) — Matriz geral de compatibilidade;
- [docs/compatibilidade-runtime.md](docs/compatibilidade-runtime.md) — Contratos do runtime;
- [docs/arquitetura/aplicativos-bloqueios.md](docs/arquitetura/aplicativos-bloqueios.md) — Histórico de bloqueios e evidências de rede/GUI;
- [docs/arquitetura/unwinding-x64.md](docs/arquitetura/unwinding-x64.md) — Contrato de unwinding e C++ EH;
- [feitos/ROADMAP-R1-R24.md](feitos/ROADMAP-R1-R24.md) — Registro das etapas concluídas R1 a R24;
- [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md) — Histórico legado do projeto.
