# Catálogo das APIs Win32 implementadas

Este documento é o índice técnico das funções Win32 que foram introduzidas ou
explicitamente justificadas pelos roadmaps do TradutorLinux. O roadmap registra
sequência e decisão; este catálogo registra o contrato atual. Ele não é uma
promessa de compatibilidade geral com Windows nem substitui as declarações
compatíveis em `include/tradutorlinux/`.

## Como interpretar o catálogo

Cada função pertence a uma das seguintes categorias:

- **completa no escopo**: o comportamento usado pelo fixture ou aplicativo-alvo
  está implementado e protegido por teste;
- **limitada**: existe um contrato intencionalmente menor que o Windows, sempre
  descrito na seção correspondente;
- **stub controlado**: o import é resolvido, mas a operação retorna o erro ou
  valor documentado para permitir diagnóstico sem simular sucesso;
- **infraestrutura**: função interna do loader, parser ou ponte de ABI, não uma
  exportação Win32 para o convidado.

As variantes `A` usam buffers/strings ANSI conforme o contrato do projeto; as
variantes `W` usam UTF-16 convidado e conversão validada para UTF-8 no host.
Handles são tokens opacos associados ao processo/prefixo. Erros seguem o par
retorno Win32 + `GetLastError`, salvo onde a tabela indicar um contrato
específico.

## KERNEL32 e NTDLL

### Processo, módulos, ambiente e encerramento

`GetModuleHandleA/W`, `GetModuleHandleExA/W`, `GetProcAddress`, `LoadLibraryA/W`,
`LoadLibraryExA/W` e `FreeLibrary` consultam o grafo de módulos do loader. O
grafo suporta módulos internos registrados, DLLs lado a lado dentro do prefixo,
API Sets e resolução por nome, ordinal e encaminhamento quando o formato foi
validado. `FreeLibrary` não descarrega módulos internos.

`GetCommandLineA/W`, `GetEnvironmentVariableA/W`, `SetEnvironmentVariableW`,
`GetEnvironmentStringsW`, `FreeEnvironmentStringsW` e
`ExpandEnvironmentStringsW` expõem o ambiente do processo convidado. A linha
de comando é construída uma vez no contexto do processo; os buffers têm os
limites Win32 documentados e não expõem caminhos internos do host.

`GetCurrentProcess`, `GetCurrentProcessId`, `GetCurrentThread`,
`GetCurrentThreadId`, `GetExitCodeProcess`, `GetExitCodeThread`, `ExitProcess`,
`ExitThread`, `TerminateProcess` e `FreeLibraryAndExitThread` controlam o ciclo
de vida do processo ou da thread convidada. `ExitProcess` transfere o código
para o runner, que o propaga ao Linux; falhas por sinal são isoladas e
reportadas como `GuestFault 71`, e espera esgotada como `GuestTimeout 72`.

`CreateProcessA/W`, `OpenProcess`, `DuplicateHandle`, `ResumeThread`,
`SuspendThread`, `SetThreadContext`, `GetThreadContext` e `CreateRemoteThread`
existem apenas no subconjunto necessário aos fixtures. Não há serviços,
drivers, injeção de código ou modelo completo de segurança entre processos.

### Memória, heap e exceções

`VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery`, `VirtualAllocEx`,
`VirtualFreeEx`, `VirtualProtectEx`, `VirtualQueryEx`, `HeapCreate`,
`HeapDestroy`, `HeapAlloc`, `HeapReAlloc`, `HeapFree`, `HeapValidate`,
`HeapSize`, `HeapCompact`, `GlobalAlloc`, `GlobalLock`, `GlobalUnlock`,
`GlobalFree`, `GlobalSize`, `LocalAlloc` e `LocalFree` traduzem alocação para
memória Linux com validação de tamanho, permissões e ownership. A primeira
entrega aceita somente as combinações de flags cobertas pelos testes; não
oferece heaps Windows independentes completos nem memória compartilhada entre
processos.

`SetUnhandledExceptionFilter`, `AddVectoredExceptionHandler`,
`RemoveVectoredExceptionHandler`, `RaiseException`, `UnhandledExceptionFilter`
e `__C_specific_handler` integram a fronteira SEH x64. `RtlCaptureContext`,
`RtlLookupFunctionEntry`, `RtlVirtualUnwind`, `RtlPcToFileHeader`, `RtlUnwind` e
`RtlUnwindEx` validam `.pdata/.xdata`, contexto e limites da stack convidada.
Handlers estáticos ou formatos fora do contrato são rejeitados de modo
controlado; isso não é suporte a todo o ABI de exceções C++.

### Arquivos, caminhos e recursos

`CreateFileA/W`, `OpenFile`, `CreateFile2`, `CloseHandle`, `ReadFile`,
`WriteFile`, `FlushFileBuffers`, `SetFilePointer`, `SetFilePointerEx`,
`GetFileSize`, `GetFileSizeEx`, `SetEndOfFile`, `GetFileTime`, `SetFileTime`,
`GetFileInformationByHandle`, `GetFileInformationByHandleEx`,
`GetFinalPathNameByHandleW`, `LockFile`, `LockFileEx`, `UnlockFile` e
`UnlockFileEx` implement I/O regular síncrono no prefixo do aplicativo.

`DeleteFileA/W`, `MoveFileA/W`, `MoveFileExA/W`, `CopyFileW`, `CopyFileExW`,
`ReplaceFileW`, `CreateDirectoryA/W`, `RemoveDirectoryW`,
`GetFileAttributesA/W`, `GetFileAttributesExW`, `SetFileAttributesW`,
`FindFirstFileA/W`, `FindFirstFileExW`, `FindNextFileA/W`, `FindClose`,
`FindFirstStreamW`, `FindNextStreamW`, `FindFirstChangeNotificationW`,
`FindNextChangeNotification` e `FindCloseChangeNotification` cobrem
manutenção e enumeração de arquivos. A normalização de caminhos impede escape
do prefixo e não inventa letras de drive.

`GetCurrentDirectoryA/W`, `SetCurrentDirectoryA/W`, `GetFullPathNameA/W`,
`GetModuleFileNameA/W`, `GetSystemDirectoryA/W`, `GetWindowsDirectoryA/W`,
`GetTempPathA/W`, `GetTempFileNameW`, `GetVolumePathNameA/W` e
`GetFinalPathNameByHandleW` fornecem caminhos derivados do prefixo e do
contexto do processo. `GetLongPathNameW` e `GetShortPathNameW` têm semântica
limitada ao filesystem Linux.

`FindResourceA/W`, `FindResourceExW`, `LoadResource`, `LockResource` e
`SizeofResource` leem recursos da imagem PE mapeada; não carregam recursos de
DLLs externas sem que o módulo esteja no grafo.

### Threads, sincronização e espera

`CreateThread`, `CreateRemoteThread`, `ExitThread`, `WaitForSingleObject`,
`WaitForSingleObjectEx`, `WaitForMultipleObjects`, `WaitForMultipleObjectsEx`,
`CloseHandle`, `GetCurrentThreadId` e `GetExitCodeThread` usam a infraestrutura
de threads convidadas com uma ponte explícita Microsoft x64 ↔ System V AMD64.

`CreateEventA/W`, `OpenEventA/W`, `SetEvent`, `ResetEvent`, `CreateMutexA/W`,
`CreateMutexExW`, `ReleaseMutex`, `CreateSemaphoreA/W`,
`CreateSemaphoreExW`, `OpenSemaphoreW`, `ReleaseSemaphore`,
`CreateWaitableTimerA/W`, `OpenFileMappingA/W`, `SetWaitableTimer`,
`CancelWaitableTimer`, `WaitOnAddress`, `WakeByAddressSingle` e
`WakeByAddressAll` cobrem os objetos de sincronização usados pelos fixtures.
O trace registra criação, sinalização e começo/fim das esperas. Callbacks de
threadpool (`CreateThreadpoolWork`, `SubmitThreadpoolWork`,
`WaitForThreadpoolWorkCallbacks`, `CloseThreadpoolWork`,
`CreateThreadpoolTimer`, `SetThreadpoolTimer`,
`WaitForThreadpoolTimerCallbacks`, `CloseThreadpoolTimer`) têm cobertura
controlada, sem prometer o scheduler completo do Windows.

`InitializeCriticalSection`, `InitializeCriticalSectionEx`,
`InitializeCriticalSectionAndSpinCount`, `EnterCriticalSection`,
`TryEnterCriticalSection`, `LeaveCriticalSection` e `DeleteCriticalSection`
usam estado do host. `InitializeSRWLock`, aquisições/liberações SRW,
`SleepConditionVariableSRW`, `SleepConditionVariableCS`,
`WakeConditionVariable` e `WakeAllConditionVariable` cobrem o subconjunto
necessário para concorrência intra-processo. `TlsAlloc`, `TlsGetValue`,
`TlsSetValue`, `TlsFree` e `FlsAlloc/FlsGetValue/FlsSetValue/FlsFree` preservam
slots por thread e limpam o estado no encerramento.

### Console, locale e utilitários

`GetStdHandle`, `SetStdHandle`, `ReadFile`, `WriteFile`, `ReadConsoleA/W`,
`WriteConsoleA/W`, `GetConsoleMode`, `SetConsoleMode`,
`GetConsoleScreenBufferInfo`, `SetConsoleTextAttribute`, `AllocConsole`,
`AttachConsole`, `FreeConsole`, `GetFileType`, `GetConsoleOutputCP`,
`SetConsoleOutputCP` e `GetLastError/SetLastError` formam o contrato de console
e erros. A saída do convidado permanece em `stdout`; trace e diagnóstico ficam
em `stderr`.

`MultiByteToWideChar`, `WideCharToMultiByte`, `GetACP`, `GetOEMCP`, `GetCPInfo`,
`IsDBCSLeadByte`, `IsDBCSLeadByteEx`, `AreFileApisANSI`, `CompareStringA/W`,
`CompareStringEx`, `GetUserDefaultLCID`, `GetSystemDefaultLCID`,
`GetUserDefaultLocaleName`, `LocaleNameToLCID`, `GetLocaleInfoA/W`,
`GetLocaleInfoEx`, `IsValidLocale`, `IsValidCodePage`, `EnumSystemLocalesW`,
`GetStringTypeA/W`, `GetStringTypeExA/W`, `LCMapStringA/W`, `LCMapStringEx`,
`GetDateFormatW`, `GetDateFormatEx`, `GetTimeFormatW`, `GetTimeFormatEx`,
`GetNumberFormatW` e `FormatMessageA/W` implement conversões e formatação
determinísticas para as páginas de código e locales cobertas pelos testes.

`GetTickCount`, `GetTickCount64`, `Sleep`, `SleepEx`, `SwitchToThread`,
`QueryPerformanceCounter`, `QueryPerformanceFrequency`,
`GetSystemTime`, `GetLocalTime`, `SystemTimeToFileTime`, `FileTimeToSystemTime`,
`FileTimeToLocalFileTime`, `SystemTimeToTzSpecificLocalTime`,
`TzSpecificLocalTimeToSystemTime` e `CompareFileTime` usam relógios e fusos do
host com conversão explícita para FILETIME.

## USER32 e GDI32

### Janelas e message loop

`RegisterClassA/W`, `RegisterClassExA/W`, `UnregisterClassW`, `CreateWindowExA/W`,
`DestroyWindow`, `ShowWindow`, `UpdateWindow`, `SetWindowTextA/W`,
`GetWindowTextA/W`, `GetWindowTextLengthA/W`, `MoveWindow`, `SetWindowPos`,
`GetWindowRect`, `GetClientRect`, `GetParent`, `SetParent`, `IsWindow`,
`IsWindowVisible`, `IsWindowEnabled`, `EnableWindow`, `SetFocus`,
`GetFocus`, `SetForegroundWindow`, `GetForegroundWindow`, `GetActiveWindow`,
`GetDesktopWindow`, `GetSystemMetrics`, `GetSystemMetricsForDpi`,
`GetDpiForWindow`, `GetDpiForSystem` e `AdjustWindowRectExForDpi` modelam a
janela Win32 sobre X11.

`GetMessageA/W`, `PeekMessageA/W`, `TranslateMessage`, `DispatchMessageA/W`,
`DefWindowProcA/W`, `PostQuitMessage`, `SendMessageA/W`, `PostMessageA/W`,
`SendMessageTimeoutA`, `MsgWaitForMultipleObjects` e
`MsgWaitForMultipleObjectsEx` implement o pump e a entrega de mensagens.
`SetTimer`/`KillTimer` geram `WM_TIMER` no pump. O backend é X11 headless em
testes (`Xvfb`); não há suporte GUI amplo, Wayland ou comportamento de um
window manager real.

`FindWindowA/W`, `FindWindowExA/W`, `WindowFromPoint`, `ChildWindowFromPoint`,
`GetWindow`, `GetAncestor`, `GetClassNameA/W`, `GetWindowThreadProcessId`,
`CallWindowProcA/W`, `GetWindowLongW`, `GetWindowLongPtrA/W`,
`SetWindowLongW`, `SetWindowLongPtrA/W`, `SetClassLongPtrA/W`, `SetCapture`,
`ReleaseCapture`, `GetCapture`, `BringWindowToTop`, `SetCursorPos`,
`GetCursorPos`, `GetKeyState` e `GetAsyncKeyState` expõem apenas o estado
logical e eventos que o backend possui.

### Diálogos, menus, clipboard e controles

`DialogBoxParamA/W`, `DialogBoxIndirectParamW`, `CreateDialogParamA/W`,
`CreateDialogIndirectParamW`, `EndDialog`, `DefDlgProcA`, `GetDlgItem`,
`GetDlgItemTextA/W`, `SetDlgItemTextA/W`, `SendDlgItemMessageA/W`,
`CheckDlgButton`, `IsDlgButtonChecked`, `CheckRadioButton`, `MapDialogRect`,
`GetDialogBaseUnits`, `GetNextDlgTabItem` e `IsDialogMessageA/W` formam o
contrato mínimo de diálogo modal/modeless. Templates válidos são interpretados
com limites; controles genéricos têm apenas o comportamento coberto pelas
fixtures.

`CreateMenu`, `CreatePopupMenu`, `DestroyMenu`, `AppendMenuA/W`,
`InsertMenuA/W`, `InsertMenuItemW`, `RemoveMenu`, `DeleteMenu`, `SetMenu`,
`GetMenu`, `GetSubMenu`, `GetMenuItemCount`, `GetMenuItemID`,
`GetMenuItemInfoW`, `SetMenuItemInfoW`, `GetMenuState`, `GetMenuStringW`,
`EnableMenuItem`, `CheckMenuItem`, `CheckMenuRadioItem`, `DrawMenuBar`,
`TrackPopupMenu` e `TrackPopupMenuEx` mantêm estado lógico de menus e popup.
Desenho, bandeja e interação dependente de window manager continuam limitados.

`OpenClipboard`, `CloseClipboard`, `EmptyClipboard`, `SetClipboardData`,
`GetClipboardData`, `GetClipboardOwner`, `IsClipboardFormatAvailable`,
`RegisterClipboardFormatA/W`, `CountClipboardFormats`,
`EnumClipboardFormats`, `SetClipboardViewer` e `ChangeClipboardChain` têm
contrato mínimo por processo e formato, sem prometer integração completa com a
seleção/clipboard do desktop.

### Pintura e GDI

`GetDC`, `GetWindowDC`, `GetDCEx`, `ReleaseDC`, `BeginPaint`, `EndPaint`,
`GetUpdateRect`, `GetUpdateRgn`, `InvalidateRect`, `InvalidateRgn`,
`ValidateRect`, `ValidateRgn`, `FillRect`, `FrameRect`, `InvertRect`,
`DrawFocusRect`, `DrawEdge`, `DrawFrameControl`, `TextOutA`, `ExtTextOutA/W`,
`DrawTextA/W`, `GetTextExtentPoint32A/W`, `GetTextExtentExPointA/W`,
`GetTextMetricsA/W`, `GetCharWidthA/W`, `GetCharWidth32A/W` e
`GetCharABCWidthsA` cobrem pintura textual e retângulos do protótipo X11.

`GetStockObject`, `CreateFontA/W`, `CreateFontIndirectA/W`, `CreateSolidBrush`,
`CreatePen`, `CreateCompatibleDC`, `CreateCompatibleBitmap`, `CreateBitmap`,
`CreateDIBSection`, `SelectObject`, `DeleteObject`, `DeleteDC`,
`SetBkMode`, `SetBkColor`, `SetTextColor`, `SetDCBrushColor`,
`SetDCPenColor`, `GetDeviceCaps`, `BitBlt`, `StretchBlt`, `AlphaBlend`,
`TransparentBlt`, `GdiAlphaBlend`, `GradientFill`, `MoveToEx`, `LineTo`,
`Polyline`, `Polygon`, `Rectangle`, `Ellipse`, `Arc`, `Pie`, `RoundRect`,
`CreateRectRgn`, `CreateRectRgnIndirect`, `CombineRgn`, `SelectClipRgn`,
`GetClipBox`, `GetClipRgn`, `ExcludeClipRect`, `IntersectClipRect`,
`CreatePolygonRgn`, `FrameRgn`, `FillRgn`, `PaintRgn`, `InvertRgn`, `GetObjectA/W`
e `GetDIBits` fornecem o subconjunto reutilizável usado pelos alvos. GDI+,
impressão, fontes avançadas e todos os objetos gráficos do Windows permanecem
fora do contrato, mesmo quando um símbolo é resolvido como stub.

## Rede

### WS2_32

`WSAStartup`, `WSACleanup`, `WSAGetLastError`, `WSASetLastError`, `socket`,
`WSASocketA`, `closesocket`, `bind`, `listen`, `accept`, `connect`, `shutdown`,
`send`, `recv`, `sendto`, `recvfrom`, `select`, `WSAPoll`, `ioctlsocket`,
`getsockopt`, `setsockopt`, `getpeername`, `getsockname`, `gethostname`,
`getaddrinfo`, `freeaddrinfo`, `gethostbyname`, `gethostbyaddr`, `getnameinfo`,
`getservbyname`, `getservbyport`, `inet_addr`, `inet_ntoa`, `inet_pton`,
`inet_ntop`, `htonl`, `htons`, `ntohl`, `ntohs`, `WSAAsyncSelect`,
`WSAAsyncGetHostByName`, `WSAEventSelect`, `WSACreateEvent`, `WSACloseEvent`,
`WSASetEvent`, `WSAResetEvent`, `WSAWaitForMultipleEvents`,
`WSAEnumNetworkEvents`, `WSAIoctl` e `WSAAddressToStringA` são traduzidos para
sockets POSIX. Eventos assíncronos e notificações são limitados ao fluxo das
fixtures; não há implementação de todos os providers Winsock.

### WININET e IPHLPAPI

`InternetOpenW`, `InternetConnectW`, `HttpOpenRequestW`, `HttpAddRequestHeadersW`,
`HttpSendRequestW`, `InternetReadFile`, `InternetQueryDataAvailable`,
`InternetSetOptionW`, `HttpQueryInfoW`, `InternetCrackUrlW` e
`InternetCloseHandle` implementam o caminho HTTP usado pelos testes. Proxy,
cache, autenticação e todos os protocolos WinINet não fazem parte do contrato.

`GetAdaptersInfo`, `GetAdaptersAddresses`, `if_nametoindex` e
`NetApiBufferFree` usam `getifaddrs`/interfaces Linux; ausência de IPv4 pode
resultar em `ERROR_NO_DATA` de forma legítima. `WTSEnumerateSessionsW`,
`WTSQuerySessionInformationW` e `WTSFreeMemory` cobrem somente a sessão local.

## CRT e bibliotecas auxiliares

O `msvcrt.dll` mínimo inclui os argumentos de processo (`__getmainargs`,
`__initenv`), streams e I/O (`__iob_func`, `fopen`, `fclose`, `fread`,
`fwrite`, `fprintf`, `printf`-equivalentes), memória (`malloc`, `calloc`,
`realloc`, `free`), strings/memória (`memcpy`, `memmove`, `memset`, `strlen`,
`strcmp`, `strcpy`, `strstr`, variantes wide), conversão (`strtol`, `strtoul`,
`atoi`, `mbstowcs`, `wcstombs`), locale, tempo, sinais, `_beginthreadex`,
terminação C++ (`__CxxFrameHandler3`, `_CxxThrowException`, `_purecall`) e
funções de arquivo `_stat64`, `_wfopen`, `_wrename`, `_wunlink`.

Esse é um subconjunto de CRT orientado aos fixtures e aplicativos-alvo. Ele não
pretende substituir o runtime Microsoft completo. `OLEAUT32` cobre BSTR
(`SysAllocString*`, `SysReAllocString*`, `SysFreeString`, `SysStringLen`),
VARIANT (`VariantInit`, `VariantClear`, `VariantCopy*`, `VariantChangeType*`)
e SAFEARRAY (`SafeArrayCreate*`, acesso, limites, lock e destruição) com
validação dos buffers e descritores.

## COM, shell, segurança e módulos de suporte

`CoInitialize`, `CoInitializeEx`, `CoUninitialize`, `OleInitialize`,
`OleUninitialize`, `CoCreateGuid`, `CLSIDFromString`, `StringFromGUID2`,
`CLSIDFromProgID`, `CoTaskMemAlloc/Free/Realloc`, `CoCreateInstance`,
`CoGetClassObject`, `CreateStreamOnHGlobal`, `RegisterDragDrop`,
`RevokeDragDrop`, `DoDragDrop`, `ReleaseStgMedium` e `CoGetMalloc` formam
contratos mínimos de COM/OLE. O stream em memória implementa a vtable `IStream`
testada (`Read`, `Write`, `Seek`, `SetSize`, `Stat`, `Commit`, `Revert`);
registro COM, ActiveX e automação `IDispatch` ampla estão fora do escopo.

`SHGetKnownFolderPath`, `SHGetFolderPathW`, `SHGetFolderPathAndSubDirW`,
`SHFileOperationW`, `SHGetFileInfoW`, `SHGetPathFromIDListW`,
`SHBrowseForFolderW`, `SHGetMalloc`, `SHChangeNotify`, `ShellExecuteA/W`,
`ShellExecuteExW`, `CommandLineToArgvW`, `SHCreateItemFromParsingName`,
`DragQueryFileW`, `DragQueryPoint`, `DragFinish` e APIs de ícone/shell fornecem
somente caminhos e operações aprovados pelo prefixo. Não há integração geral
com o shell do desktop.

O registro usa `RegOpenKeyExA/W`, `RegCreateKeyExA/W`, `RegCloseKey`,
`RegQueryValueExA/W`, `RegSetValueExA/W`, enumeração e remoção de chaves/valores
em armazenamento por prefixo. `OpenProcessToken`, consultas de token/SID,
`AllocateAndInitializeSid`, `CheckTokenMembership`, ACLs
(`GetNamedSecurityInfoW`, `SetNamedSecurityInfoW`, `SetEntriesInAclW`) e
privilégios (`LookupPrivilegeValueW`, `AdjustTokenPrivileges`) têm contratos
limitados e não elevam privilégios Linux.

`CertGetNameStringW`, `CertOpenStore`, `CertCloseStore`, enumeração e
propriedades de certificados, `CryptMsgGetParam`/`CryptMsgClose` e
`WinVerifyTrust`/helpers de cadeia são validações controladas. Loja Windows,
revogação, Authenticode completo e provedores criptográficos do sistema não são
prometidos; handles desconhecidos falham sem acessar memória arbitrária.

`GetFileVersionInfoSizeA/W`, `GetFileVersionInfoA/W`, `GetFileVersionInfoSizeExA/W`,
`GetFileVersionInfoExA/W` e `VerQueryValueA/W` leem `RT_VERSION`. `InitCommonControls`,
image lists, `CreateStatusWindowW`, `CreateToolbarEx`, `PropertySheetW`,
`TaskDialog*` e subclassing de controles possuem apenas os contratos de
fixtures. `SetWindowTheme` e funções UxTheme são stubs/limitadas; DWM, WINMM,
DirectX, drivers e GPU são resolvidos somente quando o diagnóstico controlado
exige uma rejeição segura.

## Contratos internos Rust/C

Os roadmaps de Rust introduziram APIs `extern "C"` para análise e validação,
mas elas não são imports disponíveis aos executáveis Windows. Todas usam
buffers caller-owned, retorno de status e erro estruturado; nenhuma exceção ou
alocação Rust atravessa a ABI.

- `tl_pe_parse_v1_size` e `tl_pe_parse_v1_fill` validam PE e produzem o wire
  TLPE v1.0. A primeira consulta o tamanho necessário; a segunda preenche o
  buffer. O parser Rust é canônico somente nos caminhos promovidos e não executa
  nem mapeia a imagem. Detalhes: [`rust-pe-parser.md`](rust-pe-parser.md).
- `tl_msix_parse_v1_size` e `tl_msix_parse_v1_fill` validam MSIX/AppX e
  produzem TLMS; `tl_msix_inflate_raw` descomprime raw DEFLATE dentro dos
  limites. Detalhes: [`rust-msix-parser.md`](rust-msix-parser.md).
- `tl_profile_parse_v1_size` e `tl_profile_parse_v1_fill` validam perfis de
  compatibilidade e produzem TLPR. Detalhes: [`rust-profile-parser.md`](rust-profile-parser.md).
- `tl_app_catalog_parse_v1_size` e `tl_app_catalog_parse_v1_fill` validam o
  catálogo persistente e produzem TLAC. Detalhes:
  [`rust-app-catalog-parser.md`](rust-app-catalog-parser.md).
- `tl_rust_validator_create`/`destroy` controlam o handle opaco; as funções
  `tl_rust_validator_validate_utf8`, `validate_utf16`,
  `validate_relative_path` e `validate_c_drive_path` aplicam os limites de
  strings e caminhos do prefixo. O C++ é responsável pelo ownership do
  adaptador e o Rust nunca recebe ponteiros não validados. Detalhes:
  [`rust-ffi.md`](rust-ffi.md).

## Fixtures e evidência

As funções não são consideradas suportadas apenas porque aparecem no registro
de exports. A evidência deve vir de fixture PE32+ ou de aplicativo-alvo e ser
registrada em [`docs/compatibilidade.md`](../compatibilidade.md). As principais
fixtures são:

| Domínio | Fixtures representativas |
|---|---|
| loader, memória, arquivos, CRT | `tl_hello`, `tl_echo`, `tl_file`, `tl_dynload`, `tl_k32_gap`, `xxd`, `bzip2` |
| GUI, diálogo, menu e GDI | `tl_gui`, `tl_win`, `tl_win_w`, `tl_dialog`, `tl_user_ext`, `tl_gdiex` |
| threads e sincronização | `tl_thread`, `tl_sync`, `tl_waitaddr`, `tl_fiber`, `tl_proton_probe` |
| rede e confiança | `tl_ws2`, `tl_worker_rsl`, `tl_wininet`, `tl_crypt32`, `tl_wintrust` |
| COM/OLE e shell | `tl_com`, `tl_stream`, `tl_oleaut_bstr`, `tl_shell_path` |
| corpus real | matrizes em `tests/apps/` e smokes sob `Xvfb` |

O estado de cada aplicativo, o código de saída e as limitações observadas
continuam na matriz de compatibilidade. O trace definido em
[`docs/diagnostico.md`](../diagnostico.md) é a referência para diagnosticar
execução, não o nome do export.
