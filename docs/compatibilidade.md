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
| `tl_win2.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Duas janelas simultâneas com `WNDPROC`s independentes; eventos roteados por janela (fila por janela no pump); executado automaticamente sob Xvfb (cenário `janelas` do `runtime_gui_smoke`, `KeyPress 'q'` em A e `'k'` em B) | Fase 7 |
| `tl_key.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Teclado estendido: `Shift+q` → `WM_CHAR('Q')`, `Return` → `WM_KEYDOWN(VK_RETURN)` e `Left` → `WM_KEYUP(VK_LEFT)`; executado sob Xvfb (cenário `keys`, exit-code `7`) | Fase 7 |
| `tl_timer.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `SetTimer`, `KillTimer`, `DestroyWindow`, `PostQuitMessage` | Timer periódico de 200 ms: dois `WM_TIMER`, depois `KillTimer` + `DestroyWindow`; executado sob Xvfb (cenário `timer`, exit-code `7`) | Fase 7 |
| `tl_gdi.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage`, `BeginPaint`, `EndPaint`; `GDI32.dll!GetStockObject`, `TextOutA` | Pintura mínima no `WM_PAINT` (`BeginPaint`/`TextOutA`/`EndPaint`) validando `HDC == HWND` e `rcPaint`; executado sob Xvfb (cenário `gdi`, exit-code `3`) | Fase 7 |
| `tl_reloc.exe` | PE32+ AMD64 | Não | Nenhum | Gerado com `-Wl,--dynamicbase`, verificado, parseado e mapeado na Fase 2; usado para validar base relocations | Fase 4 |
| `tl_missing_dll.exe` | PE32+ AMD64 | Não | `USER32.dll!MessageBoxW` | Gerado, verificado e rejeitado na Fase 3: `USER32.dll` é conhecida, mas o símbolo diagnostica `unknown-symbol`; retorna `5` sem executar o entry point | Fase 4 |
| `tl_crash.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado, mapeado e executado em processo filho isolado: o convidado acessa o endereço `0`, o hospedeiro observa o `SIGSEGV` via `waitpid`, emite `terminated category="guest-signal" signal="SIGSEGV"` e retorna `71` (`GuestFault`) | Diagnóstico de falhas |
| `tl_hang.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado e executado em processo filho isolado com `--timeout 1`: o convidado entra em loop infinito, o hospedeiro o mata com `SIGKILL`, emite `terminated category="guest-timeout"` e retorna `72` (`GuestTimeout`) | Diagnóstico de falhas |

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

O protótipo registra um subconjunto de `USER32.dll` e `GDI32.dll` e usa X11
diretamente. Ele é experimental, não altera o subsistema de console e só aceita
`type == 0` em `MessageBoxA`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `USER32.dll` | `MessageBoxA` | Suportado | Caixa modal com `hWnd == NULL` e `uType == 0`; OK retorna `1`, fechar retorna `0` |
| `USER32.dll` | `RegisterClassExA` | Suportado | Classe única por nome (case-insensitive); retorna atom `>= 1` |
| `USER32.dll` | `CreateWindowExA` | Suportado | Cria janela X11 a partir da classe registrada e despacha `WM_CREATE` ao `WNDPROC` (retorno `-1` aborta a criação); parent/menu/instância/param ignorados |
| `USER32.dll` | `ShowWindow` | Suportado | Mostra/esconde a janela X11 |
| `USER32.dll` | `UpdateWindow` | Suportado | Despacha `WM_PAINT` diretamente ao `WNDPROC` |
| `USER32.dll` | `GetMessageA` | Suportado | Traduz eventos X11 para `WM_PAINT`/`WM_LBUTTONDOWN`/`WM_KEYDOWN`/`WM_KEYUP`/`WM_CLOSE`, roteados por janela (fila por janela no pump); entrega mensagens pendentes antes dos eventos X11; despacha `WM_TIMER` expirados; retorna `0` com `WM_QUIT` após `PostQuitMessage` |
| `USER32.dll` | `TranslateMessage` | Suportado | Converte o `WM_KEYDOWN` mais recente em `WM_CHAR` com o caractere real (sem `WM_CHAR` para teclas sem caractere) |
| `USER32.dll` | `SetTimer` | Suportado | Timer periódico por janela → `WM_TIMER`; só `lpTimerFunc == NULL` |
| `USER32.dll` | `KillTimer` | Suportado | Remove um timer ativo |
| `USER32.dll` | `DispatchMessageA` | Suportado | Invoca o `WNDPROC` do convidado (`TL_MSABI`, host→convidado) |
| `USER32.dll` | `DefWindowProcA` | Suportado | `WM_CLOSE` → `DestroyWindow`; demais retornam `0` |
| `USER32.dll` | `DestroyWindow` | Suportado | Destrói a janela e despacha `WM_DESTROY` |
| `USER32.dll` | `PostQuitMessage` | Suportado | Sinaliza `WM_QUIT`; `GetMessageA` retorna `0` |
| `USER32.dll` | `GetDC` / `ReleaseDC` | Suportado | `HDC == HWND` (token opaco da janela); validam o par `hwnd`/`dc` |
| `USER32.dll` | `BeginPaint` / `EndPaint` | Suportado | Preenchem o `PAINTSTRUCT` (layout Microsoft x64, 72 bytes) com o tamanho da janela e marcam/desmarcam o estado de pintura; `HDC == HWND` |
| `GDI32.dll` | `GetStockObject` | Suportado | Token opaco por stock object (tabela estática, `object` em `0..23`); stock objects não são liberados |
| `GDI32.dll` | `TextOutA` / `TextOut` | Suportado | Desenha texto ANSI com comprimento explícito via `XDrawString` no `HDC`/janela |

`tl_gui.exe` é validado automaticamente quanto a formato e imports; a janela
deve ser validada manualmente numa sessão X11. `tl_win.exe`, `tl_win2.exe`,
`tl_key.exe`, `tl_timer.exe` e `tl_gdi.exe` são executados de ponta a ponta sob
`Xvfb` (sempre um servidor próprio, sem window manager) pelo teste
`runtime_gui_smoke`, que cobre o message loop (autoclose), o fechamento real por
`WM_DELETE_WINDOW`, a entrada de teclado (`KeyPress` sintético →
`WM_KEYDOWN`/`WM_CHAR`), a demultiplexação entre duas janelas simultâneas, o
teclado estendido (`KeyPress`+`KeyRelease`, `Shift`, teclas sem caractere →
`WM_KEYDOWN`/`WM_KEYUP`), os timers (`SetTimer` → `WM_TIMER` → `KillTimer`) e a
pintura mínima (`BeginPaint`/`TextOut`/`EndPaint`) — ver
[`gui-x11.md`](arquitetura/gui-x11.md).

## Aplicativos-alvo reais (Fase 8)

A Fase 8 mede progresso por aplicativos reais, e não apenas por fixtures. Os
primeiros alvos escolhidos são utilitários de console pequenos, de código
aberto e compilados em CI com `mingw-w64`. Cada alvo é fixado por versão,
toolchain e lista de imports; a lista real é capturada por `llvm-readobj` e por
`--report` do runtime e protegida por teste (label `targetapp`, 8 testes).

As fontes são baixadas com hash SHA-256 verificado pelo módulo
`tests/targets/CMakeLists.txt` (opção `TL_BUILD_TARGET_APPS=ON`, usada no job
`target-apps` do CI). O `--report` lista os imports reais e classifica o alvo
como `result: supported` (exit code `0`) ou `result: unsupported` (exit code
`5`), sem mapear nem executar a imagem; o script
`tests/targets/verify_target_report.cmake` aceita as duas respostas e exige que
todo import do manifest apareça listado.

| Aplicativo | Versão / toolchain | Imports (símbolos) | Estado |
|---|---|---|---|
| `xxd.exe` | vim `v9.2.0957` (`src/xxd.c`), `-O2 -s` | `KERNEL32.dll` (16), `msvcrt.dll` (57) | **Executa de ponta a ponta**: saída byte-idêntica ao `xxd` do sistema nos modos padrão e `-p`, exit `0`; arquivo inexistente → exit `2` com erro em stderr. Regressão e2e em CTest (ouro em `tests/targets/golden/xxd/`, 3 testes) |
| `bzip2.exe` | bzip2 `1.0.8`, `-O2 -s` | `KERNEL32.dll` (13), `msvcrt.dll` (66) | Cross-build no CI; imports pinados; `result: supported` (todos os imports de `msvcrt.dll` implementados: `strncpy`, `strstr`, `ungetc`, `fgetc`, `fread`, `isspace`, `memmove`, `strcat`, `remove`, `_stat64`) |
| `dos2unix.exe` | dos2unix `7.5.6`, `-O2 -DD2U_UNIFILE -s` | `KERNEL32.dll` (24), `msvcrt.dll` (63), `SHELL32.dll!CommandLineToArgvW` (1) | Cross-build no CI; imports pinados; `result: unsupported` (SHELL32 e caminho `W` fora de escopo) |
| `unix2dos.exe` | dos2unix `7.5.6` | idem `dos2unix.exe` | Idem |

Os manifests com a lista completa de imports ficam em
`tests/targets/manifests/`. Os binários são produtos de build e ficam em
`build/<dir>/tests/targets/out/`.

## CRT mínimo e KERNEL32 de console (Fase 9)

O subconjunto de `msvcrt.dll` implementado é o núcleo do CRT do mingw-w64 e o
subconjunto de `KERNEL32.dll` exigido pelo `xxd.exe`. A fronteira usa a
convenção Microsoft x64 (`TL_CRT_MSABI`/`TL_MSABI`) e não propaga exceções C++.
Contratos de ABI em `docs/arquitetura/msvcrt.md` e
`docs/arquitetura/console.md`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `msvcrt.dll` | `__getmainargs` | Suportado | Constrói `argc`/`argv`/`envp` a partir da linha de comando do convidado (definida pelo CLI via `msvcrt_set_guest_command_line`); `argv[0]` é o caminho do executável; o final da lista é `NULL` |
| `msvcrt.dll` | `__initterm`, `__set_app_type`, `__setusermatherr`, `_cexit`, `_lock`, `_unlock`, `__C_specific_handler` | Suportado | `__initterm` executa a lista de callbacks (TLS/CTOR); demais são no-ops ou terminam o convidado (`__C_specific_handler` → exit `3`) |
| `msvcrt.dll` | `_amsg_exit`, `abort`, `exit`, `atexit` | Suportado | Terminam via `ExitProcess`; `atexit` acumula handlers executados no encerramento |
| `msvcrt.dll` | `_errno`, `getenv`, `strerror` | Suportado | Célula `errno` global do hospedeiro; `getenv` lê o ambiente do host |
| `msvcrt.dll` | `fopen`/`fclose`/`fflush`/`ferror`/`fseek`/`ftell`/`rewind`/`fgetc`/`fputc`/`fputs`/`fprintf`/`vfprintf`/`fwrite` | Suportado | I/O em `GuestFile` (layout `_iobuf` de 48 bytes), unbuffered via `::write` com loop `EINTR` |
| `msvcrt.dll` | `_open`/`_fdopen`/`_fileno`/`_isatty`/`_setmode`/`__iob_func` | Suportado | Tradução de flags `_O_*`; modo por fd (`_O_TEXT`/`_O_BINARY`) refletido na flag `_IOSTRG` do `GuestFile` |
| `msvcrt.dll` | `malloc`/`calloc`/`free`, `memcpy`/`memset` | Suportado | Alocação e memória diretas do hospedeiro |
| `msvcrt.dll` | `strlen`/`strcmp`/`strncmp`/`strcpy`/`strncpy`/`strstr`/`strcat`/`strtol`/`strtoul`/`wcslen`/`isalnum`/`isspace`/`toupper` | Suportado | Semântica libc para ASCII/latin-1 |
| `msvcrt.dll` | `fgetc`/`fread`/`ungetc` | Suportado | `fgetc` lê byte e verifica `charbuf` (pushback); `fread` lê `count` elementos de `size` bytes; `ungetc` devolve caractere ao stream via `charbuf` do `GuestFile` |
| `msvcrt.dll` | `memmove`/`remove`/`_stat64` | Suportado | `memmove` com tratamento de overlap; `remove` delega ao host; `_stat64` retorna `-1` com `ENOSYS` (stub) |
| `msvcrt.dll` | `localeconv`, `___lc_codepage_func`, `___mb_cur_max_func` | Suportado | Locale C fixo: `lconv` estático, code page `1252`, `mb_cur_max == 1` |
| `msvcrt.dll` | `signal` | Suportado | Registra handlers em tabela por sinal; nenhuma entrega real ao convidado |
| `KERNEL32.dll` | `VirtualQuery` | Suportado | Preenche `MEMORY_BASIC_INFORMATION` (48 bytes) a partir do `/proc/self/maps`: `BaseAddress`/`AllocationBase` = início da VMA, `RegionSize`, `State=MEM_COMMIT`, `Protect`/`AllocationProtect` mapeados de `rwx`, `Type=MEM_IMAGE`/`MEM_PRIVATE` |
| `KERNEL32.dll` | `VirtualProtect` | Suportado | `mprotect` sobre a página alinhada dentro da região; escreve a proteção antiga em `*lpflOldProtect`; rejeita região que não contém `[address, address+size)` |
| `KERNEL32.dll` | `MultiByteToWideChar` / `WideCharToMultiByte` | Suportado | CP `0` (ACP → 1252), `1252` e `65001` (UTF-8), conversões manuais sem locale; contagem com ponteiros `NULL`; `MB_ERR_INVALID_CHARS`; `ERROR_INSUFFICIENT_BUFFER` (122) |
| `KERNEL32.dll` | `Initialize/Enter/Leave/DeleteCriticalSection` | Suportado | No-ops com validação de ponteiro (convidado single-thread → exclusão trivial) |
| `KERNEL32.dll` | `TlsGetValue` | Suportado | Retorna `NULL` com `ERROR_SUCCESS` para slot não usado (TLS do CRT não é inicializado por nenhum callback do xxd) |
| `KERNEL32.dll` | `GetConsoleMode` / `SetConsoleMode` | Suportado | `GetConsoleMode` devolve `0x3` e `TRUE` só para fd com `isatty`; caso contrário `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `IsDBCSLeadByteEx` | Suportado | Sempre `FALSE` (sem DBCS) |
| `KERNEL32.dll` | `Sleep` | Suportado | `nanosleep` com loop `EINTR` |
| `KERNEL32.dll` | `SetUnhandledExceptionFilter` | Suportado | Registra o handler em célula global (nunca invoca); retorna o anterior |
| `KERNEL32.dll` | `GetModuleHandleA/W` | Suportado | Retorna handle para módulos conhecidos (`kernel32.dll`, `user32.dll`, `gdi32.dll`, `msvcrt.dll`); NULL para desconhecidos |
| `KERNEL32.dll` | `GetProcAddress` | Suportado | Stub: retorna NULL (resolução dinâmica de símbolos não suportada) |
| `KERNEL32.dll` | `GetCommandLineA/W` | Suportado | Retorna linha de comando formatada com aspas a partir do `argv` do convidado |
| `KERNEL32.dll` | `GetEnvironmentVariableA/W` | Suportado | Delega ao `getenv` do host; retorna tamanho em contagem-only, erro `ERROR_FILE_NOT_FOUND` se inexistente |
| `KERNEL32.dll` | `GetProcessHeap` | Suportado | Retorna token opaco fixo (heap único do processo) |
| `KERNEL32.dll` | `HeapAlloc` | Suportado | `malloc` do hospedeiro; flag `HEAP_ZERO_MEMORY` (0x0008) → `calloc` |
| `KERNEL32.dll` | `HeapFree` | Suportado | `free` do hospedeiro |
| `KERNEL32.dll` | `HeapReAlloc` | Suportado | `realloc` do hospedeiro |
| `KERNEL32.dll` | `GetTickCount64` | Suportado | `steady_clock` em milissegundos |
| `KERNEL32.dll` | `GetSystemTimeAsFileTime` | Suportado | `system_clock` convertido para ticks de 100ns desde 1601 |

O `xxd.exe` tem 42 testes unitários novos (`tests/test_win32.cpp` e
`tests/test_msvcrt.cpp`) cobrindo as conversões de code page (inclusive
surrogate pairs e erro `1113`), `VirtualQuery`/`VirtualProtect`, `TlsGetValue`,
critical sections, `__getmainargs`, stdio em `GuestFile`, `strtol`/`wcslen`,
locale e sinais.

Observações que orientam a próxima etapa (Fase 9/10):

- Todos os alvos compartilham o núcleo de CRT do mingw-w64: `__getmainargs`,
  `__iob_func`, `__initenv`, `_fmode`, `_errno`, `_commode`, `_initterm`,
  `_amsg_exit`, `_lock`/`_unlock`, `malloc`/`free`/`calloc`, `exit`/`atexit`/
  `abort`, `fopen`/`fclose`/`fread`/`fwrite`/`fprintf`/`vfprintf`/`fseek` e o
  grupo de strings (`strlen`/`strcmp`/`strcpy`/`strncpy`/`strstr`/`strcat`/
  `memcpy`/`memmove`/`memset`).
- `bzip2.exe` agora tem todos os imports de `msvcrt.dll` implementados;
  o próximo passo é criar a regressão e2e de execução (stdin/stdout/exit code).
- `dos2unix`/`unix2dos` exigem o caminho `W` (`GetCommandLineW`,
  `FindFirstFileW`/`FindNextFileW`/`FindClose`, `GetFileAttributesW`,
  `_wfopen`, `wcs*`) e `SHELL32.dll!CommandLineToArgvW` (expansão de curingas
  do mingw) — decidir se esse subconjunto entra na Fase 9 ou fica para depois.
- Nenhum alvo usa `GetStartupInfoA`/`GetEnvironmentStringsA` diretamente: o
  `crt2.o` do mingw delega a linha de comando e o ambiente ao `__getmainargs`
  de `msvcrt.dll`, então essas APIs são dependência interna do CRT mínimo, e
  não do aplicativo.
