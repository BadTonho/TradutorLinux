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

- **Fase atual:** Fase 7 — Avaliar GUI.
- **Marco concluído:** a Fase 7 foi validada de ponta a ponta e a decisão de produto foi tomada: **seguir com a GUI Win32 mínima como objetivo experimental**. `tl_gui.exe` abriu a janela X11, recebeu o clique em OK e encerrou com código `0`; `tl_win.exe` criou uma janela real e executou um message loop completo (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `PostQuitMessage`), encerrando via `WM_CLOSE`/autoclose com código `0`; o modo `--report` lista imports suportados sem executar o PE; `tl_hello`, `tl_echo` e `tl_file` têm regressões e limitações publicadas na matriz.
- **Marco concluído:** o smoke test de GUI passou a ter cobertura automática em CI. O teste `runtime_gui_smoke` sobe um `Xvfb` próprio e executa `tl_win.exe` de ponta a ponta em dois cenários: autoclose (message loop encerra sozinho via `WM_QUIT`) e fechamento real por `WM_DELETE_WINDOW` (mesmo `ClientMessage` do botão de fechar do WM), exigindo exit-code `1`, stdout vazio e os eventos esperados no trace. A conexão X11 do runtime é fechada no teardown (`DisplayCloser`), validado sob ASAN com `detect_leaks=1`.
- **Marco concluído:** `CreateWindowExA` agora despacha `WM_CREATE` ao `WNDPROC` do convidado antes de devolver o `HWND` (retorno `-1` aborta a criação e devolve `NULL`). A fixture `tl_win.c` marca uma flag no `WM_CREATE` e propaga no exit code via `PostQuitMessage`, então o `runtime_gui_smoke` prova o despacho exigindo exit-code `1`.
- **Próximo resultado observável:** dar entrada real ao message loop via `TranslateMessage` (teclado: `KeyPress` → `WM_KEYDOWN`/`WM_CHAR`) ou redefinir o pump para suportar múltiplas janelas simultâneas (fila de eventos por janela), cada um com fixture e teste de integração.

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

Validação: registro interno de `KERNEL32.dll` com `GetStdHandle`, `WriteFile` e `ExitProcess`; resolução por nome e ordinal com patch da IAT e restauração das permissões; stubs `ms_abi` testados diretamente; processo convidado com pilha de 1 MiB e guard page; fixture `tl_missing_dll.exe` retorna `5` sem executar o entry point e emite `unknown-symbol`; falhas de símbolo, ordinal, símbolo sem implementação, delay import e slot de IAT inválido têm testes unitários. Presets `debug` e `sanitize` passaram com 93 testes; no ambiente local, o sanitize foi executado com `LSAN_OPTIONS=detect_leaks=0` porque a descoberta do GoogleTest falha no LeakSanitizer sob ptrace.

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

Validação local: os 113 testes dos presets `debug` e `sanitize` passam, incluindo `tl_win.exe`, resolução dos novos imports de `USER32.dll`, o relatório sem execução e os testes unitários de layout `MSG`/`WNDCLASSEXA` e do ponteiro `WNDPROC` reconstruído por `bit_cast`. O loader valida o entry point, usa a pilha convidada com guard page, aplica relocations e rejeita execução fora da base quando não há relocations. `cppcheck` e `clang-tidy` passam sem pendências.

### Critério de saída — atendido

Uma aplicação gráfica de teste cria uma janela, recebe eventos básicos e encerra corretamente, sem comprometer o runtime de console.

Validação visual (2026-08-15, sessão X11 `DISPLAY=:0` acessível): `tl_gui.exe` executado com `--trace` abriu a janela "TradutorLinux GUI / Fase 7" com botão OK; ao clicar, o runtime registrou `[tl][runtime][info] ExitProcess exit-code="0" status="success" mechanism="guest-transfer"`, `[tl][process][info] exit exit-code="0" explicit="sim"` e encerrou com código `0`, liberando a imagem. `tl_win.exe` executado com `--trace` e `TL_GUI_AUTOCLOSE_MS=1` registrou `RegisterClassExA`, `CreateWindowExA`, `GetMessageA message="WM_QUIT"` e `ExitProcess exit-code="0"`, confirmando o message loop de ponta a ponta (autoclose → `WM_CLOSE` → `DefWindowProcA` → `DestroyWindow` → `WM_DESTROY` → `PostQuitMessage(0)`).

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

## Definição de pronto

Uma tarefa do roadmap só é considerada pronta quando:

- o código foi compilado com as configurações suportadas;
- existe um teste automatizado ou uma justificativa documentada para teste manual;
- falhas são observáveis pelo trace ou por uma mensagem de erro útil;
- a documentação da API ou limitação foi atualizada;
- o comportamento não quebra os fixtures já suportados.
