# Matriz de compatibilidade — runtime

Este documento contém os contratos e limitações do loader, APIs e runtime originalmente registradas na matriz.

## Leitor de PE (Fase 1)

O leitor de PE (`include/tradutorlinux/pe/pe_reader.hpp`, `src/pe/pe_reader.cpp`) valida e interpreta:

- DOS header, assinatura PE, COFF header e optional header PE32+ (magic `0x20B`).
- Tabela de seções, com verificação de que headers e dados crus cabem no arquivo.
- Import table por nome (hint) e por ordinal, com limites de DLLs e símbolos.
- Delay import table (`IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`) por nome e ordinal,
  quando os descritores usam RVAs (`grAttrs=0x1`).
- Diretório de exceções x64 (`.pdata`/`.xdata`) com `RUNTIME_FUNCTION`,
  `UNWIND_INFO` versões 1 e 2, epílogos V2 normalizados, handlers
  reconhecidos e cadeias validadas.
- Base relocations por bloco e entrada.

Comportamento de rejeição:

| Entrada | Resultado |
|---|---|
| Arquivo truncado no meio de qualquer estrutura | `Truncated` |
| Assinatura DOS/PE ausente, offsets inconsistentes, tamanhos inválidos | `Malformed` |
| Arquitetura diferente de `x86-64` (machine `0x8664`) | `UnsupportedArchitecture` |
| Optional header PE32 (magic `0x10B`) ou outro formato | `UnsupportedFormat` |
| Descriptor delay-import com atributos diferentes de `0x1` | `UnsupportedMechanism` |
| Versão 3+/opcode/flag de mecanismo futuro de `UNWIND_INFO` estruturalmente válido | `UnsupportedMechanism` |
| Tabela `.pdata`/`.xdata`, RVA, código ou cadeia de unwind inválidos | `Malformed` |

O CLI expõe o leitor via `--trace` (eventos do componente `pe`, ver `docs/diagnostico.md`) e via resumo em `stderr`. A saída do leitor é comparada em teste de integração com `llvm-readobj` para as fixtures geradas.

O contrato de desempilhamento e despacho SEH fica em
[`arquitetura/unwinding-x64.md`](arquitetura/unwinding-x64.md). O runtime
suporta exceções explícitas V1/V2 fora de epílogos e um subconjunto checked de
C++ `__CxxFrameHandler3`; `__finally`, rethrow nativo completo e sinais Linux
continuam fora do contrato.

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

### Extensões de DLL por aplicativo (B14.4)

Perfis v2 podem declarar DLLs PE32+ AMD64 em `compat/dlls/`. A fixture
`tl_compat_dll_app.exe` e o teste `integration_compat_dll_profile` exercitam o
contrato com uma DLL personalizada, uma dependência PE, exports, imports para
`KERNEL32.dll`, TLS callback, `DllMain`, `LoadLibrary`, `GetProcAddress` e
`FreeLibrary`. O mesmo destino é testado com duas variantes em prefixos
independentes; cada execução recebe somente o provider do seu perfil e a fonte
permanece em `compat/dlls/`.

O provider do perfil precede uma DLL PE existente em `drive_c`, que precede o
provider genérico interno. A precedência também vale por export: um símbolo
ausente na extensão pode ser resolvido pelo provider seguinte. Provider
personalizado ausente, inválido, com import não resolvido, dependência ausente,
ciclo ou attach rejeitado é descartado inteiro e usa fallback quando houver.
Uma falha depois do attach é falha do convidado e não vira fallback silencioso.
`compat/` não é pesquisada nem copiada para `drive_c`, e `--report` não carrega
essas DLLs; o diagnóstico contextual ocorre em `app run --trace`.

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

`VirtualAlloc`, `VirtualFree`, `VirtualProtect` e `VirtualQuery` têm o contrato limitado descrito em
[`runtime-basico.md`](arquitetura/runtime-basico.md).

### Fronteira de memória convidada (E11)

`read_guest_memory` e `write_guest_memory` são as primitivas protegidas para
copiar buffers entre o processo convidado e o host. A implementação usa
`process_vm_readv`/`process_vm_writev` quando permitido pelo Linux e
`/proc/thread-self/mem` como fallback controlado; nenhum dos caminhos precisa
desreferenciar o ponteiro convidado no código C++. O resultado distingue
`Success`, `Partial`, `Unmapped`, `PermissionDenied`, `InvalidArgument` e
`SystemError`.

Os testes cobrem ponteiro nulo, overflow de endereço, página desmontada com
cópia parcial, destino somente leitura e alternância concorrente entre
`PROT_NONE` e leitura/escrita. O validador de strings (`cstring`/UTF-16), os
buffers de entrada do MPR, os caminhos de arquivo (`ReadFile`/`WriteFile`),
`FindFirstFileA/W`, as conversões comuns de caminhos e `GetMessageA` já usam
essa cópia. As saídas de `GetCurrentDirectoryA/W`, `GetModuleFileNameA/W`,
`GetFullPathNameA/W`, `GetFinalPathNameByHandleW`, `GetTempPathA/W`,
`GetTempFileNameW`, `GetDiskFreeSpaceA/W` e APIs relacionadas de caminhos
também usam essa fronteira. `GlobalMemoryStatusEx`, `GlobalMemoryStatus`,
`VirtualQuery`, `VirtualQueryEx`, `VirtualProtect` e
`GetPhysicallyInstalledSystemMemory` publicam suas estruturas e escalares pela
mesma cópia protegida. `WaitForMultipleObjects`, `WaitOnAddress`,
`ReleaseSemaphore`, `INIT_ONCE`, SRW locks, variáveis de condição e
`RegisterWaitForSingleObject` também não interpretam mais diretamente os
buffers convidados. No WININET, `InternetReadFile`,
`InternetQueryDataAvailable`, `HttpQueryInfoW`, `InternetSetOptionW`,
`InternetCrackUrlW` e o corpo opcional de `HttpSendRequestW` seguem o mesmo
contrato. `GetAdaptersInfo`, `GetAdaptersAddresses` e `if_nametoindex` em
`IPHLPAPI.DLL` também copiam strings, estruturas encadeadas e tamanhos por essa
fronteira; os ponteiros internos dos registros são calculados no host e só são
publicados junto com o bloco completo. A migração do runtime ainda não está completa:
`validate_mapped_range` continua sendo uma fotografia de `/proc/self/maps`, e
há APIs antigas com acesso direto após validação; essas rotas não são
anunciadas como atômicas até serem migradas.

As APIs `GetPrivateProfileStringA/W` e `GetPrivateProfileSectionA/W` copiam
strings de entrada para objetos host e publicam resultados ANSI/UTF-16 por
`write_guest_memory`, preservando a truncagem e a terminação dos buffers. A
leitura de arquivos INI continua limitada ao parser simples existente.

As saídas de `GetExitCodeThread`, `GetThreadTimes`, `CreateThread`,
`InitializeSListHead` e `InitializeProcThreadAttributeList` também passam pela
fronteira protegida; `InterlockedPushEntrySList` copia os ponteiros encadeados
antes de publicá-los. A implementação SLIST continua sendo a emulação mínima
de contexto convidado documentada no módulo.

Os stubs comuns de WTS, impressão, SetupAPI, DirectX/DXGI, TDH e memória de
processo também zeram saídas por cópia protegida e retornam
`ERROR_INVALID_PARAMETER` quando o destino não é acessível. A enumeração WTS
continua limitada à sessão local sintética, e as demais operações permanecem
explicitamente não suportadas.

No `WS2_32.dll`, `WSAStartup`, conversões de endereço, `WSAAddressToStringA`,
`gethostname` e `getaddrinfo` copiam entradas e saídas entre memória host e
convidada. `send`/`recv`, `sendto`/`recvfrom`, `getsockname`/`getpeername`,
`setsockopt`/`getsockopt`, `ioctlsocket`, `WSAIoctl` e `getnameinfo` também
usam buffers temporários do host e cópias protegidas; uma falha de acesso
retorna erro Winsock controlado, sem entregar o ponteiro convidado ao syscall
Linux. `WSAPoll`, `select`, `WSAWaitForMultipleEvents` e
`WSAEnumNetworkEvents` também copiam suas listas/estruturas para o host e
publicam os resultados por transferência protegida. Endereços retornados por
`getaddrinfo` continuam sendo registros estáticos do runtime, e a
implementação suporta apenas o subconjunto IPv4 documentado. A regressão dos
buffers de payload/opções pode ser pulada quando o sandbox não permite criar
sockets UDP locais.

## GUI mínima (Fase 7)

O protótipo registra um subconjunto de `USER32.dll` e `GDI32.dll` e usa X11
diretamente. Ele é experimental, não altera o subsistema de console e só aceita
`type == 0` em `MessageBoxA`.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `USER32.dll` | `MessageBoxA` | Suportado | Caixa modal com `hWnd == NULL` e `uType == 0`; OK retorna `1`, fechar retorna `0` |
| `USER32.dll` | `RegisterClassExA` | Suportado | Classe única por nome (case-insensitive); retorna atom `>= 1` |
| `USER32.dll` | `CreateWindowExA` | Suportado no subconjunto | Cria janela X11 a partir da classe registrada e despacha `WM_CREATE` ao `WNDPROC`; classes próprias usadas como filhos ficam em uma side-table, não viram janelas X11 individuais e participam do hit-test de mouse |
| `USER32.dll` | `ShowWindow` | Suportado | Mostra/esconde a janela X11 |
| `USER32.dll` | `UpdateWindow` | Suportado | Despacha `WM_PAINT` diretamente ao `WNDPROC` |
| `USER32.dll` | `InvalidateRect` | Suportado no subconjunto | Valida o `RECT` opcional, enfileira um `WM_PAINT` por janela até a entrega e faz flush da superfície X11 projetada para filhos lógicos |
| `USER32.dll` | `GetMessageA` | Suportado no subconjunto | Traduz eventos X11 para `WM_PAINT`/`WM_LBUTTONDOWN`/`WM_LBUTTONUP`/`WM_RBUTTONDOWN`/`WM_RBUTTONUP`/`WM_KEYDOWN`/`WM_KEYUP`/`WM_CLOSE`; o hit-test entrega mouse a filhos lógicos customizados com `HWND` e coordenadas locais, enquanto controles comuns sem `WNDPROC` preservam suas notificações no parent; um botão secundário só vira callback de bandeja quando a janela registrou `Shell_NotifyIconA/W`; entrega mensagens pendentes antes dos eventos X11, despacha `WM_TIMER` expirados e retorna `0` com `WM_QUIT` após `PostQuitMessage` |
| `USER32.dll` | `PostMessageA` / `PostMessageW` | Suportado no subconjunto | Thread principal enfileira diretamente; threads convidadas secundárias podem postar para um `HWND` registrado, e a mensagem é entregue pela fila do thread principal; a fila cross-thread é limitada a 4096 mensagens e não copia payload apontado por `lParam` |
| `USER32.dll` | `TranslateMessage` | Suportado | Converte o `WM_KEYDOWN` mais recente em `WM_CHAR` com o caractere real (sem `WM_CHAR` para teclas sem caractere) |
| `USER32.dll` | `SetTimer` | Suportado | Timer periódico por janela → `WM_TIMER`; só `lpTimerFunc == NULL` |
| `USER32.dll` | `KillTimer` | Suportado | Remove um timer ativo |
| `USER32.dll` | `DispatchMessageA` | Suportado | Invoca o `WNDPROC` do convidado (`TL_MSABI`, host→convidado) |
| `USER32.dll` | `DefWindowProcA` | Suportado | `WM_CLOSE` → `DestroyWindow`; demais retornam `0` |
| `USER32.dll` | `RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` etc. | Suportado | Wrappers para `A` via `wide_to_utf8`/`utf8_to_wide`; `RegisterClassExW` converte `WNDCLASSEXW` (80 bytes), `CreateWindowExW` converte classe/título, `SetWindowTextW`/`GetWindowTextW`/`GetWindowTextLengthW` convertem, `FindWindowW`/`SendMessageW`/`AppendMenuW` delegam |
| `USER32.dll` | `GetClassInfoW` | Suportado no subconjunto | Consulta a tabela de classes registrada, preenche `WNDCLASSW` quando há saída válida e retorna `ERROR_CLASS_DOES_NOT_EXIST` (`141`) para classe ausente |
| `USER32.dll` | `LoadMenuW`, `GetMenu`, `SetMenu`, `GetSubMenu`, `GetMenuItemCount`, `GetMenuItemInfoW`, `InsertMenuItemW` | Suportado no subconjunto | `LoadMenuW` lê recursos `RT_MENU` MENUEX v1 do módulo convidado, preserva itens/IDs/textos/submenus e associa o menu de classe à janela principal; `GetMenuItemInfoW` valida e preenche o buffer; `InsertMenuItemW` aceita `MENUITEMINFOW` x64 com estado, tipo, ID, submenu e texto, por posição ou ID. Quando a extensão host-side `7zip` está selecionada por perfil schema 4, dropdowns aninhados, seleção por mouse/teclado e itens folha encaminham `WM_COMMAND`; templates v0, bitmaps e as demais mutações continuam fora. Execução direta sem perfil permanece genérica. |
| `USER32.dll` | `EnableMenuItem`, `CheckMenuItem`, `CheckMenuRadioItem` | Suportado no subconjunto | Atualiza `MenuItem::state` para menus criados/carregados pelo runtime, aceita seleção por comando ou posição e retorna o estado anterior conforme o contrato básico; `CheckMenuRadioItem` marca um item dentro de uma faixa e desmarca os demais. Inserção/remoção e bitmaps continuam fora |
| `USER32.dll` | `DestroyWindow` | Suportado | Destrói a janela e despacha `WM_DESTROY` |
| `USER32.dll` | `PostQuitMessage` | Suportado | Sinaliza `WM_QUIT`; `GetMessageA` retorna `0` |
| `USER32.dll` | `GetDC` / `ReleaseDC` | Suportado | `HDC == HWND` (token opaco da janela); validam o par `hwnd`/`dc`; controles lógicos projetam o desenho na superfície X11 do pai com offsets acumulados |
| `USER32.dll` | `BeginPaint` / `EndPaint` | Suportado | Preenchem o `PAINTSTRUCT` (layout Microsoft x64, 72 bytes) com o tamanho da janela/controle e marcam/desmarcam o estado de pintura; o `HDC` usa a superfície X11 da janela principal |
| `USER32.dll` | `SendMessageA` / `SendMessageW` (controles comuns) | Suportado no subconjunto | Toolbar: `TB_BUTTONSTRUCTSIZE`, `TB_ADDBUTTONSA/W`, `TB_BUTTONCOUNT`, `TB_DELETEBUTTON`, `TB_SETBUTTONSIZE`, `TB_SETBITMAPSIZE`, `TB_AUTOSIZE`, `TB_SETIMAGELIST`, `TB_ENABLEBUTTON`; status bar: `SB_SETTEXTA/W`, `SB_SETPARTS`, `SB_SETMINHEIGHT`, `SB_SIMPLE` |
| `GDI32.dll` | `GetStockObject` | Suportado | Token opaco por stock object (tabela estática, `object` em `0..23`); stock objects não são liberados |
| `GDI32.dll` | `TextOutA` / `TextOut` | Suportado | Desenha texto ANSI com comprimento explícito via `XDrawString`; o texto convidado é copiado antes do desenho e, em controles lógicos, soma a posição dos pais ao destino |
| `GDI32.dll` | `FillRect`, `GetObjectA/W`, `CreateDIBSection`, `GetTextExtentPoint32W`, `GetTextMetricsA/W`, `GetClipBox` | Suportado no subconjunto | Estruturas e strings de entrada são lidas em snapshots host e as estruturas de saída são publicadas por cópia protegida; DIB continua limitado a 256 MiB e métricas permanecem estáticas |
| `GDI32.dll` | `GetCharWidthA/W`, `GetCharABCWidthsA`, `GetCharABCWidthsFloatA`, `GetTextExtentPointA/W`, `GetTextExtentExPointA/W`, `TranslateCharsetInfo`, `Set/OffsetWindowOrgEx`, `SetBrushOrgEx`, `GetDeviceGammaRamp` | Suportado no subconjunto | Arrays, pontos, métricas, strings contadas e rampas são montados em memória host e publicados por cópia protegida; larguras, charset, origens e gamma permanecem valores estáticos |

### Diálogos e controles (Fase 13.11)

| Módulo | APIs | Estado | Limite publicado |
|---|---|---|---|
| `USER32.dll` | `DialogBoxParamA` / `DialogBoxParamW`, `EndDialog`, `GetDlgItem`, `SetDlgItemTextW`, `SendDlgItemMessageW`, `GetNextDlgTabItem`, `IsDialogMessageW` | Suportado no subconjunto | Somente template numérico `RT_DIALOG` padrão do módulo atual; o caminho ANSI delega ao modal wide após validar `MAKEINTRESOURCE`; modal único; controles lógicos `BUTTON`/`EDIT`/`STATIC`/`COMBOBOX`; alterações em controles de diálogos invalidam o pai e entregam um `WM_PAINT` de baixa prioridade após as filas de mensagens, evitando pintura síncrona durante callbacks cross-thread; classes customizadas recebem ciclo básico, mas continuam sem renderer visual genérico; a classe `7-Zip::FM` só recebe shell visual específico quando a extensão `7zip` está selecionada por perfil schema 4. |
| `USER32.dll` | `CreateDialogParamA` / `CreateDialogParamW` | Suportado no subconjunto | Template numérico `RT_DIALOG`, inclusive classe textual customizada e diálogo sem controles; cria uma janela modeless lógica com controles padrão/genéricos, despacha `WM_INITDIALOG` e mantém o `HWND` válido durante `WM_DESTROY`; o smoke D2 usa `CreateDialogParamA` para a abertura/fechamento de `PuTTY Configuration`, e `tl_dialog` protege o caminho wide com `DestroyWindow` |
| `USER32.dll` | `GetWindowRect`, `GetWindowLongW`, `SetWindowLongW` | Suportado no subconjunto | Geometria side-table e wrappers limitados de 32 bits sobre `*Ptr` |
| `USER32.dll` | `CopyImage`, `DestroyIcon` | Suportado no subconjunto | Tokens de ícone copiados; não há `LoadImageW` nem desenho de ícones |
| `COMCTL32.dll` | `InitCommonControlsEx` | Suportado no layout de 8 bytes | Valida `cbSize`/classes; ordinais 410/413 continuam `unknown-ordinal` |
| `COMCTL32.dll` | `CreateStatusWindowW` | Suportado no subconjunto | Parent válido; cria uma `msctls_statusbar32` lógica no rodapé, com texto UTF-16 convertido para UTF-8 e desenho na superfície X11 principal |
| `COMCTL32.dll` | `CreateToolbarEx` | Suportado no subconjunto | Parent válido; valida até 128 entradas do vetor `TBBUTTON`, preserva `idCommand`, desenha botões lógicos, mostra o pressionamento, cancela soltura fora do botão e encaminha somente o clique capturado por `WM_COMMAND`; mensagens `TB_ADDBUTTONSA/W` e `TB_AUTOSIZE` também atualizam esse modelo; bitmaps, image lists e estilos avançados permanecem fora |
| `COMDLG32.dll` | `GetOpenFileNameA/W` / `GetSaveFileNameA/W` / `ChooseColorA/W` / `ChooseFontA/W` / `PrintDlgW` / `CommDlgExtendedError` | Não suportado controlado | Não abre diálogos nem fabrica seleção; as funções de diálogo retornam `FALSE`, preservam as estruturas e definem `ERROR_NOT_SUPPORTED`/`CDERR_DIALOGFAILURE` quando o ponteiro é válido; ponteiro nulo retorna `ERROR_INVALID_PARAMETER`/`CDERR_STRUCTSIZE` |

`tl_dialog.exe` valida o ciclo mínimo sob Xvfb quando o ambiente fornece o
socket X11. O smoke confirma Tab/Enter, `WM_COMMAND`, retorno 42, saída
`dialog\n`, destruição modal e trace; sem X11, o teste é explicitamente
`Skipped`.

`tl_gui.exe` é validado automaticamente quanto a formato e imports; a janela
deve ser validada manualmente numa sessão X11. `tl_win.exe`, `tl_win2.exe`,
`tl_key.exe`, `tl_timer.exe`, `tl_gdi.exe`, `tl_paint.exe` e `tl_dialog.exe`
são executados de ponta a ponta sob `Xvfb` (sempre um servidor próprio, sem
window manager) pelo teste `runtime_gui_smoke`, que cobre o message loop
(autoclose), o fechamento real por `WM_DELETE_WINDOW`, a entrada de teclado
(`KeyPress` sintético → `WM_KEYDOWN`/`WM_CHAR`), a demultiplexação entre duas
janelas simultâneas, o teclado estendido (`KeyPress`+`KeyRelease`, `Shift`,
teclas sem caractere → `WM_KEYDOWN`/`WM_KEYUP`), os timers (`SetTimer` →
`WM_TIMER` → `KillTimer`), a pintura mínima (`BeginPaint`/`TextOut`/`EndPaint`)
e o diálogo modal (`Tab`/`Enter`/`WM_COMMAND`). O `x11_popup_smoke` cobre
Escape, clique externo, destruição externa e timeout. O driver valida também
os traces de contrato do message loop: `GetMessageA ... result="quit"`,
`ExitProcess ... mechanism="guest-transfer"`, `TranslateMessage ...
status="translated"`, `SetTimer`/`GetMessageA(WM_TIMER)`/`KillTimer`,
`BeginPaint`, `TextOut`, `Rectangle` e `FillRect` — ver
[`gui-x11.md`](arquitetura/gui-x11.md).

No preset `sanitize`, o CTest executa `x11_popup_smoke` com LeakSanitizer
habilitado: o cenário inclui 512 desenhos de cores e os quatro caminhos de
cleanup de popup, sem relatório de ASan/LSan na validação sob Xvfb.

## Simple Todo C (Fase 12)

O alvo `Efeckc17/simple-todo-c` é baixado por archive pinado e hash SHA-256 em
`tests/targets/CMakeLists.txt`. O recurso `app.rc` é gerado no diretório de
build com o manifesto e o ícone upstream; `tests/targets/manifests/simple_todo.json`
fixa a lista de 105 imports. O build aplica os overlays
`tests/targets/patches/simple_todo_linux.patch` e
`simple_todo_linux_autorun.patch` e `simple_todo_linux_close.patch`: a tela
ganha layout Linux, a opção de autorun no Windows é removida e o fechamento da
janela destrói o processo. O teste `targetapp_simple_todo_gui_smoke` usa
um Xvfb próprio, um `TL_PREFIX` temporário e verifica o fluxo de adicionar,
editar, buscar, concluir, excluir, esconder, mostrar e sair pelo menu emulado;
o arquivo persistente é conferido em
`C:\users\guest\AppData\Roaming\TodoApp\todos.dat` dentro do prefixo.

| Módulo | APIs adicionais | Estado | Limite publicado |
|---|---|---|---|
| `USER32.dll` | `RegisterClassA`, controles lógicos via `CreateWindowExA`, `SendMessageA`, `Get/SetWindowTextA`, foco, tabulação, geometria, `WM_COMMAND` e `WM_NOTIFY` | Implementado para o alvo | Não são janelas X11 filhas; EDIT, BUTTON, COMBOBOX, STATIC, SysListView32 e SysTreeView32 são roteados por uma side-table; TreeView cobre o modelo de itens, seleção, expansão e notificações básicas, enquanto o renderer visual permanece limitado |
| `GDI32.dll` | `CreateFontA`, `CreateSolidBrush`, `DeleteObject`, `SetBkColor`, `SetTextColor` | Implementado para o alvo | Tokens de fonte/brush e cores têm efeito limitado; o desenho usa o GC X11 mínimo |
| `SHELL32.dll` | `Shell_NotifyIconA` / `Shell_NotifyIconW` | Limitado | Registra por janela o prefixo x64 de `NOTIFYICONDATA` para `NIM_ADD`, `NIM_MODIFY` e `NIM_DELETE`; o callback de bandeja é emulado por X11 e não há integração com o tray do desktop |
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
| `msvcrt.dll` | `__initterm`, `__set_app_type`, `__setusermatherr`, `_cexit`, `_lock`, `_unlock`, `__C_specific_handler` | Suportado | `__initterm` executa a lista de callbacks (TLS/CTOR); os demais são no-ops. `__C_specific_handler` segue a ABI Microsoft de quatro argumentos e interpreta somente `SCOPE_TABLE_AMD64` de `__try/__except`. |
| `msvcrt.dll` | `_amsg_exit`, `abort`, `exit`, `atexit`, `_onexit` | Suportado | Terminam via `ExitProcess`; `atexit` e `_onexit` acumulam handlers executados no encerramento |
| `msvcrt.dll` | `_errno`, `getenv`, `strerror` | Suportado | Célula `errno` por thread; `getenv` lê o ambiente Win32 isolado do processo convidado |
| `msvcrt.dll` | `fopen`/`fclose`/`fflush`/`ferror`/`fseek`/`ftell`/`rewind`/`fgetc`/`fputc`/`fputs`/`fprintf`/`vfprintf`/`fwrite` | Suportado | I/O em `GuestFile` (layout `_iobuf` de 48 bytes), unbuffered via `::write` com loop `EINTR` |
| `msvcrt.dll` | `_open`/`_fdopen`/`_fileno`/`_isatty`/`_setmode`/`__iob_func` | Suportado | Tradução de flags `_O_*`; modo por fd (`_O_TEXT`/`_O_BINARY`) refletido na flag `_IOSTRG` do `GuestFile` |
| `msvcrt.dll` | `malloc`/`calloc`/`free`, `memcpy`/`memset` | Suportado | Alocação e memória diretas do hospedeiro |
| `msvcrt.dll` | `strlen`/`strcmp`/`strncmp`/`strcpy`/`strncpy`/`strstr`/`strcat`/`strtol`/`strtoul`/`wcslen`/`isalnum`/`isspace`/`toupper` | Suportado | Semântica libc para ASCII/latin-1 |
| `msvcrt.dll` | `fgetc`/`fread`/`ungetc` | Suportado | `fgetc` lê byte e verifica `charbuf` (pushback); `fread` lê `count` elementos de `size` bytes; `ungetc` devolve caractere ao stream via `charbuf` do `GuestFile` |
| `msvcrt.dll` | `memmove`/`remove`/`_stat64` | Suportado | `memmove` com tratamento de overlap; `remove` delega ao host; `_stat64` preenche o `struct _stat64` do MinGW (pack 8, `st_mode` em `0x06`, tamanho 56 bytes) a partir do `stat()` do host |
| `msvcrt.dll` | `localeconv`, `___lc_codepage_func`, `___mb_cur_max_func` | Suportado | Locale C fixo: `lconv` estático, code page `1252`, `mb_cur_max == 1` |
| `msvcrt.dll` | `signal` | Suportado | Registra handlers em tabela por sinal; nenhuma entrega real ao convidado |
| `KERNEL32.dll` | `VirtualQuery` | Suportado no subconjunto | Preenche `MEMORY_BASIC_INFORMATION` (48 bytes); reservas e commits próprios usam regiões rastreadas pelo runtime, inclusive `AllocationBase`, `AllocationProtect`, `State`, `Protect` e divisão após mudança parcial; os demais mapeamentos usam `/proc/self/maps` |
| `KERNEL32.dll` | `VirtualProtect` | Suportado no subconjunto | `mprotect` sobre a página alinhada dentro de uma alocação commitada; escreve a proteção antiga em `*lpflOldProtect`, atualiza a tabela de regiões e rejeita reserva ou faixa que não contenha `[address, address+size)` |
| `KERNEL32.dll` | `MultiByteToWideChar` / `WideCharToMultiByte` | Suportado | CP `0` (ACP → 1252), `1252`, OEM/`437` e `65001` (UTF-8), incluindo tabela CP437 completa; conversões manuais sem locale, entradas/saídas por cópia protegida e `ERROR_INSUFFICIENT_BUFFER` (122) |
| `KERNEL32.dll` | `Initialize/Enter/Leave/DeleteCriticalSection` | Suportado | No-ops com validação de ponteiro (convidado single-thread → exclusão trivial) |
| `KERNEL32.dll` | `InitializeCriticalSectionAndSpinCount` / `InitializeCriticalSectionEx` | Suportado no subconjunto | Reutilizam a tabela de seções críticas; spin count é ignorado e `InitializeCriticalSectionEx` aceita somente `CRITICAL_SECTION_NO_DEBUG_INFO` ou flags zero |
| `KERNEL32.dll` | `AreFileApisANSI` | Suportado no subconjunto | Retorna `TRUE` para o ACP determinístico `1252` |
| `KERNEL32.dll` | `FormatMessageA` / `FormatMessageW` | Suportado no subconjunto | Mensagens de sistema fixas, buffer fornecido ou `FORMAT_MESSAGE_ALLOCATE_BUFFER`; sem inserts, tabelas externas ou recursos de mensagem |
| `KERNEL32.dll` | `GlobalAlloc` / `GlobalLock` / `GlobalUnlock` / `GlobalFree`, `LocalAlloc` / `LocalFree` | Suportado no subconjunto | Blocos `malloc`/`calloc` rastreados por handle, flags `GMEM_MOVEABLE`/`ZEROINIT`, contagem de locks e rejeição de ponteiros arbitrários; `GlobalAlloc` e `LocalAlloc` usam o mesmo envelope de memória do processo |
| `KERNEL32.dll` | `TlsGetValue` | Suportado | Retorna o valor do slot TLS da thread convidada e `ERROR_SUCCESS` para slot não usado; TLS estático e callback possuem contrato separado na seção de concorrência |
| `KERNEL32.dll` | `GetConsoleMode` / `SetConsoleMode` | Suportado | `GetConsoleMode` devolve `0x3` e `TRUE` só para fd com `isatty`; caso contrário `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `IsDBCSLeadByteEx` | Suportado | Sempre `FALSE` (sem DBCS) |
| `KERNEL32.dll` | `Sleep` | Suportado | `nanosleep` com loop `EINTR` |
| `KERNEL32.dll` | `SetUnhandledExceptionFilter` / `UnhandledExceptionFilter` | Suportado no SEH explícito | Registra/retorna o filtro anterior; o filtro é chamado somente quando VEH e busca por frame não resolvem `RaiseException`. |
| `KERNEL32.dll` | `AddVectoredExceptionHandler` / `RemoveVectoredExceptionHandler` | Suportado no SEH explícito | Tokens opacos; prioridade `first` e remoção apenas do token correspondente. |
| `KERNEL32.dll` | `RaiseException` / `RtlUnwind` / `RtlUnwindEx` | Suportado no SEH explícito | Captura contexto, busca `.pdata/.xdata` V1/V2 fora de epílogo, chama `EHANDLER`/`UHANDLER` estáticos e transfere sem retorno ao contexto convidado selecionado; leituras e escritas de unwind ficam limitadas à stack ativa entre `TEB.StackLimit` e `TEB.StackBase`; `RtlUnwindEx` valida a consolidação `0x80000029` e o RIP retornado pelo callback. |
| `KERNEL32.dll` | `GetModuleHandleA/W` | Suportado | Retorna handle `0x1000` para módulos registrados (inclui `api-ms-win-*`/`KERNELBASE` via forwarders, extração de filename de caminhos `C:\...`), `NULL` + `ERROR_FILE_NOT_FOUND` caso contrário; `W` converte via `wide_to_utf8` |
| `KERNEL32.dll` | `GetModuleHandleExA/W` | Suportado | Flags `PIN`/`UNCHANGED_REFCOUNT`/`FROM_ADDRESS`; `FROM_ADDRESS` aceita `0x1000` ou endereço dentro da imagem (`g_guest_image_base/size`); valida `phModule` via `mapped_guest_range`; erro `ERROR_INVALID_PARAMETER`/`FILE_NOT_FOUND` |
| `KERNEL32.dll` | `LoadLibraryA/W` / `LoadLibraryExA/W` | Suportado no subconjunto | Em execuções nativas não-Proton, usa o grafo por execução e a precedência perfil → diretório da aplicação → `drive_c` → genérico; no caminho sem grafo usa módulos internos; normaliza filename case-insensitive e adiciona `.dll`; retorna `ERROR_MOD_NOT_FOUND` (126) quando ausente; `Ex` ignora `hFile`/`flags` |
| `KERNEL32.dll` | `FreeLibrary` | Suportado | Aceita `0x1000` ou base do exe; `NULL`/inválido → `0` + `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `GetProcAddress` | Suportado no subconjunto | Em execuções nativas não-Proton, busca no módulo identificado pelo handle e suporta ordinal via `MAKEINTRESOURCE`; aplica fallback por export do grafo. No caminho sem grafo, usa a busca global dos módulos internos; valida `proc_name` e `module`; `ERROR_PROC_NOT_FOUND` (127) ou `ERROR_INVALID_HANDLE` |
| `KERNEL32.dll` | `GetVersionExA/W` | Suportado | Reporta Windows 10 (10.0.19044, `VER_PLATFORM_WIN32_NT=2`, `szCSDVersion` zero, `wServicePackMajor/Minor=0`, `wSuiteMask=0`, `wProductType=1`); lê e publica `lpVersionInformation` por cópia protegida, com tamanhos A:148/156 e W:276/284 (até 284); `ERROR_INVALID_PARAMETER` em ponteiro/size inválido |
| `KERNEL32.dll` | `VerifyVersionInfoW` / `VerSetConditionMask` | Suportado | `VerifyVersionInfoW` lê `lpVersionInfo` por cópia protegida, valida `dwTypeMask` e retorna `TRUE` (versão sempre compatível); `VerSetConditionMask` codifica 3 bits por `TypeMask` (`&0x07`, `shift=i*3`) como no Wine |
| `KERNEL32.dll` | `GetUserDefaultLocaleName` | Suportado | Retorna `en-US` (wide, `0x0409`) por cópia protegida; `NULL/0` → `6` (inclui NUL); buffer <6 → `0` + `ERROR_INSUFFICIENT_BUFFER` (122) |
| `KERNEL32.dll` | `LocaleNameToLCID` | Suportado | Copia o nome wide do convidado antes de converter; `en-US`→`0x0409`, `pt-BR`→`0x0416`, `en`→`0x09`, `pt`→`0x16` (case-insensitive); `NULL`/vazio/desconhecido → `0` + `ERROR_INVALID_PARAMETER` |
| `version.dll` | `GetFileVersionInfoSizeA/W` / `GetFileVersionInfoA/W` / `GetFileVersionInfoSizeExA/W` / `GetFileVersionInfoExA/W` / `VerQueryValueA/W` | Não suportado controlado | As dez exports são `ExportSupport::Stub`: não inventam tamanho, bloco `RT_VERSION` ou ponteiro host; consultas válidas retornam falha com `ERROR_NOT_SUPPORTED`, zeram `VerQueryValue`/`len` e ponteiros inválidos retornam `ERROR_INVALID_PARAMETER` |
| `IMM32.dll` | `ImmGetContext` / `ImmReleaseContext` / `ImmSetCompositionWindow` / `ImmGetCompositionStringA/W` / `ImmAssociateContext` / `ImmGetVirtualKey` / `ImmSetCompositionFontA/W` / `ImmSetCandidateWindow` / `ImmSetCompositionStringW` / `ImmEscapeW` / `ImmNotifyIME` | Não suportado controlado | As treze exports são `ExportSupport::Stub`: não criam contexto host nem processam composição; APIs booleanas retornam `FALSE`, consultas de composição retornam `-1`, `ImmGetVirtualKey` retorna `0` com `ERROR_NOT_SUPPORTED` e ponteiros nulos retornam `ERROR_INVALID_PARAMETER` |
| `DWMAPI.dll` | `DwmSetWindowAttribute` / `DwmGetWindowAttribute` / `DwmIsCompositionEnabled` / `DwmExtendFrameIntoClientArea` / `DwmEnableBlurBehindWindow` / `DwmFlush` / `DwmGetColorizationColor` | Não suportado controlado | Não cria composição Windows nem inventa estado visual: exports são `ExportSupport::Stub`, retornam `E_NOTIMPL`, limpam saídas válidas e definem `ERROR_NOT_SUPPORTED`; `DwmDefWindowProc` permanece `Limited` e retorna `FALSE`/não tratado |
| `KERNEL32.dll` | `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll` | Suportado | `WaitOnAddress` compara `*Address` vs `*CompareAddress` (`size` 1/2/4/8, alinhado, `mapped_guest_range`); se diferente retorna `1`; se igual espera por `Wake*` ou `dwMilliseconds` (`INFINITE`→`wait`, `0`→timeout imediato) via `mutex`+`cv`+`version` por endereço; `WakeSingle`→`notify_one`, `WakeAll`→`notify_all`; timeout → `0` + `ERROR_TIMEOUT` (1460); exposto via `KERNEL32` e `api-ms-win-core-synch-l1-2-0.dll` (forwarder) |
| `KERNEL32.dll` | `GetCommandLineA/W` | Suportado | Retorna linha de comando formatada com aspas a partir do `argv` do convidado |
| `KERNEL32.dll` | `GetEnvironmentVariableA/W`, `SetEnvironmentVariableW`, `Get/FreeEnvironmentStringsW`, `ExpandEnvironmentStringsW` | Suportado | Mapa por processo, case-insensitive, copiado do host e sobreposto pelo prefixo sem mutar o Linux; bloco UTF-16 ordenado/rastreado, expansão `%NOME%`, consultas de tamanho e `ERROR_INSUFFICIENT_BUFFER` |
| `KERNEL32.dll` | `FlsAlloc`, `FlsFree`, `FlsGetValue`, `FlsSetValue` | Suportado no subconjunto por thread | Índices/callbacks por processo, valores por thread; callback MS x64 validado na imagem, uma vez no fim da thread ou em `FlsFree`; fibras reais continuam fora do escopo |
| `KERNEL32.dll` | `GetACP`, `GetOEMCP`, `GetCPInfo`, `IsValidCodePage`, `IsValidLocale`, `GetLocaleInfoW/Ex`, `EnumSystemLocalesW`, `GetStringTypeW`, `GetDateFormatW`, `GetTimeFormatW`, `LCMapStringW/Ex` | Suportado no subconjunto determinístico | Locale único `en-US`/`0x0409`, ACP 1252 e OEMCP 437; enumeração de um callback, `CT_CTYPE1`, formatos estáticos de data/hora e case mapping ASCII/Latin-1; snapshots de entradas e cópias protegidas das saídas; sort keys, CJK, formatos customizados e locale do host não entram |
| `KERNEL32.dll` | `GetStartupInfoW`, `GetSystemDirectoryA/W`, `GetWindowsDirectoryA/W`, `GetFileType`, `SetStdHandle`, `ReadConsoleW`, `WriteConsoleW`, `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `EncodePointer`, `DecodePointer`, `InitializeSListHead` | Suportado no subconjunto de processo/console | Estado padrão por processo e compartilhado por threads; `STARTUPINFOW` 104 bytes, diretórios Windows por publicação protegida, console UTF-16↔UTF-8, recursos AMD64 fixos, cookie reversível e SList vazia alinhada; sem alocação de console, herança explícita ou operações interlocked de lista |
| `KERNEL32.dll` | `FindFirstFileExW`, `SetFileAttributesW`, `SetFileInformationByHandle` | Suportado no subconjunto de metadados | Enumeração W por `FindExInfoStandard/Basic`, `*`/`?` ASCII case-insensitive e `LARGE_FETCH` como hint; atributos `READONLY`/`NORMAL`/`ARCHIVE`/`DIRECTORY`; classes `FileBasicInfo`, `FileDispositionInfo` e `FileDispositionInfoEx` validadas no prefixo |
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

As APIs de locale e conversão também rejeitam ponteiros convidados não
acessíveis sem tocar diretamente nesses endereços: a regressão
`Win32LocaleTest.ProtectedConversionAndFormattingBuffersRejectUnmappedPointers`
cobre conversões de code page, `GetCPInfo`, informações/classificação de locale,
`FoldStringW`, `GetNumberFormatW`, diretórios e formatação de data/hora.

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
`translate_windows_path()`; `CreateFileA` cobre caminhos relativos e
`C:\\...` dentro do prefixo ativo, mas rejeita caminhos absolutos Linux,
enquanto o CRT aceita esses caminhos para os aplicativos que recebem arquivos
do host como argumentos.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `GetFileSize` | Suportado | Retorna tamanho do arquivo aberto via `FileSlot.file_size`; suporta ponteiro `high_size` para arquivos > 4 GiB |
| `KERNEL32.dll` | `SetFilePointer` | Suportado | Seek por `FILE_BEGIN`/`FILE_CURRENT`/`FILE_END`; suporta ponteiro `high_distance`; atualiza `FileSlot.position` |
| `KERNEL32.dll` | `GetFileAttributesA` | Suportado | `stat()` + bits `FILE_ATTRIBUTE_DIRECTORY`/`FILE_ATTRIBUTE_ARCHIVE`/`FILE_ATTRIBUTE_READONLY` |
| `KERNEL32.dll` | `DeleteFileA` | Suportado | `unlink()` com mapeamento de erros |
| `KERNEL32.dll` | `MoveFileA` / `MoveFileExA` | Suportado no subconjunto | `rename()` com mapeamento de erros; `MoveFileExA` aceita `MOVEFILE_REPLACE_EXISTING` e rejeita flags não implementadas |
| `KERNEL32.dll` | `CreateDirectoryA` | Suportado | `mkdir()` com permissão 0777 |
| `KERNEL32.dll` | `FindFirstFileA` | Suportado | Abre `opendir()` + `readdir()` e preenche atributos, tamanho e tempos |
| `KERNEL32.dll` | `FindNextFileA` | Suportado | Continua iteração com o mesmo padrão |
| `KERNEL32.dll` | `FindFirstFileW` | Suportado | Converte UTF-16 para UTF-8 e compartilha a enumeração com a variante A |
| `KERNEL32.dll` | `FindFirstFileExW` | Suportado no subconjunto | Aceita `FindExInfoStandard`/`Basic`, `FindExSearchNameMatch`, filtro nulo e `FIND_FIRST_EX_LARGE_FETCH` como hint; usa os mesmos handles de `FindNextFileW`/`FindClose` |
| `KERNEL32.dll` | `FindNextFileW` | Suportado | Continua enumeração wide e converte o nome encontrado para UTF-16 |
| `KERNEL32.dll` | `FindClose` | Suportado | Fecha `DIR*` e libera slot |
| `KERNEL32.dll` | `GetFileAttributesW` | Suportado | Converte o caminho UTF-16 e delega ao mesmo `stat()` da variante A |
| `KERNEL32.dll` | `SetFileAttributesW` | Suportado no subconjunto | `READONLY` altera bits de escrita Linux; `NORMAL`, `ARCHIVE` e `DIRECTORY` são validados contra o tipo; `NOT_CONTENT_INDEXED` é aceito como atributo consultivo sem serviço de indexação; demais atributos sem representação retornam `ERROR_INVALID_PARAMETER` |
| `KERNEL32.dll` | `SetFileInformationByHandle` | Suportado no subconjunto | `FileBasicInfo` aplica tempos de acesso/escrita e atributos; `FileDispositionInfo` marca exclusão no fechamento; `FileDispositionInfoEx` cobre `DELETE`, `POSIX_SEMANTICS`, `ON_CLOSE` e `IGNORE_READONLY_ATTRIBUTE` |
| `KERNEL32.dll` | `GetCurrentDirectoryA/W` | Suportado | Retorna o diretório de execução convertido para caminho Windows lógico: `C:\\...` dentro do prefixo, `Z:\\...` para arquivo/diretório externo |
| `KERNEL32.dll` | `GetModuleFileNameA/W` | Suportado | Retorna o módulo definido via `set_guest_module_path()` como caminho Windows lógico: aplicação instalada no prefixo usa `C:\\...`; setup externo usa `Z:\\...` |
| `KERNEL32.dll` | `GetFullPathNameW` | Suportado | Normalização Windows completa (Wine `dlls/kernel32/path.c`): resolve relativo via `GetCurrentDirectory`, colapsa `.`/`..`, trata `C:`, `\` e `\\` (UNC); `file_part` aponta para após último `\`/`:` |
| `KERNEL32.dll` | `GetFullPathNameA` | Suportado | Conversão `A` → `W` com mesma normalização; buffer insuficiente retorna `tamanho+1` e `ERROR_INSUFFICIENT_BUFFER` |
| `KERNEL32.dll` | `lstrcatW` | Suportado no subconjunto | Concatena strings UTF-16 guest e retorna o destino; valida as strings e o intervalo gravável do resultado |
| `SHELL32.dll` | `CommandLineToArgvW` | Suportado | Divide a linha de comando UTF-16 em argumentos, preservando grupos entre aspas; o bloco único retornado é liberado por `LocalFree` |
| `SHELL32.dll` | `SHGetKnownFolderPath` | Suportado | Resolve `FOLDERID_RoamingAppData`, `LocalAppData`, `ProgramData`, `Desktop`, `Documents`, `Downloads` e `Profile` sob `C:\users\guest`/`C:\ProgramData` do prefixo ativo; nunca consulta `HOME`, XDG ou `/tmp` do host e devolve a string por `CoTaskMemAlloc`. |
| `SHELL32.dll` | `SHGetFolderPathW` | Suportado | Resolve `CSIDL_APPDATA`/`LOCAL_APPDATA`/`COMMON_APPDATA`/`DESKTOP`/`PERSONAL`/`PROFILE` sob o prefixo ativo e copia o caminho Windows para `pszPath[260]`. |
| `SHELL32.dll` | `SHGetFolderPathAndSubDirW` | Suportado | Cria a base `CSIDL` e o subdiretório relativo sob `drive_c`; rejeita caminho absoluto ou componente `..` e retorna o caminho Windows canônico. |
| `SHELL32.dll` | `ShellExecuteA/W` / `ShellExecuteExW` | Não suportado controlado | Valida strings/estrutura e retorna falha com `ERROR_NOT_SUPPORTED`; `SHELLEXECUTEINFOW` x64 tem 112 bytes, com `hInstApp` em 56 e `hProcess` em 104, ambos nulos na falha. Nenhum processo ou documento é aberto. |
| `GDI32.dll` | `CreateFontW` | Suportado | Wrapper `wide_to_utf8` → `CreateFontA`; valida `face_name` wide, token estático |
| `GDI32.dll` | `SetDCBrushColor` / `SetDCPenColor` | Suportado | Stub retorna `0`, `ERROR_SUCCESS` |
| `gdiplus.dll` | `GdiplusStartup` / `GdiplusShutdown` / `GdipAlloc` / `GdipFree` / `GdipCreateBitmapFromStream` / `GdipCloneImage` / `GdipDisposeImage` / `GdipCreateHBITMAPFromBitmap` | Não suportado controlado | As oito exports são `ExportSupport::Stub`: não inicializam GDI+, não alocam memória nem fabricam imagens/HBITMAPs; saídas válidas são zeradas e as operações retornam `GenericError` (`1`) com `ERROR_NOT_SUPPORTED` |
| `UxTheme.dll` | `SetWindowTheme` / `OpenThemeData` / `CloseThemeData` / desenho, consultas e buffered paint | Não suportado controlado | As exports são `ExportSupport::Stub`: não criam tema, brush, HDC ou buffered-paint; retornam `E_NOTIMPL`, limpam saídas válidas e definem `ERROR_NOT_SUPPORTED`; entradas inválidas retornam `E_INVALIDARG` |
| `WINMM.dll` | `timeSetEvent` | Suportado | Stub retorna `1` |
| `dbghelp.dll` | `SymFromAddr` | Não suportado controlado | Valida o buffer opcional `symbol`, zera `displacement` quando válido e retorna `FALSE` + `ERROR_NOT_SUPPORTED`; resolução de símbolos não é fabricada |
| `MPR.dll` | `WNetAddConnection2W` / `WNetOpenEnumW` / `WNetEnumResourceW` / `WNetCloseEnum` / `WNetGetResourceInformationW` / `WNetGetResourceParentW` | Não suportado controlado | Todos os seis exports têm classificação explícita `ExportSupport::Stub`, validam argumentos convidados, não criam conexões, credenciais ou handles; operações válidas retornam `ERROR_NOT_SUPPORTED` e handles não emitidos retornam `ERROR_INVALID_HANDLE` |
| `POWRPROF.dll` | `PowerGetActiveScheme` / `PowerSetActiveScheme` / `CallNtPowerInformation` | Suportado | `PowerGetActiveScheme` devolve GUID `Balanced` alocado por `LocalAlloc` e liberável por `LocalFree`; `PowerSetActiveScheme` `S_OK`, `CallNtPowerInformation` `memset` `0` |
| `IPHLPAPI.DLL` | `GetAdaptersInfo` / `GetAdaptersAddresses` / `if_nametoindex` | Suportado no subconjunto | `getifaddrs` do host, contratos de buffer `ERROR_BUFFER_OVERFLOW`/`ERROR_NO_DATA`, registros x64 com strings UTF-16 de largura fixa, interfaces IPv4 e `if_nametoindex` real; IPv6 e campos DNS continuam fora |

### Stubs com contrato explícito

`ExportSupport::Stub` identifica uma resolução deliberadamente limitada; não
significa que a API esteja implementada nem que o aplicativo tenha suporte de
fluxo principal. Os contratos abaixo são protegidos por
`Win32StubTest.*` e pelas suítes de cobertura dos aplicativos:

| Família | Contrato auditado |
|---|---|
| `KERNEL32.dll` / `WINSPOOL.DRV` / `WTSAPI32.dll` | Operações remotas, impressão e consulta detalhada de sessão retornam falha controlada, zeram saídas válidas e definem `ERROR_NOT_SUPPORTED`; os eventos incluem símbolo, mecanismo `stub` e detalhe. |
| `USER32.dll` | O estado básico de menus (`EnableMenuItem`, `CheckMenuItem`, `CheckMenuRadioItem`) e `InsertMenuItemW` são limitados ao modelo MENUEX v1 descrito acima; `SetMenuItemInfoW`, remoção, bitmaps e execução popup avançada continuam retornando falha controlada. |
| `SensApi.dll` | `IsDestinationReachableW` e `IsNetworkAlive` mantêm o contrato conservador usado pelos alvos atuais; flags válidas são preenchidas com `NETWORK_ALIVE_LAN`. |
| `SETUPAPI.dll` / `CFGMGR32.dll` | O enumerador usa handle sentinela, reporta coleção vazia com `ERROR_NO_MORE_FILES` e zera buffers/contadores de saída. |
| `D3D*.dll` / `DXGI.dll` / `DDRAW.dll` | Fábricas e compiladores não criam objetos: retornam HRESULT de falha/indisponibilidade e limpam ponteiros de saída válidos. Não há suporte DirectX, GPU ou jogos. |
| `WINMM.dll` | `PlaySoundA/W` e `timeSetEvent` mantêm os retornos de compatibilidade históricos; callbacks multimídia não são agendados e `timeKillEvent` não mantém estado de timer. |

### Limitações conhecidas

- `--cpu <segundos>` usa `RLIMIT_CPU` e mede tempo de CPU, não tempo de parede;
  `--memory <MiB>` usa `RLIMIT_AS` e limita o espaço de endereçamento virtual do
  processo. Ambos aceitam `0` como sem limite. Os limites são instalados no
  filho isolado, herdados por processos criados via `CreateProcessA/W` e não
  constituem sandbox.

- `WIN32_FIND_DATAW` tem layout de 592 bytes; enumeração preenche atributos,
  tamanho e tempos. A variante A segue o mesmo estado.
- Enumeração cobre `*`, `?`, `*.*` e correspondência exata case-insensitive
  em ASCII; classes de caracteres, locale de arquivos e case-fold Unicode amplo
  ficam fora.
- `FindFirstFileExW` rejeita níveis, operações, filtros e flags fora do
  subconjunto publicado. `SetFileAttributesW` aceita `NOT_CONTENT_INDEXED`
  sem persistir um bit POSIX equivalente, mas não representa `HIDDEN`,
  `SYSTEM`, `COMPRESSED`, ADS ou atributos de nuvem; `SetFileInformationByHandle`
  não cobre rename, EOF, allocation, links nem outras classes.
- `CommandLineToArgvW` cobre aspas e separação por espaço usadas pelos alvos;
  as regras completas de escape com barras invertidas antes de aspas ainda não
  fazem parte do subconjunto publicado.
- `CreateFileA` cobre caminhos relativos e caminhos `C:\\...` dentro do
  prefixo ativo; caminhos absolutos Linux continuam rejeitados.
- As APIs wide de arquivo cobrem o subconjunto exercitado por `tl_files_wide`
  e `tl_file_metadata`:
  `CreateFileW`, tamanho/posição, atributos, tempos, cópia/movimentação,
  diretórios e nomes finais; não inventam letras de drive nem aceitam caminhos
  absolutos Windows.
- `FileSlot` rastreia tamanho, posição e exclusão pendente; `ReadFile` e
  `WriteFile` atualizam a posição automaticamente a partir do descritor host,
  mantendo `SetFilePointer(FILE_CURRENT)` consistente após I/O.
- APIs que usam um `FileSlot` mantêm o lock até concluir a operação;
  `CloseHandle` não pode limpar ou reutilizar o descritor durante leitura,
  escrita, seek, consulta de metadados ou atualização de atributos/tempos.
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
- 18 testes unitários novos em `tests/test_win32.cpp` cobrem `GetFileSize`,
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

`WS2_32.dll` é um módulo separado. O contrato aceita AF_INET, TCP/UDP,
`getaddrinfo` para `localhost`/loopback, `WSAAddressToStringA` para converter
endpoints IPv4, conversões de ordem de bytes, `WSAPoll` e o subconjunto de
eventos WSA (`WSAEventSelect`, objetos manuais, espera e enumeração de
eventos). A fixture nunca acessa Internet; no sandbox sem permissão de
socket, o teste retorna um skip controlado, enquanto a validação com loopback
permitido passa de ponta a ponta.

`WININET.dll` é separado de `WS2_32.dll` e atende somente um cliente HTTPS
direto de loopback: `localhost`/`127.0.0.1`, `INTERNET_FLAG_SECURE`,
`GET`/`HEAD`/`POST`, sem proxy, credenciais, cookies, cache ou
redirecionamento. A CA é fornecida somente pelo host via
`TL_WININET_CA_FILE`, removida do ambiente visível ao convidado e usada em
um teste TLS local; não há loja de certificados, validação de cadeia Windows
nem WinTrust. O contrato completo está em
[`wininet.md`](arquitetura/wininet.md).

`MPR.dll` expõe somente os seis símbolos consumidos pelas fixtures atuais.
Todos validam os argumentos convidados antes de responder; as operações
válidas retornam `ERROR_NOT_SUPPORTED`, sem conexões, credenciais ou estado de
enumeração. `WNetOpenEnumW` deixa o handle de saída nulo, e handles que não
foram emitidos pelo runtime são rejeitados por `WNetEnumResourceW` e
`WNetCloseEnum` com `ERROR_INVALID_HANDLE`. No `--report`, esses exports são
classificados como `stub`; resolução de import não significa suporte de rede.

## Cadeia WinTrust explícita

`WINTRUST.dll` expõe `WinVerifyTrust` e os três `WTHelper*` no contrato de blob
descrito em [`wintrust.md`](arquitetura/wintrust.md). As fixtures `tl_trust.exe`
e `tl_wthelper.exe` usam dois certificados DER reais (folha e raiz): a primeira
verifica a cadeia e a segunda consulta/fecha o estado. Não há loja de
certificados do sistema, Authenticode, `WTD_CHOICE_FILE`, catálogo ou
revogação.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `WINTRUST.dll` | `WinVerifyTrust` | Suportado no subconjunto | Valida `WINTRUST_ACTION_GENERIC_VERIFY_V2` + `WTD_CHOICE_BLOB` com envelope `TLTC`, cadeia de dois DER, assinatura/validade X.509 e raiz explícita; HRESULT não nulo para política ou cadeia inválida |
| `WINTRUST.dll` | `WTHelperProvDataFromStateData`, `WTHelperGetProvSignerFromChain`, `WTHelperGetProvCertFromChain` | Suportado no subconjunto | Consulta o estado criado por `WTD_STATEACTION_VERIFY`, signer `0` e certificados folha/raiz `0..1`; rejeita contra-assinantes, índices inválidos e ponteiros externos; estado encerra em `CLOSE` |
| `CRYPT32.dll` | `CertGetNameStringW` | Suportado no subconjunto | Valida `CERT_CONTEXT`/estrutura DER e extrai `CERT_NAME_SIMPLE_DISPLAY_TYPE`, `CERT_NAME_FRIENDLY_DISPLAY_TYPE`, `CERT_NAME_DNS_TYPE`, `CERT_NAME_EMAIL_TYPE` ou `CERT_NAME_ATTR_TYPE`; sem verificação criptográfica, loja, SAN, Authenticode ou `Cert*` de cadeia |
| `CRYPT32.dll` | `CertNameToStrW` | Suportado no subconjunto | Valida `CERT_NAME_BLOB` DER e converte os tipos simple/OID/X.500 para UTF-16, com separadores, ordem reversa, quoting e limites de buffer no subconjunto publicado; sem RDNs multiatributo, loja ou verificação criptográfica |
| `CRYPT32.dll` | `CertOpenStore` / `CertCloseStore` | Suportado no subconjunto | Loja em memória e wrappers `CertOpenSystemStoreA/W` com nomes validados; provedores não implementados retornam `ERROR_NOT_SUPPORTED`, sem loja Windows ou leitura do trust store Linux |
| `CRYPT32.dll` | `CryptQueryObject` | Rejeição controlada | Formatos de consulta ainda não implementados retornam `FALSE` + `ERROR_NOT_SUPPORTED`, zeram as saídas válidas e nunca fabricam handles ou `CERT_CONTEXT`; Authenticode, arquivos assinados e CMS permanecem fora do contrato |
| `CRYPT32.dll` | `CryptMsgClose` | Rejeição controlada | Handles nulos ou não emitidos pelo runtime retornam `FALSE` + `ERROR_INVALID_HANDLE`; nenhum estado CMS é fabricado |
| `CRYPT32.dll` | `CryptMsgGetParam` | Rejeição controlada | Handles nulos ou não emitidos pelo runtime retornam `FALSE` + `ERROR_INVALID_HANDLE` e tamanho zero; `pcbData` nulo retorna `ERROR_INVALID_PARAMETER`; CMS/Authenticode ainda não são implementados |
| `WTSAPI32.dll` | `WTSEnumerateSessionsW` / `WTSFreeMemory` | Suportado no subconjunto | Retorna uma sessão local `Console` com alocação rastreada; `WTSFreeMemory` só libera blocos emitidos pelo runtime |
| `WTSAPI32.dll` | `WTSQuerySessionInformationW` | Stub controlado | Retorna `FALSE` + `ERROR_NOT_SUPPORTED`, zera os parâmetros de saída e emite trace |

## Registro genérico

`ADVAPI32.dll` não possui mais chave ou valor específicos do Todo. O subconjunto
de `RegCreateKeyEx[A/W]`, `RegOpenKeyEx[A/W]`, `RegSetValueEx[A/W]`,
`RegQueryValueEx[A/W]`, `RegDeleteValue[A/W]` e `RegCloseKey` usa chaves/valores
genéricos e persiste bytes, tipo e nomes normalizados em UTF-8 em um arquivo por
escopo (`APPDATA`, ou `TL_REGISTRY_FILE` para testes). Para `REG_SZ` e
`REG_EXPAND_SZ`, `RegSetValueExA/W` converte respectivamente de CP1252 e
UTF-16LE para UTF-8 canônico; `RegQueryValueExA/W` converte de volta para a
codificação solicitada. Dados binários preservam os bytes originais. Hive real,
COM e as demais APIs `CRYPT32` continuam fora deste contrato. A conversão
`ProgramFilesDir` wide é protegida por
`Win32RegistryTest.WideQueryConvertsDefaultProgramFilesValueToUtf16` e o
round-trip A/W por `Win32RegistryTest.WideSetAndAnsiQueryUseTheSameStringValue`.

## Segurança virtual por prefixo

`ADVAPI32.dll` expõe token não elevado do processo atual, SID virtual
persistente e DACLs para objetos existentes em `C:\` do prefixo. O owner/group
fixo é o SID artificial `S-1-5-21-<a>-<b>-<c>-1000`; um objeto sem metadado
recebe uma ACE allow `GENERIC_ALL` para ele. `GetNamedSecurityInfoW` retorna um
bloco liberável por `LocalFree`; `SetNamedSecurityInfoW` e `SetFileSecurityW`
persistem a DACL. Renomear preserva a DACL, excluir remove o metadado e copiar
restaura o padrão.

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `ADVAPI32.dll` | `OpenProcessToken`, `GetTokenInformation` | Suportado no subconjunto | Só `GetCurrentProcess()` + `TOKEN_QUERY`; `TokenUser` usa protocolo de buffer e `TokenElevation` é `0`; ponteiros de saída e o SID embutido são publicados por cópia protegida |
| `ADVAPI32.dll` | Operações de SID e `CheckTokenMembership` | Suportado no subconjunto | SID variável validado por snapshot protegido; World/Admin conhecidos; usuário virtual pertence apenas ao próprio SID; cópias e tamanhos de saída usam a fronteira protegida |
| `ADVAPI32.dll` | `InitializeSecurityDescriptor`, `SetSecurityDescriptorDacl`, `SetEntriesInAclW` | Suportado no subconjunto | Descritor absoluto e ACE allow/deny; `GRANT`, `SET`, `DENY`, `REVOKE`; somente trustee SID |
| `ADVAPI32.dll` | `GetNamedSecurityInfoW`, `SetNamedSecurityInfoW`, `SetFileSecurityW` | Suportado no subconjunto | Arquivo existente em `C:\` do prefixo; owner/group imutáveis e DACL persistente |

SACL, auditoria, herança complexa, trustees por nome, certificados, privilégios,
elevação, `AccessCheck`, permissões POSIX e a identidade Linux não fazem parte
do contrato. As DACLs não bloqueiam `CreateFile` e não constituem sandbox. Ver
[seguranca-acl.md](arquitetura/seguranca-acl.md).

As rotas de token e SID rejeitam ponteiros convidados não acessíveis sem
desreferenciá-los diretamente; `Win32SecurityTest.ProtectedTokenAndSidBuffersRejectUnmappedPointers`
cobre saídas de token, SID, trustee, associação e tamanhos. A proteção de ACL e
descritores ainda é uma etapa separada.

## COM mínimo e streams em memória (ole32)

`ole32.dll` expõe `CoInitialize`/`CoUninitialize`/`CoTaskMemAlloc` e camada COM mínima para testes de inicialização. `CoCreateInstance`/`CoGetClassObject` validam `rclsid`/`riid`/`ppv` via `mapped_guest_range` e retornam `REGDB_E_CLASSNOTREG` (`0x80040154`) ou `CLASS_E_NOAGGREGATION` (`0x80040110`); `OleInitialize`/`OleUninitialize` são stubs `S_OK`. `CreateStreamOnHGlobal` acrescenta um `IStream` volátil, com vtable Microsoft x64 explícita, backing store anônimo do runtime ou um bloco válido de `GlobalAlloc`. O contrato detalhado está em [`ole-streams.md`](arquitetura/ole-streams.md).

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `ole32.dll` | `CoInitialize` / `CoInitializeEx` | Suportado | Retorna `S_OK` (0), ignora `reserved`/`coInit` |
| `ole32.dll` | `CoUninitialize` / `OleUninitialize` | Suportado | No-op |
| `ole32.dll` | `OleInitialize` | Suportado | Retorna `S_OK` |
| `ole32.dll` | `CoCreateInstance` / `CoGetClassObject` | Suportado | Valida `rclsid`/`riid`/`ppv`, `unkOuter==nullptr` senão `CLASS_E_NOAGGREGATION`, senão `REGDB_E_CLASSNOTREG`, `*ppv=nullptr` |
| `ole32.dll` | `CoTaskMemAlloc` / `CoTaskMemFree` / `CoTaskMemRealloc` | Suportado | `malloc`/`free`/`realloc` do host |
| `ole32.dll` | `CreateStreamOnHGlobal` | Suportado no subconjunto | Aceita `hGlobal=NULL` ou um bloco válido de `GlobalAlloc`; cria `IStream` em memória e preserva o bloco conforme `delete-on-release`. Backing store externo desconhecido e expansão além da capacidade de `GlobalAlloc` são rejeitados. `Read`/`Write`/`Seek`/`SetSize`/`Stat`, `QueryInterface` e referência são cobertos pelos testes; cópia, clone e lock de região permanecem fora do contrato |

## Concorrência (Fase 11)

O subsistema de concorrência adiciona suporte a threads convidadas, TLS,
sincronização por mutexe e handles de thread. O runtime executa no mesmo
processo filho; cada thread convidada recebe seu próprio TEB/GS, stack e
`thread_local` isolado. Handles de thread são codificados por endereço
(`kThreadHandleBase + índice`).

| Módulo | API | Estado | Comportamento suportado |
|---|---|---|---|
| `KERNEL32.dll` | `CreateThread` | Suportado | Aloca stack com guard page, TEB, `arch_prctl(GS)`, cria `std::thread` com wrapper que preserva GS; retorna handle de thread; falhas de recurso do host retornam `NULL`/`ERROR_NOT_ENOUGH_MEMORY`, e falha na inicialização de GS encerra a thread antes do entry point, registra `api-failure` e libera TEB/stack ao fechar o handle |
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
| `KERNEL32.dll` | TLS estático do módulo principal | Suportado no subconjunto | Copia o template `IMAGE_TLS_DIRECTORY`, preserva o zero-fill e, para o slot pointer-backed de contrato `0x430`, aloca um bloco zerado de `0x1000` por TEB; a memória é liberada no encerramento da thread |
| `KERNEL32.dll` | `InitializeCriticalSection` | Suportado | Side-table com `pthread_mutex_t` (máximo 32 entradas) |
| `KERNEL32.dll` | `EnterCriticalSection` | Suportado | `pthread_mutex_lock` via side-table |
| `KERNEL32.dll` | `LeaveCriticalSection` | Suportado | `pthread_mutex_unlock` via side-table |
| `KERNEL32.dll` | `DeleteCriticalSection` | Suportado | `pthread_mutex_destroy` + libera entrada na side-table |
| `KERNEL32.dll` | `ConvertThreadToFiber` / `ConvertThreadToFiberEx` / `ConvertFiberToThread` | Suportado | `Convert*` retorna token `0x*` + `g_current_fiber_data`; `Ex` ignora `flags`; `ConvertFiberToThread` limpa `g_current_fiber_data` |
| `KERNEL32.dll` | `CreateFiber` / `CreateFiberEx` / `SwitchToFiber` / `DeleteFiber` / `GetFiberData` | Suportado | Stub retorna token estático; `CreateFiberEx` ignora `stack_commit/reserve/flags`; `SwitchToFiber`/`DeleteFiber` no-op; `GetFiberData` retorna `g_current_fiber_data` |
| `KERNEL32.dll` | `CreateToolhelp32Snapshot` | Suportado | Enumera `/proc` (`TH32CS_SNAPPROCESS` apenas); retorna `&SnapshotSlot` ou `INVALID_HANDLE_VALUE` (`-1`) + `ERROR_INVALID_PARAMETER`/`NOT_ENOUGH_MEMORY`; `CloseHandle` libera slot |
| `KERNEL32.dll` | `Process32FirstW` / `Process32NextW` | Suportado | Valida `hSnapshot` e `dwSize==568`, preenche `PROCESSENTRY32W` via `/proc/[pid]/status` (`PPid`, `Threads`, `Name`→`szExeFile` wide); `Next` avança `next_index`; fim → `0` + `ERROR_NO_MORE_FILES` (18) |
| `KERNEL32.dll` | `OpenProcess` | Suportado | Valida `/proc/[pid]` existe; retorna token `kProcessHandleBase+pid` ou `NULL` + `87`; `CloseHandle` aceita token via range |
| `KERNEL32.dll` | `OutputDebugStringA` / `OutputDebugStringW` | Suportado | Emite diagnóstico `runtime_trace` com a mensagem |
| `KERNEL32.dll` | `SetDllDirectoryW` | Suportado | Define diretório adicional de busca de DLLs no runtime |
| `KERNEL32.dll` | `VirtualQueryEx` | Suportado | Consulta mapeamento do processo via `tl_VirtualQuery` |
| `KERNEL32.dll` | `GetTimeZoneInformation` | Suportado | Retorna fuso horário padrão UTC / `TIME_ZONE_ID_STANDARD` |
| `KERNEL32.dll` | `GetProcessId` | Suportado | Retorna PID do processo convidado ou handle associado |
| `KERNEL32.dll` | `QueryFullProcessImageNameW` | Suportado | Preenche nome e caminho da imagem do processo convidado |
| `KERNEL32.dll` | `FileTimeToLocalFileTime` | Suportado | Converte estrutura de tempo de arquivo |
| `KERNEL32.dll` | `GetLongPathNameW` / `GetShortPathNameW` | Suportado | Converte caminhos entre formatos curto e longo |
| `KERNEL32.dll` | `SetThreadPriority` | Suportado | Retorna sucesso para ajuste de prioridade |
| `KERNEL32.dll` | `GetProcessAffinityMask` | Suportado | Retorna máscara de afinidade do processo e do sistema |
| `KERNEL32.dll` | `CreateHardLinkW` | Suportado | Criação de hard links entre arquivos via chamada `link(2)` |
| `KERNEL32.dll` | `K32GetModuleFileNameExW` | Suportado | Retorna caminho da imagem do módulo executável |
| `GDI32.dll` | `CreateBitmap` | Suportado | Cria e registra handle de bitmap em memória |
| `GDI32.dll` | `StretchBlt` | Suportado | Cópia e redimensionamento de blocos de imagem em DC |
| `GDI32.dll` | `GetObjectW` | Suportado | Consulta informações de dimensões de BITMAP ou LOGFONTW e publica o resultado por cópia protegida |
| `GDI32.dll` | `CreateDIBSection` | Suportado no subconjunto | Valida `BITMAPINFO`/dimensões por snapshot protegido, publica `ppvBits` por escrita protegida, aloca bitmap DIB com ponteiro direto a pixels e limita a superfície a 256 MiB |
| `OLEAUT32.dll` | `SysAllocString` / `SysAllocStringLen` / `SysFreeString` / `SysStringLen` / `SysStringByteLen` | Suportado | Alocação, liberação e consulta de BSTR com cabeçalho de 4 bytes e terminação null |
| `OLEAUT32.dll` | `VariantInit` / `VariantClear` / `VariantCopy` / `VariantCopyInd` / `VariantChangeType` | Suportado | Gerenciamento e clonagem de estruturas VARIANT |
| `OLEAUT32.dll` | `SafeArrayCreate` / `SafeArrayDestroy` / `SafeArrayGetDim` / `SafeArrayAccessData` / etc. | Suportado | Suporte e gerenciamento de contêineres SafeArray multidimensionais |
| `KERNEL32.dll` | `GetTickCount` | Suportado | Retorna tempo de uptime do sistema em milissegundos via `CLOCK_MONOTONIC` |
| `KERNEL32.dll` | `SetCurrentDirectoryW` | Suportado | Altera diretório de trabalho do processo no Linux via `chdir` |
| `KERNEL32.dll` | `DeviceIoControl` | Suportado | Stub de controle de dispositivos e consultas de I/O de disco |
| `KERNEL32.dll` | `FoldStringW` | Suportado | Mapeamento e normalização de strings wide |
| `KERNEL32.dll` | `SetThreadExecutionState` | Suportado | Gerenciamento de energia e estado de suspensão de thread |
| `KERNEL32.dll` | `AllocConsole` / `AttachConsole` / `FreeConsole` | Suportado | Ciclo de vida e alocação de console Win32 |
| `KERNEL32.dll` | `SystemTimeToTzSpecificLocalTime` | Suportado | Conversão de estrutura `SYSTEMTIME` para fuso horário local |
| `KERNEL32.dll` | `IsDBCSLeadByte` | Suportado | Detecção de lead bytes para páginas de código multibyte |
| `KERNEL32.dll` | `GetNumberFormatW` | Suportado | Formatação numérica em buffers wide |
| `USER32.dll` | `SetUserObjectInformationW` | Suportado | Configuração de atributos em objetos de usuário |
| `USER32.dll` | `WaitForInputIdle` | Suportado | Sincronização de prontidão de entrada de processo |
| `USER32.dll` | `FindWindowExW` | Suportado | Busca hierárquica de janelas filhas |
| `USER32.dll` | `SetProcessDefaultLayout` | Suportado | Configuração de layout de renderização de janelas (LTR/RTL) |
| `ADVAPI32.dll` | `LookupPrivilegeValueW` | Suportado | Resolução de LUID para identificadores de privilégios de segurança |
| `ADVAPI32.dll` | `AdjustTokenPrivileges` | Suportado | Ajuste e concessão de privilégios em tokens de processo |
| `SHELL32.dll` | `SHFileOperationW` / `SHGetFileInfoW` | Não suportado controlado | Operações de arquivo e metadados/ícones do Shell retornam `ERROR_NOT_SUPPORTED`; `SHFileOperationW` marca `fAnyOperationsAborted` e zera `hNameMappings`, sem alterar o sistema de arquivos. |
| `SHELL32.dll` | `SHGetPathFromIDListW` | Suportado | Conversão de lista de IDs de shell para caminho no sistema de arquivos |
| `SHELL32.dll` | `SHBrowseForFolderW` | Não suportado controlado | Valida o `BROWSEINFO` mínimo e retorna `nullptr` + `ERROR_NOT_SUPPORTED`; nenhum diálogo ou PIDL é fabricado |
| `SHELL32.dll` | `SHGetMalloc` | Suportado | Obtenção do alocador de memória padrão do Shell |
| `SHELL32.dll` | `SHChangeNotify` | Suportado | Emissão e notificação de eventos do sistema de arquivos para o shell |
| `ole32.dll` | `CLSIDFromString` | Suportado | Conversão de strings de GUID/CLSID para estrutura binária `GUID` |
| `SHLWAPI.dll` | `SHAutoComplete` | Suportado | Retorna `S_OK` para autocompletar em caixas de texto |
| `SHLWAPI.dll` | `PathIsRelativeA` / `PathIsRelativeW` | Suportado | Identifica se um caminho é relativo ou absoluto |

### Limitações conhecidas

- O slot 0 de TLS (`TlsGetValue(0)`) é reservado para o ponteiro ao TEB
  (`NtTib.Self`); o convidado não deve chamar `TlsAlloc` para obter o TEB.
- A side-table de `CRITICAL_SECTION` suporta no máximo 32 seções simultâneas;
  exaustão emite trace de `side-table` com `category="exhaustion"`.
- Handles nomeados não são compartilhados entre processos; o nome é validado,
  mas a tabela é local ao processo host.
- `CreateThread` não suporta `CREATE_SUSPENDED`; `stack_size == 0` usa o
  tamanho padrão (1 MiB). Se o host não puder reservar a stack, o TEB ou a
  thread POSIX, a API falha de forma controlada e libera o estado parcial.
- Se `ARCH_SET_GS` falhar depois da criação do handle, a thread é marcada como
  concluída sem chamar o entry point convidado; `WaitForSingleObject` observa
  a conclusão, e `CloseHandle` libera o TEB, a stack e o slot. Esse caminho é
  protegido por injeção somente de teste e pelo evento `api-failure` com
  `symbol="CreateThread"` e `operation="guest-teb"`.
- `ExitThread` termina somente a thread corrente; não limpa destructors C++.
- O fim de vida de threads convidadas usa trampolim `setjmp`/`longjmp`
  (`thread_local`): `ExitThread` nunca atravessa `pthread_exit`; o `join` real
  acontece em `CloseHandle` (com guarda contra fechamento duplo), que também
  libera a pilha mapeada e invalida o cache de `/proc/self/maps`.
- O fixture `tl_thread.exe` requer mingw-w64 para cross-build; a regressão e2e
  está coberta por `fixture_tl_thread_metadata` e
  `runtime_tl_thread_matches_readobj`.
- O fixture `tl_tls_generic.exe` cobre um template TLS com byte inicializado,
  zero-fill, leitura do slot pointer-backed `0x430` e leitura zero-inicializada
  do bloco associado. O mecanismo continua sendo um subconjunto orientado por
  evidência, não uma implementação de TLS dinâmica universal. A validação
  Debug do unitário e dos quatro testes CTest passou em 2026-09-04.
- Testes unitários em `tests/test_win32.cpp` cobrem `TlsAlloc`,
  `TlsSetValue`, `TlsGetValue`, `TlsFree`, `GetCurrentThreadId`,
  `GetCurrentProcessId`, `CRITICAL_SECTION` (init/enter/leave/delete,
  null check, side-table exhaustion), `CloseHandle` (null/garbage),
  `WaitForSingleObject` (invalid handle, timeout), e `TlsSetGetValue`
  com múltiplos slots.
