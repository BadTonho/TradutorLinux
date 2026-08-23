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
| `tl_win_w.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExW`, `CreateWindowExW`, `ShowWindow`, `UpdateWindow`, `GetMessageW`, `TranslateMessage`, `DispatchMessageW`, `DefWindowProcW`, `DestroyWindow`, `PostQuitMessage`, `SetWindowTextW`, `GetWindowTextW` | Janela real via `W` (wrappers `wide_to_utf8` → `A`): `RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`SetWindowTextW`/`GetWindowTextW`; validado `--report` 12/12, execução `Xvfb` análoga a `tl_win` | Fase 7 |
| `tl_key.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage` | Teclado estendido: `Shift+q` → `WM_CHAR('Q')`, `Return` → `WM_KEYDOWN(VK_RETURN)` e `Left` → `WM_KEYUP(VK_LEFT)`; executado sob Xvfb (cenário `keys`, exit-code `7`) | Fase 7 |
| `tl_timer.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `SetTimer`, `KillTimer`, `DestroyWindow`, `PostQuitMessage` | Timer periódico de 200 ms: dois `WM_TIMER`, depois `KillTimer` + `DestroyWindow`; executado sob Xvfb (cenário `timer`, exit-code `7`) | Fase 7 |
| `tl_gdi.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `USER32.dll!RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage`, `BeginPaint`, `EndPaint`; `GDI32.dll!GetStockObject`, `TextOutA` | Pintura mínima no `WM_PAINT` (`BeginPaint`/`TextOutA`/`EndPaint`) validando `HDC == HWND` e `rcPaint`; executado sob Xvfb (cenário `gdi`, exit-code `3`) | Fase 7 |
| `tl_reloc.exe` | PE32+ AMD64 | Não | Nenhum | Gerado com `-Wl,--dynamicbase`, verificado, parseado e mapeado na Fase 2; usado para validar base relocations | Fase 4 |
| `tl_missing_dll.exe` | PE32+ AMD64 | Não | `USER32.dll!TlUnknownSymbolW` | Gerado, verificado e rejeitado na Fase 3: `USER32.dll` é conhecida, mas o símbolo diagnostica `unknown-symbol`; retorna `5` sem executar o entry point. A import library do fixture é gerada via `dlltool` (`defs/tl_missing_dll.def`) porque o símbolo não existe nas bibliotecas reais do mingw | Fase 4 |
| `tl_delay_import.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess` somente no diretório delay-import | **Suportado:** descritor `grAttrs=0x1`, INT/IAT atrasadas e resolução antecipada; `--report` resolve 1/1 e a execução chama `ExitProcess` pela IAT atrasada. A variante com `TlMissingDelayImportW` retorna `5` antes do entry point e identifica `mechanism="delay-import"` | Delay imports RVA |
| `tl_unwind.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!RtlCaptureContext`, `RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlPcToFileHeader`, console e `ExitProcess` | **Suportado no núcleo de unwinding:** possui `.pdata`/`.xdata`, captura um `CONTEXT`, localiza sua `RUNTIME_FUNCTION`, desempilha um frame real e valida a base da imagem; imprime `unwind\n`, exit `0`. Não prova nem declara despacho SEH/`try/catch`. | Núcleo de unwinding x64 |
| `tl_crash.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado, mapeado e executado em processo filho isolado: o convidado acessa o endereço `0`, o hospedeiro observa o `SIGSEGV` via `waitpid`, emite `terminated category="guest-signal" signal="SIGSEGV" fault-address="0x0"` (o crash log captura o `si_addr` no filho e o converte em RVA/seção/importação quando o endereço cai dentro da imagem) e retorna `71` (`GuestFault`) | Diagnóstico de falhas |
| `tl_hang.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado e executado em processo filho isolado com `--timeout 1`: o convidado entra em loop infinito, o hospedeiro o mata com `SIGKILL`, emite `terminated category="guest-timeout"` e retorna `72` (`GuestTimeout`) | Diagnóstico de falhas |
| `tl_thread.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!CloseHandle`, `CreateThread`, `ExitProcess`, `ExitThread`, `GetStdHandle`, `WaitForSingleObject`, `WriteFile` | **Suportado no escopo da Fase 11**: cria duas threads sequenciais, cada uma escreve "Thread done" e termina via `ExitThread`; a thread principal aguarda cada handle, escreve "Main done" e encerra. Metadata e execução e2e passam em Debug, Release e Sanitize (`LSAN_OPTIONS=detect_leaks=0`); saída esperada: `Thread done\nThread done\nMain done\n` e exit `0` | Fase 11 |
| `tl_files_wide.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — arquivos, metadados, tempos e caminhos Unicode | Fixture genérica suportada: cria arquivo com `é`, consulta tamanho/atributos/tempos, copia, move e remove; saída `files\n`, exit `0` | Base de arquivos |
| `tl_resources.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!FindResourceW`, `LoadResource`, `LockResource`, `SizeofResource` | Lê somente o recurso `RCDATA` embutido após validação de limites; saída byte-idêntica ao payload, exit `0`; `--report` não executa | Recursos PE |
| `tl_sync.exe` | PE32+ AMD64 | Não | eventos, mutex, semáforo e esperas em `KERNEL32.dll` | Cobre evento manual/automático, timeout, semáforo, mutex recursivo e `WaitForMultipleObjects`; saída `sync\n`, exit `0` | Sincronização |
| `tl_process_parent.exe` / `tl_process_child.exe` | PE32+ AMD64 | Não | `CreateProcessW`, `GetExitCodeProcess`, `TerminateProcess` e `WaitForSingleObject` | Pai cria filhos PE32+ pelo mesmo parser/loader/import resolver; o código de saída real do convidado viaja pelo pipe de resultado do filho (protocolo `[flag][exit_code LE32]`) e o cache de `/proc/self/maps` é invalidado pós-fork e a cada mmap/munmap da pilha; valida código `7` e encerramento controlado `9`; saída `child\nparent\n`, exit `0`. `CreateProcessA/W` resolve caminhos relativos primeiro no diretório do executável convidado (ordem de busca do Windows) | Processos filhos |
| `tl_install_setup.exe` / `tl_install_app.exe` | PE32+ AMD64 | Não | arquivos Unicode, ambiente, `GetModuleFileNameW`, `CreateProcessW`, espera e handles | **Fluxo de instalação suportado:** setup externo observa `Z:\\...`, copia a aplicação de `C:\\windows\\temp` para `C:\\Program Files` e a inicia com `CreateProcessW`; a aplicação observa `C:\\...`, diretório herdado e `%LOCALAPPDATA%` do mesmo prefixo. `install → catálogo → app run` é coberto por `integration_install_prefix_catalog_run`; prefixos distintos não compartilham estado. O setup de múltiplos candidatos confirma `InstallPending` (`6`) e a escolha no launcher | Instalação por prefixo |
| `tl_network_loopback.exe` | PE32+ AMD64 | Não | `WS2_32.dll` TCP/UDP, resolução local e `WSAPoll` | Fixture somente loopback, com TCP, UDP e `localhost`; passa com sockets permitidos e é skip controlado em sandbox que retorna `EACCES/EPERM` | WS2_32 |
| `tl_registry_unicode.exe` | PE32+ AMD64 | Não | `ADVAPI32.dll` chaves/valores Unicode | Cria, persiste, reabre, consulta e remove chave/valor UTF-16 em armazenamento genérico por escopo; saída `registry\n`, exit `0` | Registro |
| `tl_dynload.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `LoadLibraryA/W/ExA/ExW`, `FreeLibrary`, `GetModuleHandleA/W/ExA/ExW`, `GetProcAddress`, `GetLastError` | Fixture de carregamento dinâmico: `LoadLibrary` com caminho `C:\...`, API Set `api-ms-win-core-file-l1-1-0.dll`, `LoadLibraryEx`, `GetProcAddress` por nome e ordinal (36=`GetTickCount64`), `FreeLibrary`, `GetModuleHandleEx` `PIN`/`FROM_ADDRESS`; saída `dynload\n`, exit `0` | Carregamento dinâmico |
| `tl_version.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `GetVersionExA/W`, `VerifyVersionInfoW`, `VerSetConditionMask`, `GetUserDefaultLocaleName`, `LocaleNameToLCID` | Fixture versão/locale: `GetVersionExA/W` 10.0.19044, `VerifyVersionInfoW`/`VerSetConditionMask` cadeia `VER_MAJOR|MINOR`, `GetUserDefaultLocaleName` → `en-US` (6 com NUL, `122` em buffer curto), `LocaleNameToLCID` `en-US`/`pt-BR`; saída `version\n`, exit `0` | Versão/locale |
| `tl_waitaddr.exe` | PE32+ AMD64 | Não | `KERNEL32.dll`/`api-ms-win-core-synch-l1-2-0.dll` — `WaitOnAddress`/`WakeByAddressSingle`/`WakeByAddressAll`, `CreateThread`/`WaitForSingleObject` | Fixture espera por endereço: timeout 50ms `ERROR_TIMEOUT`, size inválido `87`, `WaitOnAddress` 1/2/4/8, thread waiter `WaitOnAddress`→`WakeByAddressSingle`→`WaitForSingleObject`; via `api-ms-win-core-synch-l1-2-0.dll` forwarder; saída `waitaddr\n`, exit `0` | Sincronização por endereço |
| `tl_fiber.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `ConvertThreadToFiber`/`ConvertThreadToFiberEx`/`ConvertFiberToThread`/`CreateFiber`/`CreateFiberEx`/`SwitchToFiber`/`DeleteFiber`/`GetFiberData` | Fixture fibras: `ConvertThreadToFiberEx` com flags, `CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread`; saída `fiber\n`, exit `0` | Fibras |
| `tl_toolhelp.exe` | PE32+ AMD64 | Não | `KERNEL32.dll` — `CreateToolhelp32Snapshot`/`Process32FirstW`/`Process32NextW`/`OpenProcess`/`GetCurrentProcessId` | Fixture Toolhelp: `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` enumera `/proc`, `Process32FirstW`/`NextW` com `PROCESSENTRY32W` 568 bytes valida `dwSize`, `OpenProcess` via `/proc/[pid]` e `CloseHandle` para snapshot/process; saída `toolhelp\n`, exit `0` | Processos |
| `tl_shell.exe` | PE32+ AMD64 | Não | `SHELL32.dll` — `SHGetKnownFolderPath`/`SHGetFolderPathW`/`SHGetFolderPathAndSubDirW`/`ShellExecuteW`/`ShellExecuteExW` | Fixture SHELL32: `FOLDERID_RoamingAppData`→`en-US` path, `CSIDL_APPDATA`/`TestSub`, `ShellExecuteW` `42`, `ShellExecuteExW` dummy `hProcess`; saída `shell\n`, exit `0` | Pastas conhecidas |
| `tl_gdiex.exe` | PE32+ AMD64 | Não | `GDI32.dll` — `CreateFontW`/`SetDCBrushColor`/`SetDCPenColor`; `gdiplus.dll` — 8 APIs; `UxTheme.dll` — `SetWindowTheme`; `WINMM.dll` — `timeSetEvent`; `dbghelp.dll` — `SymFromAddr` | Fixture GDI estendido: `CreateFontW` wide, `SetDCBrush/PenColor`, `GdiplusStartup`/`GdipCreateBitmapFromStream`/`Clone`/`HBITMAP`/`Dispose`/`Alloc/Free`, `SetWindowTheme`, `timeSetEvent` `1`, `SymFromAddr` stub; `USER32` `GetDC`; saída `gdiex\n`, exit `0` | GDI estendido |
| `tl_com.exe` | PE32+ AMD64 | Não | `ole32.dll` — `CoInitialize`/`CoInitializeEx`/`CoUninitialize`/`CoCreateInstance`/`CoGetClassObject`/`OleInitialize`/`OleUninitialize`/`CoTaskMemAlloc/Free` | Fixture COM mínimo: `CoInitialize` `S_OK`, `CoCreateInstance` `REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`, `OleInitialize`; saída `com\n`, exit `0` | COM mínimo |
| `tl_powr.exe` | PE32+ AMD64 | Não | `POWRPROF.dll` — `PowerGetActiveScheme`/`PowerSetActiveScheme`/`CallNtPowerInformation`; `IPHLPAPI.DLL` — `GetAdaptersInfo`/`GetAdaptersAddresses`/`if_nametoindex` | Fixture energia/rede: `PowerGetActiveScheme` GUID `Balanced` `CoTaskMemFree`, `CallNtPowerInformation` `0`, `GetAdaptersInfo` `0`, `if_nametoindex` `1`; saída `powr\n`, exit `0` | Energia/rede |
| `simple_todo.exe` | PE32+ AMD64 | mingw-w64 CRT | 105 imports em `GDI32`, `KERNEL32`, `msvcrt`, `SHELL32` e `USER32` | **Suportado no subconjunto da Fase 12**: fonte pinada no commit `bcdf3d5fcebb8c0b445edb791d54511194c1b6ca` com overlay Linux versionado; build e `--report` resolvem 105/105; `targetapp_simple_todo_gui_smoke` cobre o fluxo principal, persistência, menu da bandeja, encerramento pela bandeja e fechamento da janela, com coordenadas do layout Linux | Fase 12 |

As fontes e manifestos das fixtures ficam em `tests/samples/`. Os binários são produtos de build e ficam em `build/<preset>/tests/samples/generated/`.

## Leitor de PE (Fase 1)

O leitor de PE (`include/tradutorlinux/pe/pe_reader.hpp`, `src/pe/pe_reader.cpp`) valida e interpreta:

- DOS header, assinatura PE, COFF header e optional header PE32+ (magic `0x20B`).
- Tabela de seções, com verificação de que headers e dados crus cabem no arquivo.
- Import table por nome (hint) e por ordinal, com limites de DLLs e símbolos.
- Delay import table (`IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`) por nome e ordinal,
  quando os descritores usam RVAs (`grAttrs=0x1`).
- Diretório de exceções x64 (`.pdata`/`.xdata`) com `RUNTIME_FUNCTION`,
  `UNWIND_INFO` versão 1, handlers reconhecidos e cadeias validadas.
- Base relocations por bloco e entrada.

Comportamento de rejeição:

| Entrada | Resultado |
|---|---|
| Arquivo truncado no meio de qualquer estrutura | `Truncated` |
| Assinatura DOS/PE ausente, offsets inconsistentes, tamanhos inválidos | `Malformed` |
| Arquitetura diferente de `x86-64` (machine `0x8664`) | `UnsupportedArchitecture` |
| Optional header PE32 (magic `0x10B`) ou outro formato | `UnsupportedFormat` |
| Descriptor delay-import com atributos diferentes de `0x1` | `UnsupportedMechanism` |
| Versão/opcode futuro de `UNWIND_INFO` estruturalmente válido | `UnsupportedMechanism` |
| Tabela `.pdata`/`.xdata`, RVA, código ou cadeia de unwind inválidos | `Malformed` |

O CLI expõe o leitor via `--trace` (eventos do componente `pe`, ver `docs/diagnostico.md`) e via resumo em `stderr`. A saída do leitor é comparada em teste de integração com `llvm-readobj` para as fixtures geradas.

O contrato de desempilhamento e as quatro APIs `Rtl*` promovidas ficam em
[`arquitetura/unwinding-x64.md`](arquitetura/unwinding-x64.md). O runtime não
despacha exceções nem executa handlers nesta fase.

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

O resolvedor (`include/tradutorlinux/loader/import_resolver.hpp`, `src/loader/import_resolver.cpp`) percorre as import tables estática e atrasada do PE, procura cada DLL no registro de módulos internos e grava o endereço resolvido no slot correspondente da IAT da imagem mapeada. Os módulos internos registrados embutidos são declarados em `include/tradutorlinux/loader/module.hpp` e `src/loader/module.cpp`; o contrato (registro, tabela de exports, ordinais internos, ABI) está em `docs/arquitetura/imports.md`.

O contexto mínimo de processo (`include/tradutorlinux/loader/process.hpp`, `src/loader/process.cpp`) mapeia a imagem, resolve imports e prepara a pilha do thread inicial com guard page; o entry point nunca é executado nesta fase.

Comportamento de rejeição:

| Condição | `status` no trace | Resultado |
|---|---|---|
| DLL não registrada | `unknown-dll` | `5` (`Unsupported`) |
| Símbolo não exportado pela DLL | `unknown-symbol` | `5` |
| Ordinal não exportado pela DLL | `unknown-ordinal` | `5` |
| Símbolo conhecido sem implementação | `not-implemented` | `5` |
| Descriptor delay-import com atributos não-RVA | `unsupported-mechanism` | `5` |
| Slot da IAT fora das seções mapeadas | `unsupported-mechanism` | `5` |

Em qualquer falha o entry point não é executado e todas as entradas são reportadas no trace (ver `docs/diagnostico.md`).

**Forwarders e API Sets (inspirado em Wine `dlls/*/*.spec`):** `KERNELBASE.dll` encaminha para `KERNEL32.dll`; `api-ms-win-*` e `ext-ms-win-*` encaminham para o provedor real (`KERNEL32`, `USER32`, `GDI32`, `ADVAPI32`, `WS2_32`, `SHELL32`, `ole32`, `SHLWAPI`, `version`, `WINMM`, `COMCTL32`, `COMDLG32`, `IMM32`, `PSAPI`, `msvcrt`) — ver `src/loader/module.cpp:52` (`is_api_set_dll`/`is_kernelbase_dll`/`find_export_forwarded`). Falha de símbolo em API Set vira `unknown-symbol`, não `unknown-dll`.

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
| `USER32.dll` | `RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` etc. | Suportado | Wrappers para `A` via `wide_to_utf8`/`utf8_to_wide`; `RegisterClassExW` converte `WNDCLASSEXW` (80 bytes), `CreateWindowExW` converte classe/título, `SetWindowTextW`/`GetWindowTextW`/`GetWindowTextLengthW` convertem, `FindWindowW`/`SendMessageW`/`AppendMenuW` delegam |
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
pintura mínima (`BeginPaint`/`TextOut`/`EndPaint`). O driver valida também os
traces de contrato do message loop: `GetMessageA ... result="quit"`,
`ExitProcess ... mechanism="guest-transfer"`, `TranslateMessage ...
status="translated"`, `SetTimer`/`GetMessageA(WM_TIMER)`/`KillTimer`,
`BeginPaint`, `TextOut`, `Rectangle` e `FillRect` — ver
[`gui-x11.md`](arquitetura/gui-x11.md).

## Simple Todo C (Fase 12)

O alvo `Efeckc17/simple-todo-c` é baixado por archive pinado e hash SHA-256 em
`tests/targets/CMakeLists.txt`. O recurso `app.rc` é gerado no diretório de
build com o manifesto e o ícone upstream; `tests/targets/manifests/simple_todo.json`
fixa a lista de 105 imports. O build aplica os overlays
`tests/targets/patches/simple_todo_linux.patch` e
`simple_todo_linux_autorun.patch` e `simple_todo_linux_close.patch`: a tela
ganha layout Linux, a opção de autorun no Windows é removida e o fechamento da
janela destrói o processo. O teste `targetapp_simple_todo_gui_smoke` usa
um Xvfb próprio, `APPDATA=appdata` relativo ao diretório de teste e verifica o
fluxo de adicionar, editar, buscar, concluir, excluir, esconder, mostrar e
sair pelo menu emulado.

| Módulo | APIs adicionais | Estado | Limite publicado |
|---|---|---|---|
| `USER32.dll` | `RegisterClassA`, controles lógicos via `CreateWindowExA`, `SendMessageA`, `Get/SetWindowTextA`, foco, geometria, `WM_COMMAND` e `WM_NOTIFY` | Implementado para o alvo | Não são janelas X11 filhas; EDIT, BUTTON, COMBOBOX, STATIC e SysListView32 são desenhados e roteados por uma side-table |
| `GDI32.dll` | `CreateFontA`, `CreateSolidBrush`, `DeleteObject`, `SetBkColor`, `SetTextColor` | Implementado para o alvo | Tokens de fonte/brush e cores têm efeito limitado; o desenho usa o GC X11 mínimo |
| `SHELL32.dll` | `Shell_NotifyIconA` | Implementado para o alvo | O ícone de bandeja é apenas um contrato lógico; o menu é uma janela popup X11, sem integração com o tray do desktop |
| `msvcrt.dll` | `_acmdln`, `_ismbblead`, `_time64`, `_localtime64`, `strftime`, `_strlwr` | Implementado para o alvo | Locale/DBCS continuam no subconjunto C/ANSI do runtime |

O smoke de integração sob Xvfb é o contrato de regressão do fluxo do alvo e
verifica também o evento de fechamento da janela. Isso não constitui suporte
geral a aplicativos Win32;
permanecem válidas as limitações específicas das APIs listadas acima.

## Aplicativos-alvo reais (Fase 8)

A Fase 8 mede progresso por aplicativos reais, e não apenas por fixtures. Os
primeiros alvos escolhidos são utilitários de console pequenos, de código
aberto e compilados em CI com `mingw-w64`. Cada alvo é fixado por versão,
toolchain e lista de imports; a lista real é capturada por `llvm-readobj` e por
`--report` do runtime e protegida por testes com o label `targetapp`.

As fontes são baixadas com hash SHA-256 verificado pelo módulo
`tests/targets/CMakeLists.txt` (opção `TL_BUILD_TARGET_APPS=ON`, usada no job
`target-apps` do CI). O `--report` lista os imports reais, agrupados por DLL
com contagem de resolução, e classifica o alvo como `result: supported`
(exit code `0`) ou `result: unsupported` (exit code `5`), sem mapear nem
executar a imagem; inclui linha `compatibility:` com porcentagem de imports
resolvidos e `execution-result: not-attempted`. O script
`tests/targets/verify_target_report.cmake` aceita as duas respostas, exige que
todo import do manifest apareça listado sob o grupo `dll:` correspondente e
valida as linhas `compatibility:` e `execution-result:`.

Os scripts de execução e2e (`verify_target_run.cmake`,
`verify_target_run_bytes.cmake` e `verify_target_conversion.cmake`) categorizam
o resultado em:
- `supported` — execução concluída, saída idêntica ao ouro
- `failed` — terminou por sinal, timeout ou exit code inesperado
- `incorrect` — saída diverge do ouro
- `not-attempted` — sem execução (`--report` apenas)

| Aplicativo | Versão / toolchain | Imports (símbolos) | Estado |
|---|---|---|---|
| `xxd.exe` | vim `v9.2.0957` (`src/xxd.c`), `-O2 -s` | `KERNEL32.dll` (16), `msvcrt.dll` (57) | **Executa de ponta a ponta**: saída byte-idêntica ao `xxd` do sistema nos modos padrão e `-p`, exit `0`; arquivo inexistente → exit `2` com erro em stderr. Regressão e2e em CTest (ouro em `tests/targets/golden/xxd/`, 3 testes) |
| `bzip2.exe` | bzip2 `1.0.8`, `-O2 -s` | `KERNEL32.dll` (13), `msvcrt.dll` (69) | **Executa de ponta a ponta**: compressão (`-c`) e descompressão (`-d`) de arquivo; saída válida verificada com `bzip2` nativo nos dois sentidos; exit `0` |
| `dos2unix.exe` | dos2unix `7.5.6`, `-O2 -DD2U_UNIFILE -s` | `KERNEL32.dll` (23), `msvcrt.dll` (67), `SHELL32.dll!CommandLineToArgvW` (1) | **Executa de ponta a ponta no fluxo validado**: `--report` resolve 91/91 imports; regressões convertem CRLF/misto para LF e processam `uni_el_*.txt` com nome UTF-8, usando `CommandLineToArgvW` e enumeração `W`; exit `0` |
| `unix2dos.exe` | dos2unix `7.5.6`, `-O2 -DD2U_UNIFILE -s` | idem `dos2unix.exe` | **Executa de ponta a ponta no fluxo validado**: `--report` resolve 91/91 imports; regressão converte LF para CRLF por stdout; exit `0` |

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
| `msvcrt.dll` | `_amsg_exit`, `abort`, `exit`, `atexit`, `_onexit` | Suportado | Terminam via `ExitProcess`; `atexit` e `_onexit` acumulam handlers executados no encerramento |
| `msvcrt.dll` | `_errno`, `getenv`, `strerror` | Suportado | Célula `errno` global do hospedeiro; `getenv` lê o ambiente do host |
| `msvcrt.dll` | `fopen`/`fclose`/`fflush`/`ferror`/`fseek`/`ftell`/`rewind`/`fgetc`/`fputc`/`fputs`/`fprintf`/`vfprintf`/`fwrite` | Suportado | I/O em `GuestFile` (layout `_iobuf` de 48 bytes), unbuffered via `::write` com loop `EINTR` |
| `msvcrt.dll` | `_open`/`_fdopen`/`_fileno`/`_isatty`/`_setmode`/`__iob_func` | Suportado | Tradução de flags `_O_*`; modo por fd (`_O_TEXT`/`_O_BINARY`) refletido na flag `_IOSTRG` do `GuestFile` |
| `msvcrt.dll` | `malloc`/`calloc`/`free`, `memcpy`/`memset` | Suportado | Alocação e memória diretas do hospedeiro |
| `msvcrt.dll` | `strlen`/`strcmp`/`strncmp`/`strcpy`/`strncpy`/`strstr`/`strcat`/`strtol`/`strtoul`/`wcslen`/`isalnum`/`isspace`/`toupper` | Suportado | Semântica libc para ASCII/latin-1 |
| `msvcrt.dll` | `fgetc`/`fread`/`ungetc` | Suportado | `fgetc` lê byte e verifica `charbuf` (pushback); `fread` lê `count` elementos de `size` bytes; `ungetc` devolve caractere ao stream via `charbuf` do `GuestFile` |
| `msvcrt.dll` | `memmove`/`remove`/`_stat64` | Suportado | `memmove` com tratamento de overlap; `remove` delega ao host; `_stat64` preenche o `struct _stat64` do MinGW (pack 8, `st_mode` em `0x06`, tamanho 56 bytes) a partir do `stat()` do host |
| `msvcrt.dll` | `localeconv`, `___lc_codepage_func`, `___mb_cur_max_func` | Suportado | Locale C fixo: `lconv` estático, code page `1252`, `mb_cur_max == 1` |
| `msvcrt.dll` | `signal` | Suportado | Registra handlers em tabela por sinal; nenhuma entrega real ao convidado |
| `KERNEL32.dll` | `VirtualQuery` | Suportado | Preenche `MEMORY_BASIC_INFORMATION` (48 bytes); alocações privadas do `VirtualAlloc` usam a base/tamanho rastreados pelo runtime, e os demais mapeamentos usam `/proc/self/maps`; `State=MEM_COMMIT`, `Protect`/`AllocationProtect` mapeados de `rwx`, `Type=MEM_IMAGE`/`MEM_PRIVATE` |
| `KERNEL32.dll` | `VirtualProtect` | Suportado | `mprotect` sobre a página alinhada dentro da região; escreve a proteção antiga em `*lpflOldProtect`; rejeita região que não contém `[address, address+size)` |
| `KERNEL32.dll` | `MultiByteToWideChar` / `WideCharToMultiByte` | Suportado | CP `0` (ACP → 1252), `1252` e `65001` (UTF-8), conversões manuais sem locale; contagem com ponteiros `NULL`; `MB_ERR_INVALID_CHARS`; `ERROR_INSUFFICIENT_BUFFER` (122) |
| `KERNEL32.dll` | `Initialize/Enter/Leave/DeleteCriticalSection` | Suportado | No-ops com validação de ponteiro (convidado single-thread → exclusão trivial) |
| `KERNEL32.dll` | `TlsGetValue` | Suportado | Retorna `NULL` com `ERROR_SUCCESS` para slot não usado (TLS do CRT não é inicializado por nenhum callback do xxd) |
| `KERNEL32.dll` | `GetConsoleMode` / `SetConsoleMode` | Suportado | `GetConsoleMode` devolve `0x3` e `TRUE` só para fd com `isatty`; caso contrário `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `IsDBCSLeadByteEx` | Suportado | Sempre `FALSE` (sem DBCS) |
| `KERNEL32.dll` | `Sleep` | Suportado | `nanosleep` com loop `EINTR` |
| `KERNEL32.dll` | `SetUnhandledExceptionFilter` | Suportado | Registra o handler em célula global (nunca invoca); retorna o anterior |
| `KERNEL32.dll` | `GetModuleHandleA/W` | Suportado | Retorna handle `0x1000` para módulos registrados (inclui `api-ms-win-*`/`KERNELBASE` via forwarders, extração de filename de caminhos `C:\...`), `NULL` + `ERROR_FILE_NOT_FOUND` caso contrário; `W` converte via `wide_to_utf8` |
| `KERNEL32.dll` | `GetModuleHandleExA/W` | Suportado | Flags `PIN`/`UNCHANGED_REFCOUNT`/`FROM_ADDRESS`; `FROM_ADDRESS` aceita `0x1000` ou endereço dentro da imagem (`g_guest_image_base/size`); valida `phModule` via `mapped_guest_range`; erro `ERROR_INVALID_PARAMETER`/`FILE_NOT_FOUND` |
| `KERNEL32.dll` | `LoadLibraryA/W` / `LoadLibraryExA/W` | Suportado | Normaliza caminho (filename após `\/:`), case-insensitive, adiciona `.dll`; verifica `is_module_registered_forwarded`; retorna `0x1000` ou `NULL` + `ERROR_MOD_NOT_FOUND` (126); `Ex` ignora `hFile`/`flags` |
| `KERNEL32.dll` | `FreeLibrary` | Suportado | Aceita `0x1000` ou base do exe; `NULL`/inválido → `0` + `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `GetProcAddress` | Suportado | Busca global (`find_export_global`); suporta ordinal via `MAKEINTRESOURCE` (`addr<=0xFFFF` → `find_export_by_ordinal_global`); valida `proc_name` e `module` (`NULL` permitido); `ERROR_PROC_NOT_FOUND` (127) ou `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `GetVersionExA/W` | Suportado | Reporta Windows 10 (10.0.19044, `VER_PLATFORM_WIN32_NT=2`, `szCSDVersion` zero, `wServicePackMajor/Minor=0`, `wSuiteMask=0`, `wProductType=1`); valida `lpVersionInformation` e `dwOSVersionInfoSize` (A:148/156, W:276/284); `ERROR_INVALID_PARAMETER` em ponteiro/size inválido |
| `KERNEL32.dll` | `VerifyVersionInfoW` / `VerSetConditionMask` | Suportado | `VerifyVersionInfoW` valida `lpVersionInfo`/`dwTypeMask` e retorna `TRUE` (versão sempre compatível); `VerSetConditionMask` codifica 3 bits por `TypeMask` (`&0x07`, `shift=i*3`) como no Wine |
| `KERNEL32.dll` | `GetUserDefaultLocaleName` | Suportado | Retorna `en-US` (wide, `0x0409`); `NULL/0` → `6` (inclui NUL); buffer <6 → `0` + `ERROR_INSUFFICIENT_BUFFER` (122); valida `mapped_guest_range` |
| `KERNEL32.dll` | `LocaleNameToLCID` | Suportado | Converte `en-US`→`0x0409`, `pt-BR`→`0x0416`, `en`→`0x09`, `pt`→`0x16` (case-insensitive); `NULL`/vazio/desconhecido → `0` + `ERROR_INVALID_PARAMETER` |
| `KERNEL32.dll` | `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll` | Suportado | `WaitOnAddress` compara `*Address` vs `*CompareAddress` (`size` 1/2/4/8, alinhado, `mapped_guest_range`); se diferente retorna `1`; se igual espera por `Wake*` ou `dwMilliseconds` (`INFINITE`→`wait`, `0`→timeout imediato) via `mutex`+`cv`+`version` por endereço; `WakeSingle`→`notify_one`, `WakeAll`→`notify_all`; timeout → `0` + `ERROR_TIMEOUT` (1460); exposto via `KERNEL32` e `api-ms-win-core-synch-l1-2-0.dll` (forwarder) |
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
- `bzip2.exe` tem todos os imports de `msvcrt.dll` implementados e sua
  regressão e2e cobre compressão e descompressão byte-idênticas.
- `dos2unix`/`unix2dos` agora exercitam o caminho `W` (`GetCommandLineW`,
  `FindFirstFileW`/`FindNextFileW`/`FindClose`, `GetFileAttributesW`,
  `_wfopen`, `wcs*`) e `SHELL32.dll!CommandLineToArgvW`; os fluxos validados
  usam somente caminhos relativos, UTF-8 e o curinga `*`.
- Nenhum alvo usa `GetStartupInfoA`/`GetEnvironmentStringsA` diretamente: o
  `crt2.o` do mingw delega a linha de comando e o ambiente ao `__getmainargs`
  de `msvcrt.dll`, então essas APIs são dependência interna do CRT mínimo, e
  não do aplicativo.

## Sistema de arquivos (Fase 10)

O subsistema de arquivos expande o `CreateFileA`/`ReadFile`/`WriteFile`/
`CloseHandle` da Fase 5 com APIs de manipulação de diretórios, atributos e
enumeração. A tradução de caminhos Windows (`\\` → `/`) é reutilizável via
`translate_windows_path()`; `CreateFileA` rejeita letras de drive e caminhos
absolutos, enquanto o CRT aceita caminhos absolutos Linux para os aplicativos
que recebem arquivos do host como argumentos.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `GetFileSize` | Suportado | Retorna tamanho do arquivo aberto via `FileSlot.file_size`; suporta ponteiro `high_size` para arquivos > 4 GiB |
| `KERNEL32.dll` | `SetFilePointer` | Suportado | Seek por `FILE_BEGIN`/`FILE_CURRENT`/`FILE_END`; suporta ponteiro `high_distance`; atualiza `FileSlot.position` |
| `KERNEL32.dll` | `GetFileAttributesA` | Suportado | `stat()` + bits `FILE_ATTRIBUTE_DIRECTORY`/`FILE_ATTRIBUTE_ARCHIVE`/`FILE_ATTRIBUTE_READONLY` |
| `KERNEL32.dll` | `DeleteFileA` | Suportado | `unlink()` com mapeamento de erros |
| `KERNEL32.dll` | `MoveFileA` | Suportado | `rename()` com mapeamento de erros |
| `KERNEL32.dll` | `CreateDirectoryA` | Suportado | `mkdir()` com permissão 0777 |
| `KERNEL32.dll` | `FindFirstFileA` | Suportado | Abre `opendir()` + `readdir()` com padrão simples (`*` e correspondência exata); preenche `WIN32_FIND_DATAA` simplificado |
| `KERNEL32.dll` | `FindNextFileA` | Suportado | Continua iteração com o mesmo padrão |
| `KERNEL32.dll` | `FindFirstFileW` | Suportado | Converte UTF-16 para UTF-8, enumera com o mesmo padrão simples e preenche `WIN32_FIND_DATAW` simplificado |
| `KERNEL32.dll` | `FindNextFileW` | Suportado | Continua enumeração wide e converte o nome encontrado para UTF-16 |
| `KERNEL32.dll` | `FindClose` | Suportado | Fecha `DIR*` e libera slot |
| `KERNEL32.dll` | `GetFileAttributesW` | Suportado | Converte o caminho UTF-16 e delega ao mesmo `stat()` da variante A |
| `KERNEL32.dll` | `GetCurrentDirectoryA/W` | Suportado | Retorna o diretório de execução convertido para caminho Windows lógico: `C:\\...` dentro do prefixo, `Z:\\...` para arquivo/diretório externo |
| `KERNEL32.dll` | `GetModuleFileNameA/W` | Suportado | Retorna o módulo definido via `set_guest_module_path()` como caminho Windows lógico: aplicação instalada no prefixo usa `C:\\...`; setup externo usa `Z:\\...` |
| `KERNEL32.dll` | `GetFullPathNameW` | Suportado | Normalização Windows completa (Wine `dlls/kernel32/path.c`): resolve relativo via `GetCurrentDirectory`, colapsa `.`/`..`, trata `C:`, `\` e `\\` (UNC); `file_part` aponta para após último `\`/`:` |
| `KERNEL32.dll` | `GetFullPathNameA` | Suportado | Conversão `A` → `W` com mesma normalização; buffer insuficiente retorna `tamanho+1` e `ERROR_INSUFFICIENT_BUFFER` |
| `SHELL32.dll` | `CommandLineToArgvW` | Suportado | Divide a linha de comando UTF-16 em argumentos, preservando grupos entre aspas; o bloco único retornado é liberado por `LocalFree` |
| `SHELL32.dll` | `SHGetKnownFolderPath` | Suportado | Mapeia `FOLDERID_RoamingAppData`→`XDG_CONFIG_HOME`/`$HOME/.config`, `LocalAppData`→`XDG_DATA_HOME`/`$HOME/.local/share`, `ProgramData`→`/tmp/ProgramData`, `Desktop`/`Documents`/`Downloads`→`$HOME/...`; aloca via `CoTaskMemAlloc` (`malloc`), `ensure_directory_exists` |
| `SHELL32.dll` | `SHGetFolderPathW` | Suportado | `CSIDL_APPDATA`/`LOCAL_APPDATA`/`COMMON_APPDATA`/`DESKTOP`/`PERSONAL`→`$HOME/...`; copia para `pszPath[260]` |
| `SHELL32.dll` | `SHGetFolderPathAndSubDirW` | Suportado | Base `CSIDL` + `pszSubDir` (`\`→`/`) → `base/sub`; garante diretório |
| `SHELL32.dll` | `ShellExecuteW` / `ShellExecuteExW` | Suportado | `ShellExecuteW` valida `lpFile` wide e retorna `42` (>32); `ShellExecuteExW` valida `cbSize>=60` e preenche `hProcess` dummy, retorna `1` |
| `GDI32.dll` | `CreateFontW` | Suportado | Wrapper `wide_to_utf8` → `CreateFontA`; valida `face_name` wide, token estático |
| `GDI32.dll` | `SetDCBrushColor` / `SetDCPenColor` | Suportado | Stub retorna `0`, `ERROR_SUCCESS` |
| `gdiplus.dll` | `GdiplusStartup` / `GdiplusShutdown` / `GdipAlloc` / `GdipFree` / `GdipCreateBitmapFromStream` / `GdipCloneImage` / `GdipDisposeImage` / `GdipCreateHBITMAPFromBitmap` | Suportado | `GdiplusStartup` aloca token `0x1`, `GdipAlloc` `malloc`, `GdipFree` `free`, `GdipCreateBitmapFromStream`/`Clone`/`HBITMAP` retornam dummy `0` |
| `UxTheme.dll` | `SetWindowTheme` | Suportado | Valida `hwnd` e wstrings, retorna `S_OK` (0) |
| `WINMM.dll` | `timeSetEvent` | Suportado | Stub retorna `1` |
| `dbghelp.dll` | `SymFromAddr` | Suportado | Valida `process`/`displacement`/`symbol`, `displacement=0`, retorna `0` (não encontrado) mas sem crash |
| `POWRPROF.dll` | `PowerGetActiveScheme` / `PowerSetActiveScheme` / `CallNtPowerInformation` | Suportado | `PowerGetActiveScheme` aloca GUID `Balanced` via `malloc`, `PowerSetActiveScheme` `S_OK`, `CallNtPowerInformation` `memset` `0` |
| `IPHLPAPI.DLL` | `GetAdaptersInfo` / `GetAdaptersAddresses` / `if_nametoindex` | Suportado | `GetAdaptersInfo` `0` sem adapters, `GetAdaptersAddresses` `0`, `if_nametoindex` `1` |

### Limitações conhecidas

- `WIN32_FIND_DATAA` é 328 bytes (padded), não 336 como no Windows nativo;
  o convidado não deve depender do tamanho exato da estrutura.
- `FindFirstFileA` só aceita `*` como curinga; `?` e sequências `[a-z]` não
  são suportados.
- `FindFirstFileW`/`FindNextFileW` têm a mesma limitação de curinga e retornam
  somente a estrutura wide mínima usada pelos alvos atuais.
- `CommandLineToArgvW` cobre aspas e separação por espaço usadas pelos alvos;
  as regras completas de escape com barras invertidas antes de aspas ainda não
  fazem parte do subconjunto publicado.
- `CreateFileA` continua limitado a caminhos relativos sem letra de drive.
- As APIs wide de arquivo cobrem o subconjunto exercitado por `tl_files_wide`:
  `CreateFileW`, tamanho/posição, atributos, tempos, cópia/movimentação,
  diretórios e nomes finais; não inventam letras de drive nem aceitam caminhos
  absolutos Windows.
- `FileSlot` agora rastreia `file_size` e `position`; `ReadFile` e `WriteFile`
  atualizam a posição automaticamente.
- `GetFileAttributesA` para arquivos inexistentes retorna `0xFFFFFFFF` com
  `ERROR_FILE_NOT_FOUND`.
- `GetCurrentDirectoryA/W` reflete apenas o processo convidado isolado. Em
  instalações e entradas de catálogo, o diretório inicial está dentro de
  `drive_c`; a execução não altera o diretório do launcher.
- `GetModuleFileNameA/W` depende de `set_guest_module_path()` chamado antes da
  execução; o loader conserva o caminho Linux internamente, mas a API devolve
  apenas a representação lógica `C:\\...` ou `Z:\\...`.
- `MultiByteToWideChar` e `WideCharToMultiByte` suportam CP_UTF8 (65001) para
  conversão UTF-8/UTF-16; surrogates pair são suportados.
- 15 testes unitários novos em `tests/test_win32.cpp` cobrem `GetFileSize`,
  `SetFilePointer` (seek beginning/end/negative), `GetFileAttributesA`
  (file/directory/nonexistent), `DeleteFileA` (existente/inexistente),
  `MoveFileA` (existente/inexistente), `CreateDirectoryA` (novo/duplicado),
  `FindFirstFileA`/`FindClose`, `GetCurrentDirectoryA/W`,
  `GetModuleFileNameA/W` e conversão UTF-8/UTF-16 com caracteres acentuados.
- Os fluxos dos alvos reais são cobertos por `targetapp_dos2unix_eol`,
  `targetapp_unix2dos_eol` e `targetapp_dos2unix_unicode-glob`; os arquivos de
  entrada CRLF/LF vêm da fonte pinada do dos2unix e o ouro UTF-8 está em
  `tests/targets/golden/dos2unix/`.

## Recursos PE, processos e rede

O loader expõe a faixa do diretório de recursos da imagem corrente somente após
mapear headers/seções. `FindResourceW` percorre diretórios com contagem e
offsets validados; `LoadResource`/`LockResource` devolvem uma visão somente
leitura e `SizeofResource` nunca ultrapassa a imagem. O fixture
`tl_resources.exe` protege esse contrato e o `--report` continua sem mapear ou
executar o entry point.

Handles de eventos, mutexes, semáforos, processos e arquivos são tokens opacos
validados pelo runtime. `WaitForMultipleObjects` aceita até 64 handles e
retorna timeout/índice conforme o subconjunto testado. `CreateProcessW` só
aceita PE32+ x86-64 com caminho relativo; o filho passa por `parse_pe`,
relocations, imports e isolamento antes do entry point. A implementação atual
usa um pipe de resultado, suporta `GetExitCodeProcess` e `TerminateProcess` e
não executa um programa Windows diretamente pelo Linux.
As tabelas internas ainda são separadas por família de recurso; a validação do
tipo ocorre pelo espaço de tokens e a unificação em uma tabela única continua
pendente.

`WS2_32.dll` é um módulo separado. O contrato inicial aceita AF_INET, TCP/UDP,
`getaddrinfo` para `localhost`/loopback, conversões de ordem de bytes e
`WSAPoll`. A fixture nunca acessa Internet; no sandbox sem permissão de socket,
o teste retorna um skip controlado, enquanto a validação com loopback permitido
passa de ponta a ponta.

## Registro genérico

`ADVAPI32.dll` não possui mais chave ou valor específicos do Todo. O subconjunto
de `RegCreateKeyEx[A/W]`, `RegOpenKeyEx[A/W]`, `RegSetValueEx[A/W]`,
`RegQueryValueEx[A/W]`, `RegDeleteValue[A/W]` e `RegCloseKey` usa chaves/valores
genéricos e persiste bytes, tipo e nomes UTF-8/UTF-16 em um arquivo por escopo
(`APPDATA`, ou `TL_REGISTRY_FILE` para testes). Segurança, ACL, hive real,
COM e `CRYPT32` continuam fora deste contrato.

## COM mínimo (ole32)

`ole32.dll` expõe `CoInitialize`/`CoUninitialize`/`CoTaskMemAlloc` e camada COM mínima para testes de inicialização. `CoCreateInstance`/`CoGetClassObject` validam `rclsid`/`riid`/`ppv` via `mapped_guest_range` e retornam `REGDB_E_CLASSNOTREG` (`0x80040154`) ou `CLASS_E_NOAGGREGATION` (`0x80040110`); `OleInitialize`/`OleUninitialize` são stubs `S_OK`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `ole32.dll` | `CoInitialize` / `CoInitializeEx` | Suportado | Retorna `S_OK` (0), ignora `reserved`/`coInit` |
| `ole32.dll` | `CoUninitialize` / `OleUninitialize` | Suportado | No-op |
| `ole32.dll` | `OleInitialize` | Suportado | Retorna `S_OK` |
| `ole32.dll` | `CoCreateInstance` / `CoGetClassObject` | Suportado | Valida `rclsid`/`riid`/`ppv`, `unkOuter==nullptr` senão `CLASS_E_NOAGGREGATION`, senão `REGDB_E_CLASSNOTREG`, `*ppv=nullptr` |
| `ole32.dll` | `CoTaskMemAlloc` / `CoTaskMemFree` / `CoTaskMemRealloc` | Suportado | `malloc`/`free`/`realloc` do host |

## Concorrência (Fase 11)

O subsistema de concorrência adiciona suporte a threads convidadas, TLS,
sincronização por mutexe e handles de thread. O runtime executa no mesmo
processo filho; cada thread convidada recebe seu próprio TEB/GS, stack e
`thread_local` isolado. Handles de thread são codificados por endereço
(`kThreadHandleBase + índice`).

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `CreateThread` | Suportado | Aloca stack com guard page, TEB, `arch_prctl(GS)`, cria `std::thread` com wrapper que preserva GS; retorna handle de thread |
| `KERNEL32.dll` | `ExitThread` | Suportado | `longjmp` para o `setjmp` do wrapper; thread termina sem encerrar o processo |
| `KERNEL32.dll` | `WaitForSingleObject` | Suportado | Thread, evento, mutex, semáforo, processo e arquivo síncrono; suporta `INFINITE` e timeout |
| `KERNEL32.dll` | `WaitForMultipleObjects` | Suportado | Até 64 handles válidos, espera any/all e retorno por índice; polling controlado para o subconjunto atual |
| `KERNEL32.dll` | `CreateEventA/W`, `SetEvent`, `ResetEvent` | Suportado | Eventos manuais/automáticos com `condition_variable` |
| `KERNEL32.dll` | `CreateMutexA/W`, `ReleaseMutex` | Suportado | Mutex recursivo e ownership pela thread convidada corrente |
| `KERNEL32.dll` | `CreateSemaphoreA/W`, `ReleaseSemaphore` | Suportado | Contagem inicial/máxima e consumo por espera |
| `KERNEL32.dll` | `CloseHandle` | Suportado | Fecha thread/processo/sincronização/arquivo e libera os recursos associados |
| `KERNEL32.dll` | `CreateProcessW` | Suportado no contrato limitado | Cria um filho PE32+ pelo mesmo loader e devolve processo assíncrono; sem drives, WOW64 ou execução nativa direta |
| `KERNEL32.dll` | `GetExitCodeProcess` / `TerminateProcess` | Suportado no contrato limitado | Consulta código e encerra filho isolado via sinal controlado |
| `KERNEL32.dll` | `GetCurrentThreadId` | Suportado | Retorna `thread_local` `g_guest_thread_id` atribuído por `execute_guest_entry` |
| `KERNEL32.dll` | `GetCurrentProcessId` | Suportado | Retorna PID real do processo via `getpid()` |
| `KERNEL32.dll` | `TlsAlloc` | Suportado | Aloca índice de slot `thread_local` (0–63); retorna `0xFFFFFFFF` na exaustão |
| `KERNEL32.dll` | `TlsSetValue` | Suportado | Armazena valor em `g_guest_tls_slots[index]`; rejeita índice inválido |
| `KERNEL32.dll` | `TlsFree` | Suportado | Libera índice para reuso |
| `KERNEL32.dll` | `InitializeCriticalSection` | Suportado | Side-table com `pthread_mutex_t` (máximo 32 entradas) |
| `KERNEL32.dll` | `EnterCriticalSection` | Suportado | `pthread_mutex_lock` via side-table |
| `KERNEL32.dll` | `LeaveCriticalSection` | Suportado | `pthread_mutex_unlock` via side-table |
| `KERNEL32.dll` | `DeleteCriticalSection` | Suportado | `pthread_mutex_destroy` + libera entrada na side-table |
| `KERNEL32.dll` | `ConvertThreadToFiber` / `ConvertThreadToFiberEx` / `ConvertFiberToThread` | Suportado | `Convert*` retorna token `0x*` + `g_current_fiber_data`; `Ex` ignora `flags`; `ConvertFiberToThread` limpa `g_current_fiber_data` |
| `KERNEL32.dll` | `CreateFiber` / `CreateFiberEx` / `SwitchToFiber` / `DeleteFiber` / `GetFiberData` | Suportado | Stub retorna token estático; `CreateFiberEx` ignora `stack_commit/reserve/flags`; `SwitchToFiber`/`DeleteFiber` no-op; `GetFiberData` retorna `g_current_fiber_data` |
| `KERNEL32.dll` | `CreateToolhelp32Snapshot` | Suportado | Enumera `/proc` (`TH32CS_SNAPPROCESS` apenas); retorna `&SnapshotSlot` ou `INVALID_HANDLE_VALUE` (`-1`) + `ERROR_INVALID_PARAMETER`/`NOT_ENOUGH_MEMORY`; `CloseHandle` libera slot |
| `KERNEL32.dll` | `Process32FirstW` / `Process32NextW` | Suportado | Valida `hSnapshot` e `dwSize==568`, preenche `PROCESSENTRY32W` via `/proc/[pid]/status` (`PPid`, `Threads`, `Name`→`szExeFile` wide); `Next` avança `next_index`; fim → `0` + `ERROR_NO_MORE_FILES` (18) |
| `KERNEL32.dll` | `OpenProcess` | Suportado | Valida `/proc/[pid]` existe; retorna token `kProcessHandleBase+pid` ou `NULL` + `87`; `CloseHandle` aceita token via range |

### Limitações conhecidas

- O slot 0 de TLS (`TlsGetValue(0)`) é reservado para o ponteiro ao TEB
  (`NtTib.Self`); o convidado não deve chamar `TlsAlloc` para obter o TEB.
- A side-table de `CRITICAL_SECTION` suporta no máximo 32 seções simultâneas;
  exaustão emite trace de `side-table` com `category="exhaustion"`.
- Handles nomeados não são compartilhados entre processos; o nome é validado,
  mas a tabela é local ao processo host.
- `CreateThread` não suporta `CREATE_SUSPENDED`; `stack_size == 0` usa o
  tamanho padrão (1 MiB).
- `ExitThread` termina somente a thread corrente; não limpa destructors C++.
- O fim de vida de threads convidadas usa trampolim `setjmp`/`longjmp`
  (`thread_local`): `ExitThread` nunca atravessa `pthread_exit`; o `join` real
  acontece em `CloseHandle` (com guarda contra fechamento duplo), que também
  libera a pilha mapeada e invalida o cache de `/proc/self/maps`.
- O fixture `tl_thread.exe` requer mingw-w64 para cross-build; a regressão e2e
  está coberta por `fixture_tl_thread_metadata` e
  `runtime_tl_thread_matches_readobj`.
- 18 testes unitários em `tests/test_win32.cpp` cobrem `TlsAlloc`,
  `TlsSetValue`, `TlsGetValue`, `TlsFree`, `GetCurrentThreadId`,
  `GetCurrentProcessId`, `CRITICAL_SECTION` (init/enter/leave/delete,
  null check, side-table exhaustion), `CloseHandle` (null/garbage),
  `WaitForSingleObject` (invalid handle, timeout), e `TlsSetGetValue`
  com múltiplos slots.
