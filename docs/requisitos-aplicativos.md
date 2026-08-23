# Registro unificado de requisitos de aplicativos

Este é o arquivo canônico para os achados do portfólio da Fase 13. Cada
aplicativo analisado ganha uma seção aqui; o conjunto serve para comparar
lacunas, contar recorrências e priorizar capacidades que beneficiem mais de uma
classe de uso.

Um registro deve conter, no mínimo:

- nome, versão quando conhecida, formato/arquitetura e SHA-256 da amostra;
- total de imports, resolvidos e ausentes no momento da análise;
- lista dos imports estáticos ausentes, agrupada por DLL;
- tipo de pacote e requisito de descoberta/extração, quando a amostra não for
  um executável PE direto;
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
| `Affinity x64.msix` | — | — | pacote MSIX; executável interno não localizado | formato não suportado |
| `CapCut_7677236283084898320_installer.exe` | — | — | PE32 x86 (`0x14c`) | arquitetura não suportada |
| `EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe` | — | — | PE32 x86 (`0x14c`) + Mono/.NET | arquitetura/formato não suportados |
| `lghub_installer.exe` | 67/114 | 47 | — | `unsupported` |
| `Rockstar-Games-Launcher.exe` | 109/205 | 96 | `delay-import` | `unsupported` |

## Recorrências observadas

| Capacidade | Amostras que a evidenciam | Situação |
|---|---|---|
| PE32/x86 | Creative Cloud, Office Deployment Tool, CapCut, Epic | fora do alvo atual |
| Unwinding/SEH x64 | Roblox, WinRAR, Logitech G HUB, Rockstar | pendente |
| Locale, code pages, FLS e ambiente | WinRAR, Logitech G HUB, Rockstar | pendente |
| Segurança, identidade e ACLs | Roblox, Logitech G HUB | pendente |
| Pacote MSIX/AppX | Affinity | pendente |
| `delay-import` | WinRAR, Rockstar | pendente |
| Automação OLE | WinRAR, Rockstar | pendente |
| HTTP WinINet | Rockstar | pendente |

## Prioridade ativa — instaladores PE32+ x86-64

Os relatórios já são suficientes para priorizar instaladores como a primeira
classe da Fase 13. O objetivo inicial não é abrir todos os instaladores atuais;
é provar de ponta a ponta, com uma amostra reproduzível, que o runtime instala
em um prefixo exclusivo, localiza/cadastra o executável instalado e o relança
no mesmo ambiente.

Ordem de trabalho:

1. Prefixo por aplicativo, herdado por processos-filhos e salvo no catálogo.
2. Leitura, relatório e resolução de `delay-import`.
3. SEH/unwinding x64, locale/FLS/ambiente e os contratos de arquivo/processo
   recorrentes nos instaladores x64.
4. Um instalador PE32+ x86-64 de referência com fontes ou distribuição
   autorizada, compilado/armazenado de forma reproduzível e coberto por CTest.
5. Segurança/ACL, rede HTTP, automação OLE e controles somente quando o
   portfólio mostrar que são necessários para mais de um alvo.

Os instaladores PE32/x86, assemblies .NET/Mono e pacotes MSIX/AppX continuam
catalogados, mas pertencem a trilhas posteriores: cada um exige uma capacidade
de base diferente da instalação nativa PE32+ x86-64.

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

## `Affinity x64.msix` (pacote MSIX)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `Affinity x64.msix` |
| Contêiner observado | arquivo ZIP (deflate; requer extração compatível com ZIP 4.5+) |
| SHA-256 | `d3baa74d30b7b41655651e6ea58a505a1bafeb33ec7576d52e625c147bae164c` |
| Resultado atual | não selecionável como executável e não analisável pelo loader PE direto |
| Fonte | inspeção local de 2026-08-23 |

MSIX/AppX é um pacote de aplicativo, não um PE. Antes de o `--report` poder
listar imports, o runtime precisa localizar o executável definido pelo manifesto
do pacote. Portanto esta amostra amplia o portfólio para um formato de
distribuição, sem ainda afirmar nada sobre as APIs usadas pelo Affinity.

### Capacidade necessária: descoberta e preparação de pacotes MSIX

1. Reconhecer `.msix` e `.appx` no launcher/CLI como pacotes, diferenciando-os
   de um `.exe` PE direto.
2. Validar a estrutura do ZIP e limitar tamanho, número de entradas e caminhos
   antes da extração; nenhuma entrada pode escapar do diretório de destino.
3. Ler `AppxManifest.xml`, enumerar as aplicações declaradas e resolver o
   executável de cada uma dentro do pacote.
4. Extrair para o prefixo próprio do aplicativo e registrar o executável,
   diretório de trabalho e metadados no catálogo.
5. Só então executar `--report` no PE interno e registrar imports, arquitetura,
   dependências de framework e resultado de execução.

Validação estrutural do pacote é indispensável para tratar a entrada como dado
hostil. Verificação de assinatura, políticas de confiança e sandbox são
capacidades de segurança separadas e permanecem fora deste marco.

## `CapCut_7677236283084898320_installer.exe` (CapCut installer)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `CapCut_7677236283084898320_installer.exe` |
| Formato | PE32 GUI Intel x86 (`IMAGE_FILE_MACHINE_I386`, `0x14c`), 5 seções |
| Empacotamento observado | instalador autoextraível Nullsoft/NSIS |
| SHA-256 | `69dbc6f939bf4ac63a90dc56e1e9600b4d4284848a99c4190423c3f856c5961e` |
| Resultado | rejeitado no parser: `unsupported-architecture` (exit code `5`) |
| Fonte | trace local de 2026-08-23 |

Assim como o Creative Cloud e o Office Deployment Tool, este instalador foi
rejeitado antes de imports, mapeamento ou execução. Ele acrescenta o formato
NSIS à amostra, mas o requisito primário continua sendo suporte a PE32/x86 em
Linux x86-64. A interpretação de um instalador NSIS só pode ser investigada
depois que a arquitetura x86 estiver definida e validada.

## `EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe` (Epic Games Launcher installer)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe` |
| Formato | PE32 GUI Intel x86 (`IMAGE_FILE_MACHINE_I386`, `0x14c`), 3 seções |
| Runtime observado | assembly Mono/.NET |
| SHA-256 | `7bda7fbb3eea3ffdced17b5679c057943464a6ecc5e5274968b728feae470b7b` |
| Resultado | rejeitado no parser: `unsupported-architecture` (exit code `5`) |
| Fonte | trace e inspeção local de 2026-08-23 |

O parser para na arquitetura x86, logo imports nativos não foram analisados.
Mesmo após uma futura camada PE32/x86, esta amostra exigirá uma decisão de
escopo separada para executar assemblies gerenciados: hospedagem de CLR/Mono,
carregamento de assemblies, interoperabilidade e teste de versão. .NET/Mono
continuam fora do alvo atual; portanto, este instalador evidencia duas lacunas
independentes, não uma API Win32 específica faltante.

## `lghub_installer.exe` (Logitech G HUB installer)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `lghub_installer.exe` |
| Formato | PE32+ GUI x86-64, 8 seções |
| SHA-256 | `4b2f9903b27c8434afcd52fe65845632fcae47cc50432fb6b3b1637144e811e1` |
| Imports estáticos | 114 em 3 DLLs |
| Resolvidos pelo runtime | 67 |
| Ausentes | 47 |
| Resultado do `--report` | `unsupported`; `execution: not-attempted` |
| Fonte | análise local de 2026-08-23 |

O primeiro comando **Executar** terminou com exit code `5` durante a resolução
de imports, antes do entry point. `COMCTL32!InitCommonControlsEx` já resolve;
as lacunas restantes estão em `ADVAPI32` e `KERNEL32`.

### Lacunas por módulo

| DLL | APIs ausentes |
|---|---:|
| `ADVAPI32.dll` | 5 |
| `KERNEL32.dll` | 42 |
| `COMCTL32.dll` | 0/1 |

### Imports estáticos ausentes

#### `ADVAPI32.dll` (5)

```text
GetNamedSecurityInfoW
OpenProcessToken
GetTokenInformation
SetEntriesInAclW
SetNamedSecurityInfoW
```

Essas APIs pedem uma camada de descritores de segurança, token de processo e
ACLs com semântica própria; retornar sucesso sem aplicar a ACL não é suficiente
para um instalador.

#### `KERNEL32.dll` (42)

```text
GetSystemDirectoryW
RtlCaptureContext
RtlLookupFunctionEntry
RtlVirtualUnwind
IsDebuggerPresent
UnhandledExceptionFilter
IsProcessorFeaturePresent
GetFileType
GetStartupInfoW
FlsAlloc
FlsGetValue
FlsSetValue
FlsFree
InitializeCriticalSectionAndSpinCount
LCMapStringW
GetLocaleInfoW
IsValidLocale
EnumSystemLocalesW
IsValidCodePage
GetACP
GetOEMCP
GetCPInfo
GetStringTypeW
SetStdHandle
GetModuleFileNameW
ReadConsoleW
WriteConsoleW
RtlPcToFileHeader
RtlUnwindEx
RtlUnwind
EncodePointer
InitializeSListHead
FormatMessageA
GetLocaleInfoEx
FindFirstFileExW
SetFileInformationByHandle
AreFileApisANSI
InitializeCriticalSectionEx
DecodePointer
LCMapStringEx
GetEnvironmentStringsW
FreeEnvironmentStringsW
```

### Próxima investigação

Este caso confirma que unwinding/SEH x64 e o grupo locale/FLS/ambiente são
capacidades compartilhadas por aplicativos x64 grandes. A camada de ACLs deve
ser validada com uma fixture de segurança genérica e outro alvo que também a
use, antes de ser considerada suporte ao instalador do Logitech.

## `Rockstar-Games-Launcher.exe` (Rockstar Games Launcher)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `Rockstar-Games-Launcher.exe` |
| Formato | PE32+ GUI x86-64, 6 seções |
| SHA-256 | `c70131cb0427d146c9489297822e99ad87d4d5e141fd999d19f00975ab1a31f2` |
| Imports estáticos | 205 em 5 DLLs |
| Resolvidos pelo runtime | 109 |
| Ausentes | 96 |
| Mecanismo adicional | `delay-import` ainda não é suportado pelo loader |
| Resultado do `--report` | `unsupported`; `execution: not-attempted` |
| Fonte | análise local de 2026-08-23 |

O comando **Executar** terminou com exit code `5` antes do entry point. Além
das lacunas em `KERNEL32`, o aplicativo requer controles comuns por ordinal,
automação OLE, diálogo de impressão e uma camada HTTP WinINet.

### Lacunas por módulo e mecanismo

| DLL/mecanismo | APIs/ordinais ausentes |
|---|---:|
| `KERNEL32.dll` | 75 |
| `COMDLG32.dll` | 1 |
| `OLEAUT32.dll` | 7 |
| `COMCTL32.dll` | 2 |
| `WININET.dll` | 11 |
| `delay-import` | tabela presente; mecanismo não suportado |

### Imports estáticos ausentes

#### `KERNEL32.dll` (75)

```text
DecodePointer
InitializeCriticalSectionEx
GetModuleFileNameW
GlobalAlloc
GlobalLock
LocalAlloc
SetDllDirectoryW
K32GetModuleFileNameExW
SetThreadLocale
SetThreadUILanguage
UnregisterWaitEx
FormatMessageA
RegisterWaitForSingleObject
SetSearchPathMode
GetUserDefaultUILanguage
GlobalUnlock
RtlUnwind
SetEnvironmentVariableW
FreeEnvironmentStringsW
GetEnvironmentStringsW
GetOEMCP
GetACP
IsValidCodePage
FindFirstFileExW
GetTimeZoneInformation
SetStdHandle
GetLogicalDrives
GetPhysicallyInstalledSystemMemory
GetVolumePathNameA
QueryFullProcessImageNameW
SetFileAttributesW
RtlCaptureContext
GetProcessId
VirtualQueryEx
FileTimeToLocalFileTime
IsDebuggerPresent
OutputDebugStringA
OutputDebugStringW
SetNamedPipeHandleState
TransactNamedPipe
WaitNamedPipeW
RtlLookupFunctionEntry
RtlVirtualUnwind
UnhandledExceptionFilter
GetStartupInfoW
IsProcessorFeaturePresent
InitializeSListHead
GetStringTypeW
WaitForSingleObjectEx
GetExitCodeThread
TryAcquireSRWLockExclusive
EncodePointer
LCMapStringEx
GetCPInfo
RtlUnwindEx
RtlPcToFileHeader
InterlockedPushEntrySList
InitializeCriticalSectionAndSpinCount
FreeLibraryAndExitThread
GetFileType
PeekNamedPipe
SystemTimeToTzSpecificLocalTime
TzSpecificLocalTimeToSystemTime
WriteConsoleW
FlsAlloc
FlsGetValue
FlsSetValue
FlsFree
GetDateFormatW
GetTimeFormatW
LCMapStringW
GetLocaleInfoW
IsValidLocale
EnumSystemLocalesW
ReadConsoleW
```

#### `COMDLG32.dll` (1)

```text
PrintDlgW
```

#### `OLEAUT32.dll` (7)

```text
ordinal(201)
ordinal(7)
ordinal(2)
ordinal(6)
ordinal(8)
ordinal(9)
ordinal(200)
```

#### `COMCTL32.dll` (2)

```text
ordinal(410)
ordinal(413)
```

Os ordinais de `OLEAUT32` e `COMCTL32` precisam ser identificados contra uma
ABI/versão definida antes de se declararem exportações compatíveis.

#### `WININET.dll` (11)

```text
InternetReadFile
InternetCrackUrlW
InternetCloseHandle
InternetConnectW
InternetQueryDataAvailable
InternetSetOptionW
HttpOpenRequestW
HttpAddRequestHeadersW
HttpSendRequestW
HttpQueryInfoW
InternetOpenW
```

WinINet é uma camada HTTP de alto nível, diferente do subconjunto WS2_32 de
loopback já existente. Seu suporte exige contratos de URL, proxy, TLS, handles,
erros e I/O; nenhuma requisição de Internet será considerada suporte sem testes
determinísticos locais.

### Próxima investigação

Este caso aumenta a prioridade de `delay-import`, SEH x64 e locale/FLS. Para
WinINet, OLE automation, controles comuns e impressão, a primeira entrega deve
ser uma fixture genérica e reprodutível antes de qualquer tentativa de executar
o Rockstar Launcher.
