# Catálogo de aplicativos — TradutorLinux

Este catálogo orienta a expansão para uma cobertura prática ampla: lista
aplicativos reais avaliados, agrupados por categoria e com nível de
compatibilidade. Nenhuma entrada isolada define a direção do runtime; o
portfólio prioriza capacidades compartilhadas por várias classes de uso. Não é
promessa de suporte universal; cada entrada é verificada via `tests/samples` ou
`tests/targets` com `--report` e execução isolada.

## Níveis

Os níveis abaixo são funcionais e independentes do `result` do `--report`:

- **analisado** — houve leitura de PE/imports, mas o entry point não foi executado;
- **inicia** — o entry point inicia e há resultado observável, sem validação de
  um fluxo representativo;
- **fluxo principal restrito** — um fluxo representativo passa, com limitações
  explícitas que impedem classificá-lo como uso geral;
- **fluxo principal** — o fluxo principal escolhido para o alvo passa com
  resultado verificável;
- **uso diário** — múltiplos fluxos, persistência e erros tratados;
- **cobertura avançada** — múltiplas DLLs, threads, rede ou recursos validados.

`result: supported` no `--report` significa somente `imports-resolved`. Ele não
eleva sozinho um aplicativo para qualquer nível funcional acima.

## Console

| Aplicativo | Versão | Imports | Estado | Fixture/Target |
|---|---|---|---:|---|
| `xxd` | vim v9.2.0957 | 73 | **fluxo principal** | `targetapp_xxd` `xxd.exe` 73/73 |
| `bzip2` | 1.0.8 | 69 | **fluxo principal** | `targetapp_bzip2` |
| `dos2unix`/`unix2dos` | 7.5.6 | 88 | **fluxo principal** | `targetapp_dos2unix` |

## Arquivos

| Aplicativo | Imports | Estado | Fixture |
|---|---|---|---|
| `tl_files_wide.exe` | KERNEL32 W | **fluxo principal** | Unicode, tempos, cópia |
| `tl_resources.exe` | KERNEL32 RCDATA | **fluxo principal** | recursos PE |

## Rede

| Aplicativo | Imports | Estado | Fixture |
|---|---|---|---|
| `tl_network_loopback.exe` | WS2_32 23 | **fluxo principal** | TCP/UDP localhost, `WSAPoll` |
| `tl_worker_rsl.exe` | WS2_32 + IPHLPAPI + CRYPT32 + WTSAPI32 | **fluxo principal restrito** | resolução local, interfaces IPv4, loja em memória e sessão WTS local |
| `tl_wininet.exe` | WININET 11 | **fluxo principal restrito** | HTTPS `localhost` com CA TLS efêmera; sem Internet ou proxy |

## GUI

| Aplicativo | Imports | Estado | Fixture |
|---|---|---|---|
| `tl_win.exe` | USER32 A 10 | **fluxo principal** | janela + message loop X11 |
| `tl_win_w.exe` | USER32 W 12 | **fluxo principal** | W wrappers via `wide_to_utf8` |
| `simple_todo` | 105 (GDI32/USER32/SHELL32/msvcrt) | **uso diário** | `targetapp_simple_todo_gui_smoke` `105/105` |
| `7zFM_x64.exe` | 298 | **shell visual experimental** | menu, toolbar, endereço, navegação e lista são desenhados; o menu MENUEX abre submenus aninhados e encaminha itens folha por `WM_COMMAND`; a lista mostra entradas imediatas do diretório do executável, limitada a 128 linhas, permite seleção, hover e abre pastas por Enter ou duplo clique; a árvore lateral oferece hover, retorna à raiz e seleciona diretórios Linux conhecidos; a barra de endereço navega somente dentro da raiz visual; a toolbar mostra hover e pressão; `Copy` (`546`) copia opt-in um arquivo selecionado para destino existente dentro da raiz, sem sobrescrever |

Evidência atual do `7zFM_x64.exe`: o runtime em `build/debug` abriu a janela
real no X11 com `800x600`; o trace registrou a normalização da geometria
inválida recebida do aplicativo e o shell desenhou as áreas de menu, toolbar,
endereço, navegação, lista e status. A lista foi alimentada pelo diretório que
contém o executável, sem recursão e com limite de 128 linhas; permite selecionar
entradas, destacar a linha sob o ponteiro e abrir pastas por Enter ou duplo clique;
a toolbar também destaca o botão sob o ponteiro; a árvore lateral destaca a
linha sob o ponteiro e permite
retornar à raiz visual e selecionar diretórios Linux conhecidos, e a barra de
endereço aceita caminhos `Z:\...` dentro dessa raiz. A validação complementar de GUI passou em `x11_popup_smoke` e
`runtime_gui_smoke` (2/2). Isso continua sendo uma validação visual
experimental, não suporte funcional do fluxo de compactação.

## Sistema

| Aplicativo | Imports | Estado | Fixture |
|---|---|---|---|
| `tl_toolhelp.exe` | KERNEL32 Toolhelp 10 | **fluxo principal** | `/proc` enumeração |
| `tl_shell.exe` | SHELL32 5 | **fluxo principal** | pastas conhecidas |
| `tl_com.exe` | ole32 9 | **fluxo principal** | `CoCreateInstance` `REGDB_E_CLASSNOTREG` |
| `tl_stream.exe` | ole32 1 + `IStream` | **fluxo principal restrito** | stream em memória, referências e round-trip |
| `tl_trust.exe` | WINTRUST 1 | **fluxo principal restrito** | cadeia DER explícita folha→raiz, raiz incorreta rejeitada |
| `tl_wthelper.exe` | WINTRUST 4 + CRYPT32 1 | **fluxo principal restrito** | estado WinTrust, signer e certificados folha/raiz, CN DER e fechamento |
| `tl_k32_gap.exe` | KERNEL32 11 | **fluxo principal** | seções críticas estendidas, ANSI e `FormatMessageA` |
| `tl_globalmem.exe` | KERNEL32 10 | **fluxo principal** | `GlobalAlloc`/lock e `LocalAlloc` com `ZEROINIT` |
| `tl_crypt32.exe` | CRYPT32 1 + KERNEL32 4 | **fluxo principal restrito** | nome subject/issuer de blob X.509 DER |

## Benchmark de cobertura

`RobloxPlayerInstaller.exe` `13M` `d156faf0c712d4ce26d95a596ad9b1dfc813021b5c422c93887b2522d8b01a59` `430` imports `17` DLLs. Evolução:
- `75/430 (17%)` inicial
- `196/430 (45%)` Fase 10–12
- `206/430 (47%)` LoadLibrary/Version/Locale
- `214/430 (49%)` Toolhelp
- `221/430 (51%)` GUI W
- `225/430 (52%)` SHELL32
- `240/430 (55%)` GDI estendido
- `244/430 (56%)` análise local de 2026-08-23
- `430/430 (100%)` resolução estática na coleta de 2026-09-04

A amostra está com `result: supported` para resolução de imports, mas a execução
comercial falhou de forma controlada em `RBXCRASH FatalRuntimeError Worker,28`
(`ExitProcess 3`). O instalador é um benchmark de cobertura, não um alvo
exclusivo: ele não recebe nível funcional até que um fluxo de instalação e
execução conclua. A lista completa de requisitos históricos e as reanálises ficam no
[registro unificado de requisitos](requisitos-aplicativos.md).

`lghub_installer.exe` (Logitech G HUB) resolve `114/114` imports no
`--report`, mas a execução em prefixo temporário expirou com `GuestTimeout 72`
durante a inicialização; portanto permanece sem fluxo funcional validado.

Novos benchmarks do portfólio popular x64 na Fase 13.13. Os percentuais abaixo
medem cobertura de resolução de imports; não significam, sozinhos, semântica
comportamental completa. Use o campo `runtime-support` do `--report` e os testes
de integração para avaliar o nível real de suporte:
- `winrar-x64-723.exe` (WinRAR 7.23 x64): **251/251 (100%)** imports resolvidos; smoke `sfxcmd` concluído
- `7z_x64.exe` (7-Zip CLI x64): **133/133 (100%)** imports resolvidos
- `7zFM_x64.exe` (7-Zip GUI): **298/298 (100%)** imports resolvidos; há um shell visual experimental para `7-Zip::FM` com menu MENUEX e submenus aninhados, listagem imediata do diretório do executável, seleção, hover, árvore lateral com hover, barra de endereço restrita à raiz e navegação visual por pastas via Enter ou duplo clique; `Copy` (`546`) possui cópia opt-in dentro da raiz, enquanto as demais operações ainda não estão ligadas
- `Rockstar-Games-Launcher.exe`: **338/338 (100%)** imports resolvidos
- `putty_x64.exe` (PuTTY SSH Client): **348/348 (100%)** imports resolvidos
- `notepad++.exe` (Notepad++ x64): **584/584 (100%)** imports resolvidos
- `Affinity x64.msix`: pacote MSIX / AppX reconhecido pelo parser de manifesto
- `HWiNFO64.exe`: **inicia**; `OpenPrinterW` retorna `ERROR_NOT_SUPPORTED` de forma controlada
- `Rufus_x64.exe`: **inicia**; o fluxo de uso não foi validado
- Wrappers 32-bit (NSIS/Inno): rejeitados com segurança pelo filtro de arquitetura x64
