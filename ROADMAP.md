# Roadmap do TradutorLinux

Este arquivo acompanha a execução do projeto. O documento de visão, escopo e arquitetura está em [PROJETO.md](PROJETO.md).

## Como usar este roadmap

Cada fase só deve avançar quando seus critérios de saída estiverem atendidos. Uma API nova entra no projeto apenas quando existir um teste ou aplicativo-alvo que justifique seu comportamento.

Os itens marcados como concluídos devem ter evidência no repositório: código, teste, documentação ou um artefato reproduzível. O roadmap descreve ordem de dependências, não uma promessa de prazo.

## Stack decidido

- **Linguagem principal:** C++20.
- **C:** estruturas PE, interfaces C e trechos que precisem de ABI simples.
- **Assembly x86-64:** somente trampolins, bootstrap ou outras fronteiras que não possam ser expressas com segurança pelo compilador.
- **Build:** CMake + Ninja.
- **Hospedeiro inicial:** Linux x86-64.
- **Binários de teste:** PE32+ x86-64 produzidos com `mingw-w64`.

## Estado atual

- **Fase atual:** Fase 11 — Concorrência e rede opcional.
- **Marco concluído:** a Fase 7 foi validada de ponta a ponta e a decisão de produto foi tomada: **seguir com a GUI Win32 mínima como objetivo experimental**. `tl_gui.exe` abriu a janela X11, recebeu o clique em OK e encerrou com código `0`; `tl_win.exe` criou uma janela real e executou um message loop completo (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `PostQuitMessage`), encerrando via `WM_CLOSE`/autoclose com código `0`; o modo `--report` lista imports suportados sem executar o PE; `tl_hello`, `tl_echo` e `tl_file` têm regressões e limitações publicadas na matriz.
- **Marco concluído:** o smoke test de GUI passou a ter cobertura automática em CI. O teste `runtime_gui_smoke` sobe um `Xvfb` próprio e executa `tl_win.exe` de ponta a ponta em dois cenários: autoclose (message loop encerra sozinho via `WM_QUIT`) e fechamento real por `WM_DELETE_WINDOW` (mesmo `ClientMessage` do botão de fechar do WM), exigindo exit-code `1`, stdout vazio e os eventos esperados no trace. A conexão X11 do runtime é fechada no teardown (`DisplayCloser`), validado sob ASAN com `detect_leaks=1`.
- **Marco concluído:** `CreateWindowExA` agora despacha `WM_CREATE` ao `WNDPROC` do convidado antes de devolver o `HWND` (retorno `-1` aborta a criação e devolve `NULL`). A fixture `tl_win.c` marca uma flag no `WM_CREATE` e propaga no exit code via `PostQuitMessage`, então o `runtime_gui_smoke` prova o despacho exigindo exit-code `1`.
- **Marco concluído:** o message loop ganhou entrada real de teclado. `KeyPress` X11 vira `WM_KEYDOWN` (virtual key: letras em maiúsculas) com o caractere guardado; `TranslateMessage` converte em `WM_CHAR` enfileirado (entregue antes dos próximos eventos X11) e registra o evento de trace `TranslateMessage message="WM_CHAR" wparam status="translated"`. A fixture `tl_win.c` encerra a janela ao receber `WM_CHAR('q')`, e o terceiro cenário do `runtime_gui_smoke` envia um `KeyPress` sintético sob `Xvfb` e exige exit-code `3`. O teste agora sobe sempre um `Xvfb` próprio (sem window manager): com WM a janela é reparentada e o `XSendEvent` para o frame não chega ao cliente.
- **Marco concluído:** o pump passou a usar fila de eventos por janela. Todos os eventos X11 pendentes são demultiplexados para a fila da janela-alvo a cada consulta, então nada se perde entre janelas independentemente da ordem do message loop. A fixture `tl_win2.exe` cria duas janelas simultâneas com `WNDPROC`s independentes ("Janela A" e "Janela B"); o quarto cenário do `runtime_gui_smoke` envia `KeyPress 'q'` à A e `'k'` à B e exige exit-code `15` (flags 1+2+4+8), provando o roteamento independente por janela.
- **Marco concluído:** diagnóstico de falhas com isolamento em processo filho (ideia §1). O convidado agora executa em um processo filho (`fork`/`waitpid`); o pai prepara o PE, o mapeamento e os imports, e o filho só executa o entry point e reporta o resultado por um pipe antes de `_exit`. Sinais fatais do filho são restaurados para `SIG_DFL` para que um `SIGSEGV` do convidado não seja engolido por handlers do hospedeiro (ex.: AddressSanitizer). O pai distingue saída normal de término por sinal: no término por sinal emite `[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória"` e retorna `71` (`GuestFault`), novo código de saída do hospedeiro. A fixture `tl_crash.exe` (sem imports) acessa o endereço `0` e valida o diagnóstico no preset `sanitize` (onde o ASan interceptaria o sinal sem o reset), no `debug` e no `release`; o exit code do convidado continua propagado integralmente pelo pipe (não truncado pelo status POSIX).
- **Marco concluído:** a Fase 8 começou com a definição dos primeiros aplicativos-alvo reais, de código aberto e compilados em CI: `xxd` (vim `v9.2.0957`), `bzip2` (`1.0.8`) e `dos2unix`/`unix2dos` (`7.5.6`). O módulo `tests/targets` baixa as fontes pinadas por hash SHA-256, faz o cross-build com `mingw-w64` (opção `TL_BUILD_TARGET_APPS=ON`, job `target-apps` do CI) e protege os imports reais em manifests via `llvm-readobj` e `--report` (8 testes, label `targetapp`). Nenhum alvo executava ainda: todos importam `msvcrt.dll` (fora de escopo até a Fase 9) e o `--report` os classificava como `result: unsupported` / `execution: not-attempted`. Os imports capturados (xxd: 73 símbolos; bzip2: 69; dos2unix/unix2dos: 88, incluindo `SHELL32.dll!CommandLineToArgvW`) guiam o subconjunto mínimo de CRT da Fase 9.
- **Marco concluído:** o subconjunto mínimo de `msvcrt.dll` foi implementado e registrado (57 símbolos, ordinais 1–57), junto com as 14 APIs de `KERNEL32.dll` que o CRT interno do mingw e o `xxd.exe` exigem (`VirtualQuery`/`VirtualProtect` via `/proc/self/maps` + `mprotect`, `MultiByteToWideChar`/`WideCharToMultiByte` com CP 0/1252/65001, critical sections no-op para convidado single-thread, `TlsGetValue`, `GetConsoleMode`/`SetConsoleMode`, `Sleep`, `SetUnhandledExceptionFilter`, `IsDBCSLeadByteEx`). O `--report` do `xxd.exe` passou a `result: supported`.
- **Marco concluído:** `xxd.exe` executa de ponta a ponta com saída **byte-idêntica** ao `xxd` do sistema (exit `0`). Para isso a fronteira agora aloca um TEB de uma página e aponta o segmento `%gs` via `arch_prctl(ARCH_SET_GS)` durante a execução do convidado (o mingw lê `%gs:[0x30]` no `__mingw_CRTStartup`), restaurando o `GS` e liberando o TEB em seguida. O teste e2e fixa um ouro em `tests/targets/golden/xxd/` (entrada de 4880 bytes que cruza a coluna de offset em `0x1000`) e verifica em CTest: modo padrão, `-p` (plain) e caminho de erro (arquivo inexistente → exit `2`, stderr não vazio), 3 testes novos com label `targetapp`. As conversões de código de página, `VirtualQuery`/`VirtualProtect`, `TlsGetValue`, critical sections e o subconjunto de CRT têm 42 testes unitários novos (`test_win32.cpp`, `test_msvcrt.cpp`). Total: 180 testes verdes no preset com alvos; 169 em `debug`, `release` e `sanitize`.
- **Marco concluído:** `bzip2.exe` executa de ponta a ponta nos dois sentidos. `__iob_func()` devolve o ponteiro do array `GuestFile` (o convidado indexa `[0..2]` com `sizeof(_iobuf)` = 48 bytes — o argumento `rcx` é ignorado, como no CRT MSVC clássico) e `_stat64` passou de stub `ENOSYS` para implementação real que preenche o `struct _stat64` do MinGW (pack 8, `st_mode` em `0x06`, tamanho 56 bytes) via `stat()` do host; a verificação `S_ISREG` do bzip2 (`testb $0x40, +7`) depende dos bits de tipo do Linux, idênticos aos do Windows (`S_IFREG = 0x8000`). Compressão (`-c`) e descompressão (`-d`, `-d -c`) de arquivo são byte-idênticas ao `bzip2` nativo; e2e em CTest (ouro em `tests/targets/golden/bzip2/` e `golden/bzip2_decompress/`, comparação binária via `verify_target_run_bytes.cmake`), 2 testes novos com label `targetapp`, e 3 testes unitários novos para `_stat64` (`test_msvcrt.cpp`). Total: 265 testes verdes no preset com alvos; 218 em `debug` e `sanitize`.
- **Marco concluído:** `tl_thread.exe` fecha a validação do subconjunto atual de concorrência. O fixture cria duas threads convidadas sequenciais, cada uma escreve `Thread done`, termina via `ExitThread` e é aguardada por `WaitForSingleObject`; a thread principal escreve `Main done` e encerra com código `0`. Metadata e execução passam em Debug, Release e Sanitize (`LSAN_OPTIONS=detect_leaks=0`); a regressão é coberta por `fixture_tl_thread_metadata` e `runtime_tl_thread_matches_readobj`. Eventos, mutexes adicionais e WinSock continuam condicionais a novos aplicativos-alvo.
- **Próximo resultado observável:** `dos2unix`/`unix2dos` dependem de `SHELL32.dll!CommandLineToArgvW` e de dezenas de APIs novas de `msvcrt.dll` (Fase 9+).

## Fase 0 — Fundação e contrato

- [x] Criar a estrutura CMake, compilação com warnings rigorosos e testes automatizados.
- [x] Fixar o alvo: Linux x86-64 hospedando somente PE32+ x86-64.
- [x] Definir formato do trace, códigos de erro e matriz de compatibilidade.
- [x] Criar binários de teste próprios, incluindo um executável sem CRT para o primeiro salto ao entry point.
- [x] Documentar as convenções Microsoft x64 e System V AMD64 usadas em cada fronteira.
- [x] Configurar sanitizers e análise estática para os testes quando possível.

### Critério de saída — atendido

O projeto compila de forma reproduzível, executa seus testes básicos e possui fixtures Windows versionadas com seus imports documentados.

## Fase 1 — Leitor de PE seguro

- [x] Ler e validar DOS header, NT headers, optional header e section headers.
- [x] Exibir seções, entry point, imports, relocations e arquitetura.
- [x] Rejeitar PE inválido, truncado ou de arquitetura incompatível com mensagens precisas.
- [x] Cobrir o parser com testes unitários e corpus de arquivos malformados.
- [x] Comparar a saída com `llvm-readobj` nas fixtures geradas.

### Critério de saída

O leitor identifica corretamente os fixtures válidos e nunca acessa memória fora dos limites ao processar fixtures inválidos. Validação: fixtures `tl_hello.exe`/`tl_nop.exe` parseados e comparados com `llvm-readobj` em CTest, corpus malformado coberto por testes, presets `debug` e `sanitize` verdes e análise estática sem pendências.

## Fase 2 — Mapeamento de imagem

- [x] Reservar a imagem no endereço preferencial quando possível.
- [x] Copiar headers e seções, respeitando alinhamentos e permissões de página.
- [x] Aplicar base relocations para PE32+ x86-64.
- [x] Validar o mapeamento com executáveis mínimos que ainda não chamam APIs.
- [x] Garantir que a imagem não permaneça inteira com permissão RWX por conveniência.

### Critério de saída

Um executável mínimo pode ser mapeado e inspecionado pelo runtime sem executar funcionalidades fora do escopo.

Validação: `tl_nop.exe`, `tl_hello.exe` e `tl_reloc.exe` mapeados via CLI em `debug` e `sanitize`; relocations aplicadas e verificadas em memória mapeada quando a base difere da preferencial (ASan bloqueia `0x140000000` no preset `sanitize`); permissões de região verificadas via `/proc/self/maps` (headers `r--`, `.text` `r-x`, nunca `rwx`); presets `debug` e `sanitize` verdes e análise estática sem pendências. O contrato de mapeamento está em `docs/arquitetura/mapeamento-imagem.md`.

## Fase 3 — Imports e bootstrap mínimo

- [x] Resolver a import table para módulos internos suportados.
- [x] Implementar trampolins e ponte de ABI para chamadas do programa à camada hospedeira.
- [x] Preparar as estruturas mínimas de processo e thread exigidas pelo escopo inicial.
- [x] Adicionar diagnóstico para DLL, símbolo, ordinal, forwarder ou delay import ausente.
- [x] Definir o comportamento de falha antes do entry point quando uma dependência não for suportada.

### Critério de saída — atendido

O runtime resolve imports conhecidos com o ABI correto e informa de maneira reproduzível qualquer dependência desconhecida.

Validação: registro interno de `KERNEL32.dll`, `USER32.dll` e `GDI32.dll`; resolução por nome e ordinal com patch da IAT e restauração das permissões; fronteiras `ms_abi` testadas diretamente; processo convidado com pilha de 1 MiB e guard page; fixture `tl_missing_dll.exe` retorna `5` sem executar o entry point e emite `unknown-symbol`; falhas de símbolo, ordinal, símbolo sem implementação, delay import e slot de IAT inválido têm testes unitários. Os testes Sanitizer locais exigem `LSAN_OPTIONS=detect_leaks=0` porque a descoberta do GoogleTest falha no LeakSanitizer sob ptrace.

## Fase 4 — Console: primeiro marco público

- [x] Implementar `GetStdHandle`, `WriteFile`, `ReadFile` e `ExitProcess`.
- [x] Definir e testar conversão entre handles Windows e descritores Linux.
- [x] Executar `tl_hello.exe` e uma ferramenta de eco construída no repositório.
- [x] Verificar saída, retorno, trace e tratamento de erros em CI.
- [x] Documentar exatamente quais flags, handles e encodings são suportados.

### Critério de saída — MVP atendido

```text
./tradutorlinux --trace tests/samples/tl_hello.exe
```

O comando escreve a saída esperada, retorna o código correto, produz trace reproduzível e possui testes para todas as APIs usadas pelo fixture.

Validação: `tl_hello.exe` e `tl_echo.exe` executados com stdout verificado; `ExitProcess` e o código de retorno registrados no trace; handles padrão convertidos para descritores Linux por tokens opacos; `ReadFile`/`WriteFile` limitados a I/O síncrono de console e bytes sem conversão de encoding; 95 testes passaram nos presets `debug` e `sanitize` (sanitize local com `LSAN_OPTIONS=detect_leaks=0` por limitação do LeakSanitizer durante descoberta sob ptrace); `cppcheck` e `clang-tidy` passaram.

## Fase 5 — Runtime básico

- [x] Implementar `GetLastError`/`SetLastError` e o mapeamento de erros necessário.
- [x] Implementar `VirtualAlloc`/`VirtualFree` com semântica limitada e documentada.
- [x] Implementar abertura, leitura, escrita e fechamento de arquivos para um subconjunto de flags.
- [x] Definir normalização de caminhos e política explícita para caminhos Windows.
- [x] Adicionar testes de concorrência somente quando o modelo de threads fizer parte do escopo.

### Critério de saída — atendido

Uma aplicação de console consegue ler e escrever arquivos e usar memória alocada pelo runtime, com erros verificáveis e documentados.

Validação: `tl_file.exe` usa `VirtualAlloc`, `CreateFileA`, `WriteFile`, `ReadFile`, `CloseHandle`, `VirtualFree`, `GetLastError` e `SetLastError`; caminhos relativos são normalizados de `\\` para `/`, enquanto caminhos absolutos e drives são rejeitados; `VirtualAlloc` aceita somente `MEM_COMMIT | MEM_RESERVE` com `PAGE_READONLY` ou `PAGE_READWRITE`, e `VirtualFree` somente `MEM_RELEASE` com tamanho zero. Debug e sanitize passaram com 101 testes; `cppcheck`, `clang-tidy` e `git diff --check` também passaram.

## Fase 6 — Carregamento e cobertura controlada

- [x] Adicionar APIs somente guiadas por aplicações-alvo e testes de regressão.
- [x] Evoluir suporte a DLLs, resources, TLS callbacks, forwarders e delay-load conforme necessário.
- [x] Publicar uma matriz com aplicativo, arquitetura, imports, APIs usadas, estado e limitações.
- [x] Adicionar um modo de relatório que mostre o que falta para tentar executar um `.exe`.
- [x] Revisar periodicamente o custo de cada API em relação ao valor para os aplicativos-alvo.

### Critério de saída — atendido

Cada aplicação declarada como suportada possui um teste de regressão e uma lista explícita de limitações.

Validação: `tl_hello.exe`, `tl_echo.exe` e `tl_file.exe` possuem testes de integração e entradas na matriz; `tl_missing_dll.exe` protege o caminho de rejeição; `--report` tem teste unitário e CTest real, retorna `0` para `tl_file.exe`, lista cada import e declara `execution: not-attempted`. O modo não mapeia nem executa a imagem.

## Fase 7 — Avaliar GUI

- [x] Decidir que uma interface Win32 mínima é um objetivo experimental de produto.
- [x] Criar um subsistema de janela e eventos separado do runtime de console.
- [x] Começar por `MessageBoxA` e uma janela simples, com fixture PE32+ e teste automatizado de metadata/report.
- [x] Definir a integração inicial com X11 direto, mantendo a camada isolada para futura decisão sobre Wayland/toolkit.
- [x] Implementar um subconjunto mínimo de janela e eventos (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage`) com fixture `tl_win.exe` que cria janela, desenha texto e encerra ao fechar (`WM_CLOSE`).
- [x] Provar a fronteira de ABI host→convidado invocando o `WNDPROC` do convidado pela convenção Microsoft x64.
- [x] Estender o teclado: `WM_KEYUP`, virtual keys por `keysym` (incluindo teclas sem caractere), `Shift` refletido no `WM_CHAR` (fixture `tl_key.exe`).
- [x] Implementar `SetTimer`/`KillTimer` com despacho periódico de `WM_TIMER` no pump (fixture `tl_timer.exe`).
- [x] Implementar o GDI mínimo (`GDI32.dll!GetStockObject`/`TextOutA`; `USER32.dll!BeginPaint`/`EndPaint`/`GetDC`/`ReleaseDC`) com `HDC == HWND` e `PAINTSTRUCT` real (fixture `tl_gdi.exe`).

Validação local: os 129 testes dos presets `debug`, `sanitize` e `release` passam quando o ambiente fornece X11/Xvfb; sem acesso a `/tmp/.X11-unix`, o `runtime_gui_smoke` é marcado como `Skipped` rapidamente. A suíte inclui `tl_win.exe`, `tl_win2.exe`, `tl_key.exe`, `tl_timer.exe`, `tl_gdi.exe` e `tl_paint.exe`, resolução dos imports de `USER32.dll`/`GDI32.dll`, relatório sem execução, testes de layout ABI e relocations PE32+. O loader valida o entry point, usa a pilha convidada com guard page, aplica somente `DIR64`/`ABSOLUTE` para PE32+ e rejeita execução fora da base quando não há relocations. O `runtime_gui_smoke` usa `Xvfb -displayfd`, não depende de displays fixos e aborta/limpa o servidor de forma controlada. `cppcheck` e `clang-tidy` passam sem pendências.

### Critério de saída — atendido

Uma aplicação gráfica de teste cria uma janela, recebe eventos básicos e encerra corretamente, sem comprometer o runtime de console.

Validação visual (2026-08-15, sessão X11 `DISPLAY=:0` acessível): `tl_gui.exe` executado com `--trace` abriu a janela "TradutorLinux GUI / Fase 7" com botão OK; ao clicar, o runtime registrou `[tl][runtime][info] ExitProcess exit-code="0" status="success" mechanism="guest-transfer"`, `[tl][process][info] exit exit-code="0" explicit="sim"` e encerrou com código `0`, liberando a imagem. `tl_win.exe` executado com `--trace` e `TL_GUI_AUTOCLOSE_MS=1` registrou `RegisterClassExA`, `CreateWindowExA`, `GetMessageA message="WM_QUIT"` e `ExitProcess exit-code="0"`, confirmando o message loop de ponta a ponta (autoclose → `WM_CLOSE` → `DefWindowProcA` → `DestroyWindow` → `WM_DESTROY` → `PostQuitMessage(0)`).

## Fase 8 — Aplicativos-alvo reais

O projeto deixa de medir progresso apenas por fixtures e passa a medir por
aplicativos pequenos, úteis e reproduzíveis. Cada aplicativo-alvo deve ser
fixado por versão, arquitetura, toolchain e lista de imports.

- [x] Definir de 3 a 5 aplicativos-alvo reais, preferencialmente de código aberto e compiláveis no CI.
- [x] Priorizar utilitários de console: ferramentas de texto, arquivos, configuração e empacotamento simples.
- [x] Criar teste por aplicativo com stdout, stderr, exit code, arquivos produzidos e timeout.
- [x] Fazer o `--report` agrupar imports ausentes por DLL e por fase.
- [x] Separar "não suportado", "falhou durante a execução" e "resultado incorreto".
- [x] Publicar pontuação de compatibilidade por aplicativo; iniciar não é suficiente.

### Critério de saída

Pelo menos três aplicativos reais, pequenos e úteis executam um fluxo completo
de teste no Linux, com limitações publicadas e regressão automatizada. Fixtures
continuam obrigatórias para proteger contratos de ABI, mas deixam de ser a
única evidência do produto.

## Fase 9 — Base de processo e CRT

A maior barreira para aplicativos compilados normalmente será a camada de
runtime C e o estado básico do processo. Esta fase é guiada pelos imports dos
aplicativos escolhidos.

- [x] Implementar `GetModuleHandleA/W`, `GetProcAddress` limitado aos módulos registrados e informações básicas do processo.
- [x] Implementar linha de comando e ambiente: `GetCommandLineA/W`, `GetEnvironmentVariableA/W` e conversão documentada de encoding.
- [x] Implementar heap básico: `HeapAlloc`, `HeapFree`, `HeapReAlloc`, `GetProcessHeap`.
- [x] Implementar a espera exigida pelo primeiro alvo: `Sleep`.
- [x] Implementar tempo adicional quando um aplicativo-alvo justificar: `GetTickCount64` e `GetSystemTimeAsFileTime`.
- [x] Adicionar o subconjunto mínimo de `msvcrt.dll` exigido pelo primeiro alvo (`xxd`).
- [x] Expandir a CRT somente pelos imports e fluxos exigidos pelos próximos aplicativos-alvo.
- [x] Cobrir inicialização/encerramento do CRT, argumentos `argc/argv`, retorno de `main` e erros para o primeiro alvo.
- [ ] Ampliar essa cobertura para cada nova família de CRT ou aplicativo suportado.

### Critério de saída

Ao menos dois aplicativos compilados com CRT executam seus fluxos principais,
recebem argumentos e retornam seus códigos corretamente.

## Fase 10 — Sistema de arquivos e utilitários

Expandir a camada para programas que trabalham com diretórios, configuração e
arquivos, mantendo uma tradução de caminhos segura e explícita.

- [x] Implementar `FindFirstFileA/W`, `FindNextFileA/W` e `FindClose`.
- [x] Implementar `GetFileAttributesA/W`, `DeleteFileA/W`, `MoveFileA/W` e `CreateDirectoryA/W`.
- [x] Implementar `SetFilePointer`, tamanhos de arquivo e modo append quando exigidos.
- [x] Definir diretório atual, diretório do executável e variáveis de ambiente sem inventar letras de drive.
- [x] Implementar conversão UTF-16/UTF-8 e testar nomes não ASCII.
- [x] Adicionar testes de permissões, arquivos inexistentes, diretórios e concorrência controlada.

### Critério de saída

Um aplicativo-alvo consegue descobrir arquivos, criar saída em diretório, ler
configuração e lidar com erros de filesystem sem caminhos fixos do projeto.

## Fase 11 — Concorrência e rede opcional

Esta fase só começa se um aplicativo-alvo justificar threads ou rede.

- [x] Implementar `CreateThread`, `ExitThread`, `WaitForSingleObject` e `CloseHandle`.
- [x] Implementar `CRITICAL_SECTION` compatível com o escopo atual de convidado single-thread.
- [ ] Ampliar sincronização para threads, eventos e mutexes quando um aplicativo-alvo justificar.
- [x] Definir TLS, encerramento de threads e chamadas ABI em threads convidadas.
- [ ] Se houver alvo concreto, criar uma camada WinSock mínima separada de `KERNEL32.dll`.
- [x] Testar deadlock, timeout, cancelamento e propagação de falha do convidado.

### Critério de saída

Um aplicativo-alvo multithread passa testes repetíveis sem corrida conhecida,
deadlock ou corrupção de estado. Rede só entra com alvo concreto e testes
reprodutíveis.

## Fase 12 — GUI útil por aplicativo

A GUI evolui a partir de um aplicativo-alvo, e não de uma lista abstrata de
APIs.

- [ ] Escolher um aplicativo GUI pequeno, de código aberto, com janela e controles básicos.
- [ ] Implementar mouse completo, foco, mensagens de comando, menus e ciclo de vida exigidos pelo alvo.
- [ ] Implementar Unicode (`W`), fontes, desenho e invalidação somente quando usados.
- [ ] Implementar controles e diálogos somente se o aplicativo exigir.
- [ ] Avaliar Wayland/toolkit depois de existir uma aplicação GUI real suportada.
- [ ] Automatizar testes visuais por screenshot ou propriedades observáveis, além do exit code.

### Critério de saída

Um aplicativo GUI real abre, recebe interação, renderiza seu fluxo principal e
encerra corretamente em uma sessão X11 de teste, com limitações publicadas.

## O que fica explicitamente fora do estágio atual

- Jogos, DirectX, drivers, anti-cheat, .NET, COM, ActiveX e serviços Windows, até que exista decisão explícita, alvo concreto e fase própria.
- Implementar centenas de APIs sem aplicativo-alvo e regressão.
- Declarar suporte porque o programa abriu; o fluxo principal precisa ser verificável.

## Objetivo estratégico — compatibilidade ampla por etapas

O objetivo do projeto é tornar o TradutorLinux útil para aplicativos Windows em
geral, aumentando continuamente a quantidade, as categorias e o tamanho dos
aplicativos que funcionam no Linux. Isso inclui chegar progressivamente a
aplicativos grandes, desde que suas dependências possam ser implementadas com
segurança e testadas.

“Aplicativos em geral” é um objetivo de cobertura, não uma declaração de que
qualquer `.exe` já funciona. O progresso será medido por dados: aplicativos
reais testados, categorias cobertas, imports implementados, fluxos principais
aprovados, resultados corretos e falhas reproduzíveis. Não será medido por
quantidade de APIs declaradas sem uso real.

### Estratégia de expansão

- [ ] Criar um catálogo de aplicativos reais por categoria: console, arquivos, rede, ferramentas de desenvolvimento, produtividade e GUI.
- [ ] Manter níveis de compatibilidade: inicia, fluxo principal, uso diário e cobertura avançada.
- [ ] Coletar imports de muitos aplicativos e priorizar APIs que aparecem em vários alvos.
- [ ] Implementar famílias de DLLs por demanda: `KERNEL32`, `NTDLL` limitada, `ADVAPI32`, `USER32`, `GDI32`, `SHELL32`, `OLE32`, `COMDLG32`, `WS2_32` e CRTs.
- [ ] Criar testes de integração por aplicativo e uma matriz pública de limitações.
- [ ] Adicionar execução isolada, timeout, limites de recursos e diagnóstico para que aplicativos grandes não derrubem o host.
- [ ] Avaliar compatibilidade por versões e builds específicos, sem assumir que duas versões do mesmo aplicativo usam as mesmas APIs.

### Etapas para aplicativos grandes

1. **Base de execução:** PE, relocations, imports, TLS, exceções, processo,
   argumentos, ambiente, heap e CRT.
2. **Sistema operacional básico:** arquivos, diretórios, Unicode, registry
   limitado, sincronização, threads, processos filhos e tempo.
3. **Bibliotecas comuns:** shell, diálogos, controles, recursos, clipboard,
   fontes, GDI e rede.
4. **Aplicativos de médio porte:** ferramentas com múltiplas DLLs, plugins,
   configuração e vários threads.
5. **Aplicativos grandes:** GUI complexa, instaladores, suítes de produtividade
   e outros alvos escolhidos por cobertura e valor.
6. **Recursos especializados:** COM, DirectX, áudio, impressão e .NET somente
   quando houver decisão explícita de escopo e aplicativos-alvo.

Cada etapa depende da anterior. Um aplicativo grande não será considerado
suportado por simplesmente abrir a janela: ele precisa concluir operações
representativas sem corrupção, travamento ou resultado incorreto.

## Próximos marcos

| Marco | Resultado verificável |
|---|---|
| M1 — Parser | PE32+ válido lido; PE inválido rejeitado com segurança. |
| M2 — Image mapper | Imagem mínima mapeada, relocada e protegida. |
| M3 — Imports | Imports conhecidos resolvidos; ausentes diagnosticados. |
| M4 — Console | `tl_hello.exe` executa com saída e retorno corretos. |
| M5 — Arquivos | Fixture lê e escreve arquivo com semântica documentada. |
| M6 — Cobertura | Primeira aplicação-alvo adicional incluída na matriz e na regressão. |
| M7 — GUI | Janela real com message loop (`tl_win.exe`) e decisão de produto tomada: GUI Win32 mínima segue como objetivo experimental. |
| M8 — Diagnóstico controlado | Convidado executado em processo filho isolado; término por sinal vira `guest-signal` no trace e `71` no exit code; falhas controladas (ponteiro, arquivo, memória, imports) cobertas por testes. |
| M9 — Alvos reais | Três aplicativos pequenos e úteis executam fluxos completos com regressão no CI. |
| M10 — CRT mínimo | Um aplicativo compilado com CRT recebe argumentos, usa ambiente e termina corretamente. |
| M11 — Arquivos reais | Um aplicativo cria, enumera e manipula arquivos e diretórios usando caminhos traduzidos. |
| M12 — GUI real | Um aplicativo GUI escolhido por seus imports completa um fluxo principal sob X11. |

Com o marco M7 concluído, a GUI mínima avançou além do planejado e passa a ser
acompanhada no próprio roadmap da Fase 7: teclado estendido (`WM_KEYUP`, virtual
keys, `Shift`), timers (`WM_TIMER` periódico) e um GDI mínimo (pintura com
`BeginPaint`/`EndPaint`/`TextOut`). Cada nova API exige fixture e regressão; o
próximo passo natural, quando justificado por um aplicativo-alvo, é o desenho de
formas (`Rectangle`/`FillRect`), fontes/cores ou a entrada de mouse completa.

## Definição de pronto

Uma tarefa do roadmap só é considerada pronta quando:

- o código foi compilado com as configurações suportadas;
- existe um teste automatizado ou uma justificativa documentada para teste manual;
- falhas são observáveis pelo trace ou por uma mensagem de erro útil;
- a documentação da API ou limitação foi atualizada;
- o comportamento não quebra os fixtures já suportados.
