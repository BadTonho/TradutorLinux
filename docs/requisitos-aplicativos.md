# Registro unificado de requisitos de aplicativos

Este é o arquivo canônico para os achados do portfólio da Fase 13. Cada
aplicativo analisado ganha uma seção aqui; o conjunto serve para comparar
lacunas, contar recorrências e priorizar capacidades que beneficiem mais de uma
classe de uso.

Um registro deve conter, no mínimo:

- nome, versão quando conhecida, formato/arquitetura e SHA-256 da amostra;
- total de imports, resolvidos e ausentes no momento da análise;
- lista dos imports estáticos ausentes, agrupada por DLL;
- resultado de `--report` e, quando houver, do primeiro teste de execução;
- observações sobre carregamento dinâmico, fluxo testado e limitações.

Os imports abaixo são requisitos para passar a resolução estática. Eles não
provam sozinhos que um aplicativo vai executar: DLLs e funções carregadas
dinamicamente, processos-filhos e semântica de cada API precisam de validação
posterior.

## `RobloxPlayerInstaller.exe`

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `RobloxPlayerInstaller.exe` |
| Formato | PE32+ GUI x86-64, 7 seções |
| SHA-256 | `d156faf0c712d4ce26d95a596ad9b1dfc813021b5c422c93887b2522d8b01a59` |
| Imports estáticos | 430 em 17 DLLs |
| Resolvidos pelo runtime | 244 |
| Ausentes | 186 |
| Resultado observado | `Unsupported` (exit code `5`); o entry point não foi executado |
| Fonte | análise local de 2026-08-23 |

O Roblox é um benchmark de cobertura do portfólio, não um alvo exclusivo e nem
uma autorização para stubs específicos para ele.

### Lacunas por módulo

| DLL | APIs/ordinais ausentes |
|---|---:|
| `KERNEL32.dll` | 108 |
| `ADVAPI32.dll` | 29 |
| `WS2_32.dll` | 20 |
| `USER32.dll` | 14 |
| `CRYPT32.dll` | 11 |
| `COMCTL32.dll` | 2 |
| `ole32.dll` | 1 |
| `SHELL32.dll` | 1 |

### Imports estáticos ausentes

#### `KERNEL32.dll` (108)

```text
AreFileApisANSI
CancelWaitableTimer
CompareFileTime
CompareStringEx
CreateFile2
CreateMutexExW
CreateSemaphoreExW
CreateWaitableTimerA
CreateWaitableTimerW
DebugBreak
DecodePointer
DeviceIoControl
DuplicateHandle
EncodePointer
EnumSystemLocalesW
FindFirstFileExW
FindResourceExW
FlsAlloc
FlsFree
FlsGetValue
FlsSetValue
FormatMessageA
FreeEnvironmentStringsW
FreeLibraryAndExitThread
GetACP
GetCPInfo
GetCurrentProcessorNumber
GetCurrentThread
GetDateFormatW
GetDiskFreeSpaceA
GetDiskFreeSpaceW
GetEnvironmentStringsW
GetExitCodeThread
GetFileType
GetLocaleInfoEx
GetLocaleInfoW
GetLogicalProcessorInformation
GetModuleFileNameW
GetOEMCP
GetStartupInfoW
GetStringTypeW
GetSystemDirectoryA
GetSystemFirmwareTable
GetTempPathA
GetTickCount
GetTimeFormatW
GetTimeZoneInformation
GetVolumePathNameW
GlobalAlloc
GlobalFree
GlobalLock
GlobalUnlock
InitializeConditionVariable
InitializeCriticalSectionAndSpinCount
InitializeCriticalSectionEx
InitializeProcThreadAttributeList
InitializeSListHead
InitOnceBeginInitialize
InitOnceComplete
InterlockedPushEntrySList
IsDebuggerPresent
IsProcessorFeaturePresent
IsValidCodePage
IsValidLocale
K32GetModuleFileNameExW
K32GetProcessImageFileNameA
K32GetProcessMemoryInfo
LCMapStringEx
LCMapStringW
LocalAlloc
LockFile
LockFileEx
MoveFileExA
OpenEventW
OpenSemaphoreW
OutputDebugStringA
OutputDebugStringW
PeekNamedPipe
Process32First
Process32Next
ReadConsoleA
ReadConsoleW
RtlCaptureContext
RtlLookupFunctionEntry
RtlPcToFileHeader
RtlUnwind
RtlUnwindEx
RtlVirtualUnwind
SetConsoleCtrlHandler
SetEnvironmentVariableW
SetFileAttributesW
SetSearchPathMode
SetStdHandle
SetThreadDescription
SetWaitableTimer
SleepConditionVariableCS
SleepEx
SwitchToThread
SystemTimeToTzSpecificLocalTime
TryEnterCriticalSection
UnhandledExceptionFilter
UnlockFile
UnlockFileEx
UpdateProcThreadAttribute
WaitForMultipleObjectsEx
WaitForSingleObjectEx
WriteConsoleW
__C_specific_handler
```

#### `ADVAPI32.dll` (29)

```text
AllocateAndInitializeSid
CopySid
CryptCreateHash
CryptDecrypt
CryptDestroyHash
CryptDestroyKey
CryptEnumProvidersW
CryptExportKey
CryptGetHashParam
CryptGetProvParam
CryptGetUserKey
CryptHashData
CryptSetHashParam
CryptSignHashW
DeregisterEventSource
EqualSid
FreeSid
GetLengthSid
GetTokenInformation
GetUserNameW
IsValidSid
OpenProcessToken
RegDeleteTreeW
RegEnumKeyExW
RegGetValueW
RegisterEventSourceW
RegQueryInfoKeyW
ReportEventW
SystemFunction036
```

#### `WS2_32.dll` (20)

```text
getnameinfo
ordinal(111)
ordinal(112)
ordinal(115)
ordinal(116)
ordinal(151)
ordinal(51)
ordinal(52)
ordinal(55)
ordinal(56)
ordinal(57)
WSACloseEvent
WSACreateEvent
WSAEnumNetworkEvents
WSAEventSelect
WSAIoctl
WSAResetEvent
WSASetEvent
WSASocketA
WSAWaitForMultipleEvents
```

Os ordinais devem ser identificados contra a versão/ABI de `WS2_32` esperada
pelo binário antes de virar nomes de API no runtime; registrar um ordinal com
semântica presumida não é uma implementação válida.

#### `USER32.dll` (14)

```text
CallWindowProcW
DestroyIcon
DrawTextW
EnumDisplayDevicesA
GetDlgItem
GetProcessWindowStation
GetShellWindow
GetUserObjectInformationW
GetWindowLongW
GetWindowRect
GetWindowThreadProcessId
LoadAcceleratorsW
LoadBitmapW
TranslateAcceleratorW
```

#### `CRYPT32.dll` (11)

```text
CertCloseStore
CertDuplicateCertificateContext
CertEnumCertificatesInStore
CertFindCertificateInStore
CertFreeCertificateContext
CertGetCertificateContextProperty
CertGetEnhancedKeyUsage
CertGetIntendedKeyUsage
CertOpenStore
CertOpenSystemStoreA
CertOpenSystemStoreW
```

#### `COMCTL32.dll` (2)

```text
_TrackMouseEvent
TaskDialogIndirect
```

#### `ole32.dll` (1)

```text
StringFromGUID2
```

#### `SHELL32.dll` (1)

```text
Shell_NotifyIconW
```

### Próxima investigação

O portfólio, e não este instalador isoladamente, decidirá a ordem de
implementação. Os grupos inicialmente relevantes são unwinding/SEH x64,
primitivos de processo e sincronização, prefixo de instalação, WinSock com
eventos, certificados/identidade e controles GUI usuais. Cada grupo precisa de
contrato, fixture e regressão antes de ser promovido a suporte.
