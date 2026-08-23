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

## Índice de análises

| Aplicativo | Imports resolvidos | Imports ausentes | Bloqueio adicional | Estado |
|---|---:|---:|---|---|
| `RobloxPlayerInstaller.exe` | 244/430 | 186 | — | `unsupported` |
| `winrar-x64-723.exe` | 97/156 | 59 | `delay-import` | `unsupported` |
| `Creative_Cloud_Set-Up_7474.exe` | — | — | PE32 x86 (`0x14c`) | arquitetura não suportada |
| `officedeploymenttool_20228-20124.exe` | — | — | PE32 x86 (`0x14c`) | arquitetura não suportada |

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

## `winrar-x64-723.exe` (WinRAR x64 7.23)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `winrar-x64-723.exe` |
| Formato | PE32+ GUI x86-64, 8 seções |
| SHA-256 | `f435b24d4c2c5342c4f7c0143ef358f0f425b7b8a0972dd34d9dcf94789e9c4d` |
| Imports estáticos | 156 em 3 DLLs |
| Resolvidos pelo runtime | 97 |
| Ausentes | 59 |
| Mecanismo adicional | `delay-import` ainda não é suportado pelo loader |
| Resultado do `--report` | `unsupported`; `execution: not-attempted` |
| Fonte | análise local de 2026-08-23 |

O primeiro comando **Executar** no launcher terminou com exit code `5`; como o
loader ainda rejeita `delay-import`, o entry point não foi alcançado. O relatório
estático identifica a tabela normal de imports, mas a lista de símbolos da
tabela de delay imports ainda não é exposta pelo runtime e precisa de suporte
ao parser/relatório antes de poder ser inventariada.

### Lacunas por módulo e mecanismo

| DLL/mecanismo | APIs/ordinais ausentes |
|---|---:|
| `KERNEL32.dll` | 57 |
| `OLEAUT32.dll` | 2 |
| `gdiplus.dll` | 0/8 |
| `delay-import` | tabela presente; mecanismo não suportado |

### Imports estáticos ausentes

#### `KERNEL32.dll` (57)

```text
CreateHardLinkW
DeviceIoControl
GetLongPathNameW
GetShortPathNameW
GetFileType
SetFileAttributesW
FoldStringW
GetModuleFileNameW
SetCurrentDirectoryW
ExpandEnvironmentStringsW
SetThreadExecutionState
AllocConsole
AttachConsole
WriteConsoleW
FreeConsole
GetSystemDirectoryW
GetProcessAffinityMask
SetThreadPriority
SystemTimeToTzSpecificLocalTime
GetCPInfo
IsDBCSLeadByte
GlobalAlloc
GlobalLock
GlobalUnlock
GlobalFree
GetDateFormatW
GetTimeFormatW
GetLocaleInfoW
GetNumberFormatW
SetEnvironmentVariableW
GetTickCount
GetStringTypeW
SetStdHandle
LCMapStringW
InitializeCriticalSectionEx
RtlCaptureContext
RtlLookupFunctionEntry
RtlVirtualUnwind
UnhandledExceptionFilter
IsProcessorFeaturePresent
IsDebuggerPresent
GetStartupInfoW
InitializeSListHead
RtlUnwindEx
RtlPcToFileHeader
EncodePointer
InitializeCriticalSectionAndSpinCount
FindFirstFileExW
IsValidCodePage
GetACP
GetOEMCP
GetEnvironmentStringsW
FreeEnvironmentStringsW
FlsAlloc
FlsGetValue
FlsSetValue
FlsFree
```

#### `OLEAUT32.dll` (2)

```text
ordinal(2)
ordinal(6)
```

Os dois ordinais pertencem a uma DLL ainda não registrada. Eles devem ser
identificados contra a ABI compatível antes de qualquer implementação.

### Próxima investigação

Esta amostra reforça três capacidades que podem beneficiar outros aplicativos:

1. Leitura, resolução e diagnóstico detalhado de `delay-import`.
2. Unwinding/SEH x64 (`Rtl*`, `UnhandledExceptionFilter`) e o caminho de
   exceções associado.
3. Locale/console, FLS e operações de arquivo/ambiente, avaliadas junto com
   outros alvos para evitar implementação exclusiva para o WinRAR.

## `Creative_Cloud_Set-Up_7474.exe` (Adobe Creative Cloud Set-Up 7474)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `Creative_Cloud_Set-Up_7474.exe` |
| Formato | PE32 GUI Intel x86 (`IMAGE_FILE_MACHINE_I386`, `0x14c`), 3 seções |
| Empacotamento observado | UPX |
| SHA-256 | `8f994e20bea58bbf8498d1c866b35dd9d6a6bea5ea31c9155fe70396dd0b7ed5` |
| Resultado | rejeitado no parser: `unsupported-architecture` (exit code `5`) |
| Fonte | trace local de 2026-08-23 |

Este arquivo foi rejeitado antes do parsing de imports, mapeamento ou execução;
portanto não há lista de APIs faltantes para ele ainda.

### Requisito bloqueador

O suporte a esta amostra requer uma nova capacidade arquitetural, e não apenas
novas APIs Win32:

```text
executar PE32 x86 em hospedeiro Linux x86-64
→ processo/loader 32-bit compatível e fronteiras de ABI x86
   ou uma camada WOW64/emulação de CPU definida e testada
```

PE32/x86, WOW64 e emulação de CPU estão fora do alvo atual, que é exclusivamente
PE32+ x86-64 em Linux x86-64. Qualquer promoção desse requisito exige uma fase
própria, contrato de ABI e testes de loader; o empacotamento UPX só pode ser
avaliado depois que a questão de arquitetura estiver resolvida.

## `officedeploymenttool_20228-20124.exe` (Office Deployment Tool)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `officedeploymenttool_20228-20124.exe` |
| Formato | PE32 GUI Intel x86 (`IMAGE_FILE_MACHINE_I386`, `0x14c`), 5 seções |
| SHA-256 | `92a3cbd56191533e36bde1c6e4640883c900c00d1b5d43a974c870893681e0d4` |
| Resultado | rejeitado no parser: `unsupported-architecture` (exit code `5`) |
| Fonte | trace local de 2026-08-23 |

Este executável também foi rejeitado antes da análise de imports. Ele confirma
que instaladores distribuídos em PE32/x86 são uma categoria recorrente do
portfólio, não uma lacuna particular do Adobe Creative Cloud.

O requisito continua sendo suporte deliberado a PE32/x86 em Linux x86-64 —
processo/loader 32-bit com fronteiras de ABI adequadas, ou uma estratégia
WOW64/emulação definida — antes de qualquer implementação de API específica do
Office Deployment Tool.
