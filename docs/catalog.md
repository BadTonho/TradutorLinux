# Catálogo de aplicativos — TradutorLinux

Este catálogo orienta a expansão para uma cobertura prática ampla: lista
aplicativos reais avaliados, agrupados por categoria e com nível de
compatibilidade. Nenhuma entrada isolada define a direção do runtime; o
portfólio prioriza capacidades compartilhadas por várias classes de uso. Não é
promessa de suporte universal; cada entrada é verificada via `tests/samples` ou
`tests/targets` com `--report` e execução isolada.

## Níveis

- **inicia** — PE32+ válido, imports resolvidos, entry point não executado (`unsupported` → `supported` no `--report`)
- **fluxo principal** — executa fluxo principal com saída byte-idêntica ou GUI smoke sob Xvfb
- **uso diário** — múltiplos fluxos, persistência e erros tratados
- **cobertura avançada** — múltiplas DLLs, threads, rede, recursos

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
| `tl_wininet.exe` | WININET 11 | **fluxo principal restrito** | HTTPS `localhost` com CA TLS efêmera; sem Internet ou proxy |

## GUI

| Aplicativo | Imports | Estado | Fixture |
|---|---|---|---|
| `tl_win.exe` | USER32 A 10 | **fluxo principal** | janela + message loop X11 |
| `tl_win_w.exe` | USER32 W 12 | **fluxo principal** | W wrappers via `wide_to_utf8` |
| `simple_todo` | 105 (GDI32/USER32/SHELL32/msvcrt) | **uso diário** | `targetapp_simple_todo_gui_smoke` `105/105` |

## Sistema

| Aplicativo | Imports | Estado | Fixture |
|---|---|---|---|
| `tl_toolhelp.exe` | KERNEL32 Toolhelp 10 | **fluxo principal** | `/proc` enumeração |
| `tl_shell.exe` | SHELL32 5 | **fluxo principal** | pastas conhecidas |
| `tl_com.exe` | ole32 9 | **fluxo principal** | `CoCreateInstance` `REGDB_E_CLASSNOTREG` |
| `tl_stream.exe` | ole32 1 + `IStream` | **fluxo principal restrito** | stream em memória, referências e round-trip |
| `tl_trust.exe` | WINTRUST 1 | **fluxo principal restrito** | cadeia DER explícita folha→raiz, raiz incorreta rejeitada |
| `tl_k32_gap.exe` | KERNEL32 11 | **fluxo principal** | seções críticas estendidas, ANSI e `FormatMessageA` |
| `tl_globalmem.exe` | KERNEL32 10 | **fluxo principal** | `GlobalAlloc`/lock e `LocalAlloc` com `ZEROINIT` |

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

Ainda `unsupported`, `execution: not-attempted`. O instalador é um benchmark de
cobertura, não um alvo exclusivo: lacunas restantes são priorizadas por API e
categoria no portfólio da Fase 13. A lista completa de imports estáticos que
faltam para essa amostra, e os próximos aplicativos analisados, ficam no
[registro unificado de requisitos](requisitos-aplicativos.md).

`lghub_installer.exe` (Logitech G HUB) agora resolve `114/114` imports no
`--report` após `tl_k32_gap.exe`, mas a execução isolada expirou em 20 segundos
sem produzir stdout ou arquivos; continua sem declaração de compatibilidade.
