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

Cada exportação de módulo interno deve declarar explicitamente `Full`,
`Limited` ou `Stub` no registro `ExportedFunction`; não há mais classificação
implícita. O registro rejeita valores fora desse conjunto antes de publicar o
módulo, e o `--report` expõe a classificação efetivamente resolvida.

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
`GetFileVersionInfoExA/W` e `VerQueryValueA/W` são stubs controlados: não
leem `RT_VERSION`, não fabricam metadados e não retornam ponteiros host.
`InitCommonControls`,
image lists, `CreateStatusWindowW`, `CreateToolbarEx`, `PropertySheetW`,
`TaskDialog*` e subclassing de controles possuem apenas os contratos de
fixtures. `SetWindowTheme` e funções UxTheme são stubs/limitadas; DWM, WINMM,
DirectX, drivers e GPU são resolvidos somente quando o diagnóstico controlado
exige uma rejeição segura.

## APIs adicionais registradas nos roadmaps

Esta seção fecha o inventário nominal das funções que aparecem nos marcos
históricos, mas não pertencem ao caminho principal descrito acima. As listas
mantêm o nome da exportação para permitir conferência direta com `--report`;
as funções de um mesmo grupo compartilham o contrato indicado.

### Contexto de processo e sistema — KERNEL32

`GetStartupInfoA/W`, `GetSystemInfo`, `GetNativeSystemInfo`, `GetLogicalDrives`,
`GetLogicalDriveStringsW`, `GetCurrentProcessorNumber`,
`GetLogicalProcessorInformation`, `GetPhysicallyInstalledSystemMemory`,
`GlobalMemoryStatus`, `GlobalMemoryStatusEx`, `GetTimeZoneInformation`,
`GetProcessTimes`, `GetThreadTimes`, `GetProcessId`,
`QueryFullProcessImageNameW`, `GetProcessAffinityMask`,
`SetProcessAffinityMask`, `SetThreadAffinityMask`, `SetThreadPriority`,
`SetPriorityClass`, `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `Beep`,
`OutputDebugStringA/W`, `EncodePointer`, `DecodePointer`,
`InitializeSListHead`, `InterlockedPushEntrySList`, `InterlockedFlushSList`,
`GetVersion`, `GetLargePageMinimum`, `GetCurrentProcessorNumber` e
`SwitchToThread` expõem o contexto mínimo para fixtures e diagnósticos. Dados
sem equivalente seguro no Linux usam valores determinísticos ou erro
controlado; essas funções não expõem hardware, firmware ou privilégios reais.

`GetVersionExA/W`, `VerifyVersionInfoW`, `VerSetConditionMask`,
`GetUserDefaultUILanguage`, `SetThreadLocale`, `SetThreadUILanguage`,
`GetSystemDefaultLangID` e `GetUserDefaultLangID` completam o contexto de
versão e idioma virtual. Eles não alteram o locale global do hospedeiro.

### Arquivos avançados, pipes e INI — KERNEL32

`CreateFileMappingA/W`, `OpenFileMappingA/W`, `MapViewOfFile`,
`UnmapViewOfFile`, `FlushViewOfFile`, `GetDiskFreeSpaceA/W`,
`GetDiskFreeSpaceExA/W`, `GetDriveTypeA/W`, `GetVolumeInformationA/W`,
`GetVolumePathNameA/W`, `SetNamedPipeHandleState`, `TransactNamedPipe`,
`PeekNamedPipe`, `CreateNamedPipeA`, `ConnectNamedPipe`, `CreatePipe`,
`WaitNamedPipeA/W`, `CancelIo`, `ReadDirectoryChangesW` e
`GetOverlappedResult` têm implementação limitada a arquivos, pipes e
mapeamentos controlados pelo prefixo. I/O overlapped completo e IPC arbitrário
não fazem parte do contrato.

`GetPrivateProfileStringA/W`, `GetPrivateProfileIntA/W`,
`GetPrivateProfileSectionA/W` e `WritePrivateProfileStringA/W` operam em INI
aprovado pelo resolvedor de caminhos. `lstrlenW`, `lstrcpyW`, `lstrcpynA/W`,
`lstrcmpA/W`, `lstrcmpiA/W`, `lstrcatW` e `MulDiv` são helpers de texto e
aritmética com buffers validados; não são uma autorização para ignorar os
limites do endereço convidado.

`CreateHardLinkW`, `FileTimeToDosDateTime`, `DosDateTimeToFileTime`,
`GetCompressedFileSizeW`, `SetSearchPathMode`, `GetApplicationRestartSettings`,
`RegisterApplicationRestart` e `UnregisterApplicationRestart` são helpers de
arquivo ou ciclo de vida com resultado limitado ao prefixo. Recursos de restart,
compressão e busca não iniciam serviços auxiliares no host.

### PSAPI, Toolhelp e recursos PE

`CreateToolhelp32Snapshot`, `Process32First`, `Process32FirstW`,
`Process32Next`, `Process32NextW` e `OpenProcess` expõem enumeração limitada de
processos Linux e handles de processo. `EnumProcesses`, `EnumProcessModules`,
`EnumProcessModulesEx`, `GetModuleBaseNameA/W`, `GetModuleFileNameExA/W`,
`GetProcessMemoryInfo`, `K32GetProcessMemoryInfo` e
`K32GetProcessImageFileNameA` fazem o mesmo para PSAPI. Processos protegidos,
módulos kernel e namespaces de outros usuários não são simulados.

`FindResourceA/W`, `FindResourceExW`, `LoadResource`, `LockResource` e
`SizeofResource` acessam recursos da imagem PE já validada. `FindResource` não
carrega DLL externa implicitamente e nunca autoriza acesso fora da seção
`.rsrc` mapeada.

### Geometria, entrada e recursos visuais — USER32

`ClientToScreen`, `ScreenToClient`, `MapWindowPoints`, `PtInRect`, `CopyRect`,
`OffsetRect`, `InflateRect`, `IntersectRect`, `SubtractRect`, `SetRectEmpty`,
`IsRectEmpty`, `GetWindowPlacement`, `SetWindowPlacement`, `IsZoomed`,
`IsIconic`, `GetLastActivePopup`, `GetShellWindow`, `GetProcessWindowStation`,
`GetUserObjectInformationW`, `SetUserObjectInformationW`, `GetMonitorInfoA/W`,
`MonitorFromWindow`, `MonitorFromPoint`, `MonitorFromRect`,
`EnumDisplayDevicesA`, `EnumDisplaySettingsA` e `SystemParametersInfoA/W`
convertem coordenadas e consultam metadados lógicos da superfície X11. Dados
do window manager ou do desktop que não podem ser reproduzidos retornam valor
controlado.

`LoadCursorA/W`, `LoadIconA/W`, `LoadImageA/W`, `CopyImage`, `DestroyIcon`,
`DestroyCursor`, `SetCursor`, `ShowCursor`, `GetIconInfo`, `GetIconInfoExW`,
`CreateIconIndirect`, `DrawIcon`, `DrawIconEx`, `FlashWindow`,
`FlashWindowEx`, `MessageBeep`, `RegisterWindowMessageA/W`, `NotifyWinEvent`,
`GetMessageTime`, `GetQueueStatus`, `GetKeyboardLayout`, `GetKeyboardState`,
`SetKeyboardState`, `MapVirtualKeyW`, `ToAscii`, `ToAsciiEx` e
`SetProcessDpiAwarenessContext` fornecem apenas o subconjunto de entrada,
ícones e DPI coberto pelas fixtures. Não existe acessibilidade ou integração
de desktop completa.

### GDI avançado e impressão

`SetWindowOrgEx`, `SaveDC`, `RestoreDC`, `OffsetWindowOrgEx`, `SetBrushOrgEx`,
`SetMapMode`, `SetROP2`, `GetROP2`, `GetCurrentObject`, `GetTextAlign`,
`SetTextAlign`, `GetBkMode`, `GetPixel`, `SetPixel`,
`GetSystemPaletteEntries`, `RealizePalette`, `SelectPalette`,
`SetPaletteEntries`, `TranslateCharsetInfo`, `CreatePatternBrush`,
`CreateHatchBrush`, `PatBlt`, `MaskBlt`, `PlgBlt`,
`GetCharABCWidthsFloatA`, `GetCharacterPlacementW`, `GetOutlineTextMetricsA`,
`CreateDCA`, `GetDeviceGammaRamp` e `UpdateColors` operam somente nos DCs,
objetos e formatos suportados pelo backend lógico. `StartDocW`, `StartPage`,
`EndPage`, `EndDoc` e `AbortDoc` retornam falha controlada quando não há
impressora configurada; o runtime não finge um spooler Windows.

### Bibliotecas auxiliares e stubs de hardware

`WNetAddConnection2W`, `WNetOpenEnumW`, `WNetEnumResourceW`, `WNetCloseEnum`,
`WNetGetResourceInformationW` e `WNetGetResourceParentW` (`MPR.dll`) são
stubs controlados. Eles validam as estruturas, strings, buffers e ponteiros
convidados; operações válidas que exigiriam um provedor MPR retornam
`ERROR_NOT_SUPPORTED`, sem criar conexões, credenciais ou handles. Como não há
tabela de enumeração MPR, `WNetOpenEnumW` deixa a saída nula e
`WNetEnumResourceW`/`WNetCloseEnum` rejeitam handles desconhecidos com
`ERROR_INVALID_HANDLE`.

`PowerGetActiveScheme`, `PowerSetActiveScheme` e `CallNtPowerInformation`
(`POWRPROF.dll`) têm retorno controlado para consultas de energia. As APIs
`CM_Get_Child`, `SetupDiGetClassDevsA`, `SetupDiEnumDeviceInfo`,
`SetupDiEnumDeviceInterfaces`, `SetupDiGetDeviceInterfaceDetailA`,
`SetupDiGetDeviceRegistryPropertyA`, `SetupDiGetDeviceInstanceIdA` e
`SetupDiDestroyDeviceInfoList` (`CFGMGR32/SETUPAPI`) são stubs seguros para
diagnosticar dependências de hardware; não enumeram drivers reais.

`DwmSetWindowAttribute`, `DwmGetWindowAttribute`, `DwmIsCompositionEnabled`,
`DwmDefWindowProc`, `DwmExtendFrameIntoClientArea`, `DwmEnableBlurBehindWindow`,
`DwmFlush` e `DwmGetColorizationColor` (`DWMAPI.dll`) não criam uma composição
Windows: as operações de composição retornam `E_NOTIMPL`, limpam saídas válidas
e `DwmDefWindowProc` retorna somente “não tratado”.
`timeGetTime`, `timeBeginPeriod`, `timeEndPeriod`, `timeGetDevCaps`,
`PlaySoundA/W`, `timeSetEvent` e `timeKillEvent` (`WINMM.dll`) não agendam
callbacks multimídia nem acessam áudio do host.

`GdiplusStartup`, `GdiplusShutdown`, `GdipAlloc`, `GdipFree`,
`GdipCreateBitmapFromStream`, `GdipCloneImage`, `GdipDisposeImage` e
`GdipCreateHBITMAPFromBitmap` (`gdiplus.dll`), `SymFromAddr`/`ImageNtHeader`
(`DBGHELP.dll`), `ImmGetContext`/`ImmReleaseContext` e as demais APIs
`IMM32.dll`, além de `SetWindowTheme`, `OpenThemeData`, `CloseThemeData` e
funções `UxTheme`, são contratos de importação controlada. `IMM32` não cria
contextos host nem processa composição; `UxTheme` não cria temas, brushes ou
buffered-paints. Essas APIs não constituem IME, GDI+, tema visual ou depuração
completos.

### Módulos de portfólio

As funções observadas nos alvos reais são cobertas pela mesma política genérica:
`CharPrevExA` e `DosDateTimeToFileTime` (7-Zip), `Arc`, `PathIsUNCW`,
`AlphaBlend`, `NetApiBufferFree`, `LresultFromObject`, `TdhGetPropertySize`,
`OpenPrinterW`, `WTSFreeMemory` e `CreateToolbarEx` (HWiNFO/controles), e
`WSAStartup`, `WSACleanup`, `socket`, `connect`, `getaddrinfo`,
`GetAdaptersInfo`, `GetAdaptersAddresses`, `CertOpenStore` e
`WTSEnumerateSessionsW` (Worker/RSL). As primeiras têm implementação limitada
ou stub controlado conforme o módulo; as últimas têm fixtures próprias e
retornos dependentes do ambiente, como `ERROR_NO_DATA` sem IPv4.

### Registro, identidade e segurança — ADVAPI32

`RegCloseKey`, `RegDeleteValueA/W`, `RegCreateKeyExA/W`, `RegOpenKeyExA/W`,
`RegQueryValueExA/W`, `RegSetValueA/W`, `RegDeleteTreeW`, `RegEnumValueA/W`,
`RegEnumKeyA/W`, `RegDeleteKeyA/W`, `RegDeleteKeyExW`, `RegGetValueW` e
`RegQueryInfoKeyA/W` implementam chaves, valores, enumeração e remoção no
registro persistente do prefixo. O armazenamento não é o registro global do
Linux e não permite atravessar o prefixo.

`OpenProcessToken`, `GetTokenInformation`, `AllocateAndInitializeSid`,
`FreeSid`, `GetLengthSid`, `CopySid`, `EqualSid`, `IsValidSid`,
`CreateWellKnownSid`, `CheckTokenMembership`, `BuildTrusteeWithSidW`,
`InitializeSecurityDescriptor`, `SetSecurityDescriptorDacl`,
`SetSecurityDescriptorOwner`, `SetEntriesInAclW`, `GetNamedSecurityInfoW`,
`SetNamedSecurityInfoW`, `SetFileSecurityW`, `GetFileSecurityW`,
`LookupAccountNameW`, `GetUserNameA/W`, `LookupPrivilegeValueW` e
`AdjustTokenPrivileges` representam identidade, SID, DACL e privilégios
somente como metadados de compatibilidade. Eles não autenticam o usuário Linux,
não elevam privilégios e não aplicam ACLs Windows ao host. As saídas de LUID,
contadores, nomes, SID/domínio, handles de política e consultas do registro
usam cópias protegidas e rejeitam destinos não acessíveis.

`CryptAcquireContextA/W`, `CryptGenRandom`, `CryptReleaseContext`,
`CryptCreateHash`, `CryptHashData`, `CryptGetHashParam`, `CryptSetHashParam`,
`CryptDestroyHash`, `CryptSignHashW`, `CryptDecrypt`, `CryptExportKey`,
`CryptGetUserKey`, `CryptGetProvParam`, `CryptDestroyKey`,
`CryptEnumProvidersW`, `SystemFunction036` e `IsTextUnicode` existem para os
fluxos criptográficos e de identificação cobertos pelas fixtures. Provedores,
chaves privadas e armazenamento criptográfico do Windows não são expostos. As
rotas de hash, assinatura, exportação e aleatoriedade usam snapshots e cópias
protegidas para seus tamanhos, entradas e buffers de saída; `IsTextUnicode`
também usa snapshot da entrada e publicação protegida do resultado.

### Certificados e confiança

`CertGetNameStringW`, `CertDuplicateCertificateContext`,
`CertFreeCertificateContext`, `CertOpenStore`, `CertCloseStore`,
`CertEnumCertificatesInStore`, `CertFindCertificateInStore`,
`CertGetCertificateContextProperty`, `CertOpenSystemStoreA/W`,
`CertGetEnhancedKeyUsage`, `CertGetIntendedKeyUsage`, `CertNameToStrW`,
`CryptQueryObject`, `CryptMsgGetParam` e `CryptMsgClose` validam apenas DER,
`CERT_CONTEXT` e handles emitidos por este runtime. Entradas inválidas ou
handles desconhecidos retornam erro controlado.

`WinVerifyTrust`, `WTHelperProvDataFromStateData`,
`WTHelperGetProvSignerFromChain` e `WTHelperGetProvCertFromChain` implementam
o contrato de blob/cadeia explícita usado por `tl_trust`. `WTD_CHOICE_FILE`,
revogação, loja Windows e Authenticode completo continuam fora do escopo.

### OLEAUT32, COMDLG32 e controles comuns

Além das operações BSTR, VARIANT e SAFEARRAY descritas acima, `OLEAUT32` expõe
`SysStringByteLen`, `SafeArrayDestroyData` e `SafeArrayDestroyDescriptor` com
ownership validado; descritores não emitidos pelo runtime são rejeitados.

`GetOpenFileNameA/W`, `GetSaveFileNameA/W`, `ChooseColorA/W`, `ChooseFontA/W`,
`PrintDlgW` e `CommDlgExtendedError` (`COMDLG32.dll`) são stubs controlados.
Não abrem seletor nativo, não retornam caminhos/seleções fictícios e não
alteram as estruturas do convidado; falhas válidas retornam `FALSE`,
`ERROR_NOT_SUPPORTED` e `CDERR_DIALOGFAILURE`.

`InitCommonControls`, `InitCommonControlsEx`, `ImageList_Create`,
`ImageList_Destroy`, `ImageList_Add`, `ImageList_AddMasked`,
`ImageList_ReplaceIcon`, `ImageList_GetImageCount`, `ImageList_Draw`,
`ImageList_DrawEx`, `ImageList_GetIcon`, `ImageList_Duplicate`,
`ImageList_SetBkColor`, `ImageList_GetBkColor`, `ImageList_GetIconSize`,
`ImageList_GetImageInfo`, `ImageList_Remove`, `ImageList_SetIconSize`,
`CreateStatusWindowW`, `CreateToolbarEx`, `PropertySheetW`, `TaskDialog`,
`TaskDialogIndirect`, `SetWindowSubclass`, `RemoveWindowSubclass` e
`DefSubclassProc` cobrem controles lógicos e imagens nas fixtures GUI. O
contrato não inclui o conjunto completo de classes, temas ou notificações de
`COMCTL32`.

### Shell e caminhos — SHELL32/SHLWAPI

`PathFileExistsA/W`, `PathIsDirectoryA/W`, `PathCombineA/W`,
`PathFindFileNameA/W`, `PathFindExtensionA/W`, `PathRemoveFileSpecA/W`,
`PathAddBackslashA/W`, `PathRemoveBackslashA/W`, `PathIsRelativeA/W`,
`PathStripToRootW`, `PathStripPathA/W`, `PathAddExtensionW`,
`PathRemoveExtensionA/W`, `PathAppendW`, `PathCompactPathExW`,
`PathGetDriveNumberW`, `PathMatchSpecA/W`, `PathIsUNCA/W`, `StrStrIA/W`,
`StrCmpIA/W`, `SHAutoComplete`, `AssocQueryStringW`, `ColorRGBToHLS`,
`ColorHLSToRGB` e `ColorAdjustLuma` formam o subconjunto `SHLWAPI` de
normalização, comparação e transformação de caminhos. Caminhos `Z:\` são
resolvidos pelo tradutor canônico, `C:\` fica restrito ao prefixo e resultados
de existência/diretório nunca consultam uma raiz do host sem validação.

`SHGetDesktopFolder`, `SHGetSpecialFolderLocation`, `SHGetSpecialFolderPathW`,
`Shell_NotifyIconA/W`, `ExtractIconExW`, `SHGetKnownFolderPath`,
`SHGetFolderPathW`, `SHGetFolderPathAndSubDirW`, `SHGetPathFromIDListW`,
`SHFileOperationW`, `SHGetFileInfoW`, `SHBrowseForFolderW`, `SHGetMalloc`,
`SHChangeNotify`, `ShellExecuteA/W`, `ShellExecuteExW`,
`SHCreateItemFromParsingName`, `CommandLineToArgvW`, `DragQueryFileW`,
`DragQueryPoint` e `DragFinish` retornam caminhos ou executam operações apenas
no modelo de prefixo documentado. `Shell_NotifyIcon` mantém um surrogate
lógico por janela; ele não registra ícone real no tray do desktop.
`SHBrowseForFolderW` é uma rejeição controlada (`ERROR_NOT_SUPPORTED`) e não
cria diálogo nem PIDL.

### Mapeamento entre roadmap e documentação

O inventário acima é deliberadamente organizado por contrato, não pela ordem
dos roadmaps. Para localizar a evidência sem duplicar textos:

| Conteúdo originalmente tratado no roadmap | Documento técnico principal |
|---|---|
| loader PE, imagem, relocations e imports | [`mapeamento-imagem.md`](mapeamento-imagem.md), [`imports.md`](imports.md) |
| ABI Microsoft x64, TEB, TLS e unwind | [`abi-x64.md`](abi-x64.md), [`unwinding-x64.md`](unwinding-x64.md), [`ambiente-locale-fls.md`](ambiente-locale-fls.md) |
| arquivos, prefixos, instalação e catálogo | [`instaladores-e-biblioteca.md`](instaladores-e-biblioteca.md), [`perfis-compatibilidade.md`](perfis-compatibilidade.md), [`rust-app-catalog-parser.md`](rust-app-catalog-parser.md) |
| GUI X11, diálogos e controles | [`gui-x11.md`](gui-x11.md), [`guia-ui-qt6.md`](guia-ui-qt6.md) |
| rede, TLS e confiança | [`ws2-32.md`](ws2-32.md), [`wininet.md`](wininet.md), [`wintrust.md`](wintrust.md) |
| bloqueios e resultados dos aplicativos | [`aplicativos-bloqueios.md`](aplicativos-bloqueios.md), [`../compatibilidade.md`](../compatibilidade.md), [`../diagnostico.md`](../diagnostico.md) |
| parser e FFI Rust | [`rust-pe-parser.md`](rust-pe-parser.md), [`rust-msix-parser.md`](rust-msix-parser.md), [`rust-profile-parser.md`](rust-profile-parser.md), [`rust-ffi.md`](rust-ffi.md) |

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
