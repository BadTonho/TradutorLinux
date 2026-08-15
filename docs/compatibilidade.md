# Matriz de compatibilidade

Esta matriz declara o comportamento suportado; ela não é uma promessa de compatibilidade geral com Windows.

## Aplicações de teste

| Fixture | Arquitetura | CRT | Imports esperados | Estado atual | Próximo marco |
|---|---|---:|---|---|---|
| `tl_nop.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado, parseado, mapeado e com imports resolvidos na Fase 3; ainda não executado | Fase 4 |
| `tl_hello.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | Suportado no MVP: escreve `Ola do Windows no Linux!` em stdout, retorna `0` e emite trace | Fase 5 |
| `tl_echo.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `ReadFile`, `WriteFile` | Suportado no MVP: ecoa stdin para stdout com handles padrão | Fase 5 |
| `tl_file.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!CloseHandle`, `CreateFileA`, `ExitProcess`, `GetLastError`, `GetStdHandle`, `ReadFile`, `SetLastError`, `VirtualAlloc`, `VirtualFree`, `WriteFile` | Suportado no subconjunto da Fase 5: aloca memória e grava/reabre/lê arquivo relativo | Fase 6 |
| `tl_gui.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!MessageBoxA` | Protótipo manual: caixa modal X11 mínima; não executado automaticamente por depender de display | Fase 7 |
| `tl_win.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Janela real com message loop X11; fecha via `WM_CLOSE`/autoclose; teclado via `WM_KEYDOWN`/`WM_CHAR`; executado automaticamente sob Xvfb (teste `runtime_gui_smoke`, cenários autoclose, `WM_DELETE_WINDOW` e `KeyPress 'q'`) | Fase 7 |
| `tl_reloc.exe` | PE32+ AMD64 | Não | Nenhum | Gerado com `-Wl,--dynamicbase`, verificado, parseado e mapeado na Fase 2; usado para validar base relocations | Fase 4 |
| `tl_missing_dll.exe` | PE32+ AMD64 | Não | `USER32.dll!MessageBoxW` | Gerado, verificado e rejeitado na Fase 3: `USER32.dll` é conhecida, mas o símbolo diagnostica `unknown-symbol`; retorna `5` sem executar o entry point | Fase 4 |

As fontes e manifestos das fixtures ficam em `tests/samples/`. Os binários são produtos de build e ficam em `build/<preset>/tests/samples/generated/`.

## Leitor de PE (Fase 1)

O leitor de PE (`include/tradutorlinux/pe/pe_reader.hpp`, `src/pe/pe_reader.cpp`) valida e interpreta:

- DOS header, assinatura PE, COFF header e optional header PE32+ (magic `0x20B`).
- Tabela de seções, com verificação de que headers e dados crus cabem no arquivo.
- Import table por nome (hint) e por ordinal, com limites de DLLs e símbolos.
- Base relocations por bloco e entrada.

Comportamento de rejeição:

| Entrada | Resultado |
|---|---|
| Arquivo truncado no meio de qualquer estrutura | `Truncated` |
| Assinatura DOS/PE ausente, offsets inconsistentes, tamanhos inválidos | `Malformed` |
| Arquitetura diferente de `x86-64` (machine `0x8664`) | `UnsupportedArchitecture` |
| Optional header PE32 (magic `0x10B`) ou outro formato | `UnsupportedFormat` |

O CLI expõe o leitor via `--trace` (eventos do componente `pe`, ver `docs/diagnostico.md`) e via resumo em `stderr`. A saída do leitor é comparada em teste de integração com `llvm-readobj` para as fixtures geradas.

## Mapeamento de imagem (Fase 2)

O mapeador (`include/tradutorlinux/loader/image_mapper.hpp`, `src/loader/image_mapper.cpp`) reserva a imagem no endereço preferencial quando possível e aplica base relocations quando a base real difere da preferencial. O contrato detalhado (layout de memória, política de permissões, tipos de relocations suportados) está em `docs/arquitetura/mapeamento-imagem.md`.

Comportamento de rejeição:

| Condição | Resultado |
|---|---|
| `SizeOfImage` inválido (0) ou que excede o espaço de endereço do host | `InvalidImage` |
| Seção que excede o tamanho da imagem | `InvalidImage` |
| Seções sobrepostas na imagem | `InvalidImage` |
| Diretório de relocations inválido ou com alvo fora da imagem mapeada | `InvalidImage` |
| Falha do `mmap`/`mprotect` por falta de memória | `OutOfMemory` |

O CLI emite eventos `loader` no trace (ver `docs/diagnostico.md`) ou um resumo em `stderr` quando `--trace` não é usado. O mapa é liberado (`unmap`) ao final do comando.

## Resolução de imports (Fase 3)

O resolvedor (`include/tradutorlinux/loader/import_resolver.hpp`, `src/loader/import_resolver.cpp`) percorre a import table do PE, procura cada DLL no registro de módulos internos e grava o endereço resolvido no slot correspondente da IAT da imagem mapeada. Os módulos internos registrados embutidos são declarados em `include/tradutorlinux/loader/module.hpp` e `src/loader/module.cpp`; o contrato (registro, tabela de exports, ordinais internos, ABI) está em `docs/arquitetura/imports.md`.

O contexto mínimo de processo (`include/tradutorlinux/loader/process.hpp`, `src/loader/process.cpp`) mapeia a imagem, resolve imports e prepara a pilha do thread inicial com guard page; o entry point nunca é executado nesta fase.

Comportamento de rejeição:

| Condição | `status` no trace | Resultado |
|---|---|---|
| DLL não registrada | `unknown-dll` | `5` (`Unsupported`) |
| Símbolo não exportado pela DLL | `unknown-symbol` | `5` |
| Ordinal não exportado pela DLL | `unknown-ordinal` | `5` |
| Símbolo conhecido sem implementação | `not-implemented` | `5` |
| Delay import directory presente | `unsupported-mechanism` | `5` |
| Slot da IAT fora das seções mapeadas | `unsupported-mechanism` | `5` |

Em qualquer falha o entry point não é executado e todas as entradas são reportadas no trace (ver `docs/diagnostico.md`).

## APIs de console (Fase 4)

Os exports de `KERNEL32.dll` apontam para funções hospedeiras com a convenção Microsoft x64 (`TL_MSABI`). O runner chama o entry point depois de mapear a imagem e resolver a IAT; `ExitProcess` transfere o controle de volta ao runner e não encerra diretamente o processo Linux.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `GetStdHandle` | Suportado | Mapeia `STD_INPUT_HANDLE`, `STD_OUTPUT_HANDLE` e `STD_ERROR_HANDLE` para tokens opacos; outros valores retornam `NULL` |
| `KERNEL32.dll` | `WriteFile` | Suportado | Escreve em stdout/stderr; exige handle padrão válido, buffer válido e `lpOverlapped == NULL` |
| `KERNEL32.dll` | `ReadFile` | Suportado | Lê stdin; exige handle padrão de entrada válido, buffer válido e `lpOverlapped == NULL` |
| `KERNEL32.dll` | `ExitProcess` | Suportado | Captura o código de saída e retorna o controle ao runner |

Os tokens de handles padrão não são handles de arquivo. A entrada e saída são bytes; nenhuma conversão de encoding é feita. O contrato detalhado está em `docs/arquitetura/console.md`.

## Runtime básico (Fase 5)

`GetLastError`/`SetLastError` usam estado por thread. O subconjunto de arquivos
é `CreateFileA` com `GENERIC_READ`/`GENERIC_WRITE`, `CREATE_ALWAYS` ou
`OPEN_EXISTING`, seguido por `ReadFile`, `WriteFile` e `CloseHandle`. Apenas
caminhos relativos sem drive são aceitos; `\\` é normalizado para `/`.

`VirtualAlloc` e `VirtualFree` têm o contrato limitado descrito em
[`runtime-basico.md`](arquitetura/runtime-basico.md).

## GUI mínima (Fase 7)

O protótipo registra um subconjunto de `USER32.dll` e usa X11 diretamente. Ele é
experimental, não altera o subsistema de console e só aceita `type == 0` em
`MessageBoxA`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `USER32.dll` | `MessageBoxA` | Suportado | Caixa modal com `hWnd == NULL` e `uType == 0`; OK retorna `1`, fechar retorna `0` |
| `USER32.dll` | `RegisterClassExA` | Suportado | Classe única por nome (case-insensitive); retorna atom `>= 1` |
| `USER32.dll` | `CreateWindowExA` | Suportado | Cria janela X11 a partir da classe registrada e despacha `WM_CREATE` ao `WNDPROC` (retorno `-1` aborta a criação); parent/menu/instância/param ignorados |
| `USER32.dll` | `ShowWindow` | Suportado | Mostra/esconde a janela X11 |
| `USER32.dll` | `UpdateWindow` | Suportado | Despacha `WM_PAINT` diretamente ao `WNDPROC` |
| `USER32.dll` | `GetMessageA` | Suportado | Traduz eventos X11 para `WM_PAINT`/`WM_LBUTTONDOWN`/`WM_KEYDOWN`/`WM_CLOSE`; entrega mensagens pendentes antes dos eventos X11; retorna `0` com `WM_QUIT` após `PostQuitMessage` |
| `USER32.dll` | `TranslateMessage` | Suportado | Converte o `WM_KEYDOWN` mais recente em `WM_CHAR` com o caractere real |
| `USER32.dll` | `DispatchMessageA` | Suportado | Invoca o `WNDPROC` do convidado (`TL_MSABI`, host→convidado) |
| `USER32.dll` | `DefWindowProcA` | Suportado | `WM_CLOSE` → `DestroyWindow`; demais retornam `0` |
| `USER32.dll` | `DestroyWindow` | Suportado | Destrói a janela e despacha `WM_DESTROY` |
| `USER32.dll` | `PostQuitMessage` | Suportado | Sinaliza `WM_QUIT`; `GetMessageA` retorna `0` |

`tl_gui.exe` é validado automaticamente quanto a formato e imports; a janela
deve ser validada manualmente numa sessão X11. `tl_win.exe` é executado de
ponta a ponta sob `Xvfb` (sempre um servidor próprio, sem window manager) pelo
teste `runtime_gui_smoke`, que cobre o message loop (autoclose), o fechamento
real por `WM_DELETE_WINDOW` e a entrada de teclado (`KeyPress` sintético →
`WM_KEYDOWN`/`WM_CHAR`) — ver [`gui-x11.md`](arquitetura/gui-x11.md).
