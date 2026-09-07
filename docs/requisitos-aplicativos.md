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

No índice, `supported` só pode ser usado quando houver um fluxo executado e
registrado; `imports-resolved` representa apenas análise estática; e
`execution-failed` representa uma tentativa que terminou com falha controlada.
O nível funcional detalhado fica no catálogo.

## Índice de análises

| Aplicativo | Imports resolvidos | Imports ausentes | Bloqueio adicional | Estado |
|---|---:|---:|---|---|
| `RobloxPlayerInstaller.exe` | 430/430 | 0 | execução comercial termina em `RBXCRASH FatalRuntimeError Worker,28` / exit `3` | `execution-failed` |
| `winrar-x64-723.exe` | 251/251 | 0 | somente o smoke `sfxcmd`/ambiente foi validado; GUI e uso diário não foram declarados | `supported` no fluxo restrito |
| `Creative_Cloud_Set-Up_7474.exe` | — | — | PE32 x86 (`0x14c`) | arquitetura não suportada |
| `officedeploymenttool_20228-20124.exe` | — | — | PE32 x86 (`0x14c`) | arquitetura não suportada |
| `Affinity x64.msix` | — | — | pacote MSIX reconhecido; executável interno `App/Affinity.exe` é Mono/.NET | pacote reconhecido, mas formato de execução fora do escopo |
| `CapCut_7677236283084898320_installer.exe` | — | — | PE32 x86 (`0x14c`) | arquitetura não suportada |
| `EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe` | — | — | PE32 x86 (`0x14c`) + Mono/.NET | arquitetura/formato não suportados |
| `lghub_installer.exe` | 114/114 | 0 | execução expira em prefixo temporário (`GuestTimeout 72`) | `execution-failed` |
| `Rockstar-Games-Launcher.exe` | 338/338 | 0 | execução termina com exit `3`; fluxo principal não validado | `execution-failed` |

O índice usa o estado de execução atual, não apenas o resultado do `--report`.
`supported` no WinRAR significa somente o fluxo restrito `sfxcmd`/ambiente
registrado na matriz; não significa uso diário da GUI. Para o restante, imports
resolvidos e execução falha permanecem dimensões separadas.

As medições intermediárias abaixo preservam a evolução histórica. Os valores
atuais são os do índice e devem ser usados para novas decisões do roadmap.

## Snapshot canônico de metadados

O snapshot abaixo é a referência mínima para novas análises. `não registrado`
é deliberado: significa que a amostra ou a ferramenta histórica não deixou o
dado disponível no repositório e que ele precisa ser coletado antes de uma nova
declaração. Este snapshot de arquivos foi coletado em 2026-09-04; a ferramenta
registrada é `tradutorlinux 0.0.0-dev`. A execução usa o runner documentado com
trace e timeout quando indicado.

| Amostra | Versão | Formato/arquitetura | SHA-256 | Coleta | Ferramenta/versão | `--report` | Execução observada |
|---|---|---|---|---|---|---|---|
| `7z_x64.exe` | 24.08 | PE32+ x86-64 | `707f415d7d581edd9bce99a0429ad4629d3be0316c329e8b9ebd576f7ab50b71` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 133/133 | `ExitProcess 0`, banner 7-Zip |
| `7zFM_x64.exe` | 24.08 | PE32+ x86-64 | `028cf2158df45889e9a565c9ce3c6648fb05c286b97f39c33317163e35d6f6be` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev; smoke `seven_zip_smoke` | 298/298 | smoke externo seleciona `input.txt`, conclui `Copy` (`546`) dentro da raiz e encerra com exit `0`; demais operações não concluídas |
| `7z.dll` | 24.08 | DLL PE32+ x86-64 | `e79ddfb6319dbf9bac6382035d23597dad979db5e71a605d81a61ee817c1e812` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 86/86 | não executada como aplicação |
| `putty_x64.exe` | Release 0.85 | PE32+ x86-64 | `d01fdb5aae8f112526040a39b0bfb9e27d813003178645e65f8d1cfdb2a26c87` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 348/348 | `ExitProcess 1`, sem argumentos |
| `winrar-x64-723.exe` | 7.23 | PE32+ x86-64 | `f435b24d4c2c5342c4f7c0143ef358f0f425b7b8a0972dd34d9dcf94789e9c4d` | 2026-08-31 | `tradutorlinux --report` / 0.0.0-dev | 251/251 | smoke `sfxcmd`/ambiente, exit `0` |
| `Rufus_x64.exe` | 4.5.2180 | PE32+ x86-64 | `c6e6cdba209f899e5087f1a1a4babc759414b4a687b60ba4bce62b6b37e8e82b` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 14/14 | exit `56832`; fluxo de uso não validado |
| `HWiNFO64.exe` | 8.52-6060 | PE32+ x86-64 | `39292da56747eaed8b6025ba5e231e096e5792708bf4671308c2e700ed059cc0` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 28/28 | exit `44544`; `OpenPrinterW` limitado |
| `RobloxPlayerInstaller.exe` | não registrada | PE32+ x86-64 | `d156faf0c712d4ce26d95a596ad9b1dfc813021b5c422c93887b2522d8b01a59` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 430/430 | `RBXCRASH` Worker/RSL, exit `3` |
| `Rockstar-Games-Launcher.exe` | não registrada | PE32+ x86-64 | `c70131cb0427d146c9489297822e99ad87d4d5e141fd999d19f00975ab1a31f2` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 338/338 | exit `3`; fluxo principal não validado |
| `lghub_installer.exe` | 2026.4.919028 | PE32+ x86-64 | `4b2f9903b27c8434afcd52fe65845632fcae47cc50432fb6b3b1637144e811e1` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 114/114 | `GuestTimeout 72` durante a inicialização |
| `notepad++.exe` | 8.6.9 | PE32+ x86-64 | `0cac294da853593a8136f1438b6d31da915309034027934a3ec893c2b9f2456b` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 584/584 | `GuestTimeout 72` sem `Xvfb` |
| `RTSSHooks64.dll` | não registrada | DLL PE32+ x86-64 | `68c496dea7ded5b8766092f0159f4e9ba3947df0829f594eb6981d2b06352ad5` | 2026-09-04 | `tradutorlinux --report` / 0.0.0-dev | 256/256 | não executada como aplicação |
| `Affinity x64.msix` | não registrada | MSIX/AppX | `d3baa74d30b7b41655651e6ea58a505a1bafeb33ec7576d52e625c147bae164c` | 2026-09-04 | `tradutorlinux 0.0.0-dev` | pacote reconhecido; `App/Affinity.exe` é Mono/.NET e não é alvo funcional | não instalado nem executado |

Para as entradas com data ou versão ainda não registrada, a próxima coleta deve
preencher o campo antes de alterar o estado. O hash deve ser calculado sobre o
arquivo exato usado na execução; não se deve reutilizar o hash de outra versão.

## Recorrências observadas

| Capacidade | Amostras que a evidenciam | Situação |
|---|---|---|
| PE32/x86 | Creative Cloud, Office Deployment Tool, CapCut, Epic | fora do alvo atual |
| Unwinding/SEH x64 | Roblox, WinRAR, Logitech G HUB, Rockstar | núcleo `.pdata`/`.xdata` V1/V2, `Rtl*` e SEH explícito suportados; exceções C++, `__finally` e epílogo V2 pendentes |
| Locale, code pages, FLS e ambiente | WinRAR, Logitech G HUB, Rockstar | núcleo determinístico `en-US`/1252/437, ambiente por processo e FLS por thread; validação, enumeração estática, `CT_CTYPE1` e data/hora en-US suportados; UI locale, mutação por thread e fibras reais pendentes |
| Contexto de processo e console | WinRAR, Logitech G HUB, Rockstar | startup W, handles padrão, console UTF-16, diretório lógico, recursos AMD64, encode/decode e SList vazia suportados; alocação de console, herança explícita e operações interlocked pendentes |
| Segurança, identidade e ACLs | WinRAR, Logitech G HUB, Rockstar | token/SID virtual e DACL persistente para arquivos existentes em `C:\` por prefixo; sem SACL, privilégios, `AccessCheck` ou permissões Linux |
| Alocação Global/Local | WinRAR, Rockstar + fixture de protocolo | `GlobalAlloc`/`GlobalLock`/`GlobalUnlock`/`GlobalFree` e `LocalAlloc`/`LocalFree` com flags `MOVEABLE`/`ZEROINIT`, tabela lateral e rejeição de handles arbitrários |
| Pacote MSIX/AppX | Affinity + fixture `native-fixture.msix` | instalação suportada somente para pacote com PE32+ x86-64 nativo; `integration_msix_install` valida extração, catálogo e `app run`; Affinity continua fora por Mono/.NET |
| `delay-import` | WinRAR, Rockstar | suportado para descritores RVA (`grAttrs=0x1`), com resolução antecipada |
| Automação OLE | WinRAR, Rockstar | `CreateStreamOnHGlobal` entregue como stream em memória, incluindo backing store válido de `GlobalAlloc`, em `tl_stream.exe`; `OLEAUT32`/`IDispatch` pendentes |
| HTTP WinINet | Rockstar + fixture de protocolo | subconjunto HTTPS direto de loopback entregue em `tl_wininet.exe`; sem execução do Rockstar |
| Certificados/WinTrust | Rockstar + fixtures de protocolo | `CertGetNameStringW` extrai nomes de blob DER em `tl_crypt32.exe`; cadeia explícita em `tl_trust.exe`; `WTHelper*` percorre estado/signer/folha-raiz em `tl_wthelper.exe`; `CertOpenStore` cobre somente loja em memória e wrappers de nome; Authenticode e trust store Windows pendentes |

## Prioridade ativa — instaladores PE32+ x86-64

Os relatórios já são suficientes para priorizar instaladores como a primeira
classe da Fase 13. O objetivo inicial não é abrir todos os instaladores atuais;
é provar de ponta a ponta, com uma amostra reproduzível, que o runtime instala
em um prefixo exclusivo, localiza/cadastra o executável instalado e o relança
no mesmo ambiente.

Ordem de trabalho:

1. [x] Prefixo por aplicativo, herdado por processos-filhos e salvo no catálogo.
   A fixture `tl_install_setup.exe` prova instalação, descoberta/cadastro e
   relançamento no mesmo ambiente; o isolamento é funcional, não sandbox.
2. [x] Um instalador PE32+ x86-64 de referência com fontes reproduzíveis,
   coberto por CTest, incluindo seleção explícita e ausência de candidatos.
3. [x] Leitura, relatório e resolução antecipada de `delay-import` RVA, com
   fixture `tl_delay_import.exe` e diagnóstico por símbolo.
4. [x] Núcleo de unwinding x64 V1/V2 com `tl_unwind.exe` e
   `tl_unwind_v2.exe`: `.pdata`/`.xdata`, contexto, epílogos normalizados e
   `RtlCaptureContext`/`RtlLookupFunctionEntry`/`RtlVirtualUnwind`/
   `RtlPcToFileHeader`; a etapa seguinte promoveu o despacho explícito, mas
   não interpreta epílogos V2.
5. [x] Despacho SEH explícito V1/V2 fora de epílogos, com `tl_seh.exe` e
   `tl_seh_v2.exe`.
6. [x] Locale/FLS/ambiente determinísticos, comprovados por
   `tl_locale_env_fls.exe`.
7. [x] **Fase 13.7 — locale determinístico ampliado:** `IsValidCodePage`,
   `IsValidLocale`, `GetLocaleInfoEx`, `EnumSystemLocalesW`,
   `GetStringTypeW`, `GetDateFormatW` e `GetTimeFormatW`, primeiro núcleo a
   reduzir lacunas de WinRAR, Logitech G HUB e Rockstar simultaneamente,
   comprovado por `tl_locale_extended.exe`.
8. [x] **Fase 13.8 — contexto de processo e console Win32**, comprovada por
   `tl_process_console.exe`.
9. [x] **Fase 13.9 — enumeração e metadados de arquivos x64**, comprovada
   por `tl_file_metadata.exe`.
10. [x] **Fase 13.10 — identidade e DACL virtual por prefixo**, comprovada
    por `tl_security.exe`. Controles GUI e, em subfases independentes,
    automação, HTTP e confiança seguem na ordem e com os critérios registrados
    em `feitos/ROADMAP.md`.
11. [x] **B8 — instalação de pacote MSIX/AppX nativo**, comprovada por
    `native-fixture.msix`: manifesto, extração segura, seleção PE32+ x86-64,
    cadastro e execução no prefixo. Pacotes .NET/Mono e bundles permanecem
    fora do escopo.

Os instaladores PE32/x86 e assemblies .NET/Mono continuam catalogados e
pertencem a trilhas posteriores. Pacotes MSIX/AppX nativos avançaram nesta
etapa, mas somente quando o executável declarado é PE32+ x86-64; bundles,
assinatura Authenticode e demais arquiteturas ainda exigem capacidade própria.

## Inventário Worker/RSL (2026-09-04)

A busca em `Aplicativos_Windows_Populares/` não encontrou um executável
comercial com `worker` ou `rsl` no nome. A única amostra correspondente nesta
retomada é a fixture própria `tl_worker_rsl.exe`, localizada no diretório de
saída do build. Ela cobre somente o subconjunto documentado e termina com exit
`77` quando o host não possui interface IPv4; isso não é evidência para mudar o
estado do Worker/RSL comercial.

## `RobloxPlayerInstaller.exe`

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `RobloxPlayerInstaller.exe` |
| Formato | PE32+ GUI x86-64, 7 seções |
| SHA-256 | `d156faf0c712d4ce26d95a596ad9b1dfc813021b5c422c93887b2522d8b01a59` |
| Imports estáticos | 430 em 17 DLLs |
| Resolvidos pelo runtime | 430/430, reanálise de 2026-09-04 |
| Ausentes | 0 na leitura atual |
| Resultado do `--report` | `supported`, exit `0`; resolução estática concluída |
| Primeiro teste de execução | `RBXCRASH FatalRuntimeError Worker,28`, `ExitProcess 3`; fluxo comercial não concluído |
| Fonte | análise local de 2026-09-04; hash fixado acima |

O Roblox é um benchmark de cobertura do portfólio, não um alvo exclusivo e nem
uma autorização para stubs específicos para ele. A leitura atual resolve todos
os imports estáticos, mas a execução comercial termina de forma controlada no
Worker/RSL (`RBXCRASH`, exit `3`). TLS genérico e `tl_worker_rsl.exe` são
evidências reutilizáveis, não equivalem à execução bem-sucedida do instalador.
As observações de `UWOP_SET_FPREG` e os totais anteriores permanecem abaixo
como histórico de diagnóstico, não como estado atual.

### Lacunas históricas por módulo

As contagens a seguir são do snapshot de 2026-08-23 e não representam imports
ausentes na leitura atual de 430/430.

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

### Imports estáticos ausentes (histórico)

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
| Delay imports | 95 em 7 DLLs |
| Resolvidos pelo runtime | 251/251 (156 estáticos + 95 atrasados), reanálise de 2026-08-31 |
| Ausentes | 0, reanálise de 2026-08-31 |
| Mecanismo adicional | `delay-import` RVA resolvido antecipadamente; `.pdata`: 1316 funções (1313 V1, 3 V2), 3 epílogos, 2 `SET_FPREG` estendidos, 263 handlers e 14 cadeias |
| Resultado do `--report` | `supported`, 251/251; execução restrita `sfxcmd`/ambiente concluída com exit `0` |
| Fonte | análise local de 2026-08-31 |

O `--report` atual classifica os 251 imports e o smoke `sfxcmd`/ambiente encerra
com exit `0`. Sob Xvfb, o smoke específico de GUI localiza a janela
`WinRAR self-extracting archive` e confirma o cancelamento por
`WM_DELETE_WINDOW`, com `EndDialog(result=2)` e exit `3` nos builds Rust ON e
C++ OFF. Isso valida somente os fluxos restritos de análise, SFX sem GUI e
cancelamento controlado; a extração acionada por `IDOK`, compactação interativa
e uso diário continuam fora da declaração de suporte.

As listas de lacunas abaixo são mantidas como histórico das reanálises de
2026-08-26. Elas não devem ser usadas para calcular o estado atual.

### Lacunas históricas por módulo e mecanismo

| DLL/mecanismo | APIs/ordinais ausentes |
|---|---:|
| `KERNEL32.dll` | 16 |
| `OLEAUT32.dll` | 2 |
| `gdiplus.dll` | 0/8 |
| delay `SHLWAPI.dll` | 1 |
| delay `USER32.dll` | 11 |
| delay `GDI32.dll` | 3 |
| delay `ADVAPI32.dll` | 2 |
| delay `SHELL32.dll` | 6 |
| delay `ole32.dll` | 1 |

### Imports estáticos ausentes (histórico)

#### `KERNEL32.dll` (16)

```text
CreateHardLinkW
DeviceIoControl
GetLongPathNameW
GetShortPathNameW
FoldStringW
SetCurrentDirectoryW
SetThreadExecutionState
AllocConsole
AttachConsole
FreeConsole
GetProcessAffinityMask
SetThreadPriority
SystemTimeToTzSpecificLocalTime
IsDBCSLeadByte
GetNumberFormatW
GetTickCount
```

#### `OLEAUT32.dll` (2)

```text
ordinal(2)
ordinal(6)
```

Os dois ordinais pertencem a uma DLL ainda não registrada. Eles devem ser
identificados contra a ABI compatível antes de qualquer implementação.

### Imports atrasados ausentes

#### `SHLWAPI.dll` (1)

```text
SHAutoComplete
```

#### `USER32.dll` (11)

```text
SetUserObjectInformationW
GetSysColor
WaitForInputIdle
FindWindowExW
PeekMessageW
MapWindowPoints
CopyRect
CharUpperW
GetWindow
SetProcessDefaultLayout
GetClassNameW
```

#### `GDI32.dll` (3)

```text
StretchBlt
GetObjectW
CreateDIBSection
```

#### `ADVAPI32.dll` (2)

```text
LookupPrivilegeValueW
AdjustTokenPrivileges
```

#### `SHELL32.dll` (6)

```text
SHGetFileInfoW
SHGetPathFromIDListW
SHBrowseForFolderW
SHFileOperationW
SHGetMalloc
SHChangeNotify
```

#### `ole32.dll` (2)

```text
CreateStreamOnHGlobal
CLSIDFromString
```

### Próxima investigação

Esta amostra reforça capacidades que podem beneficiar outros aplicativos:

1. Contexto de processo/console, sobretudo `GetStartupInfoW`, handles padrão
   e as operações de console ainda ausentes.
2. Diálogos/controles e APIs de sistema, após o núcleo de identidade/ACL
   compartilhado já validado por fixture.

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
| Resultado atual | pacote reconhecido; não instalável no subconjunto B8 porque o executável interno é Mono/.NET |
| Fonte | inspeção local de 2026-08-23; validação estrutural em 2026-09-04 |

MSIX/AppX é um pacote de aplicativo, não um PE. O runtime agora localiza o
executável definido pelo manifesto, extrai com validação estrutural e registra
o aplicativo quando ele é um PE32+ x86-64 nativo. O Affinity continua apenas
reconhecido porque `App/Affinity.exe` é Mono/.NET, sem afirmar suporte às APIs
do aplicativo.

### Capacidade entregue e limite: pacotes MSIX nativos

1. [x] Reconhecer `.msix` e `.appx` no launcher/CLI como pacotes, diferenciando-os
   de um `.exe` PE direto.
2. [x] Validar a estrutura do ZIP e limitar tamanho, número de entradas e caminhos
   antes da extração; nenhuma entrada pode escapar do diretório de destino.
3. [x] Ler `AppxManifest.xml`, enumerar as aplicações declaradas e resolver o
   executável de cada uma dentro do pacote.
4. [x] Extrair para o prefixo próprio do aplicativo e registrar o executável,
   diretório de trabalho e metadados no catálogo.
5. [x] Só então executar `app run` no PE interno nativo e validar arquitetura;
   `--report` permanece uma inspeção estrutural do pacote e não o executa.

O teste reproduzível `integration_msix_install` empacota `tl_hello.exe`,
confirma o `--report`, instala em prefixo exclusivo, verifica a entrada do
catálogo e executa o PE extraído. A etapa não interpreta bundles, assemblies
.NET/Mono ou assinatura Authenticode.

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
| Resolvidos pelo runtime | 114/114, após a fixture `tl_k32_gap.exe` |
| Ausentes | 0 |
| Metadados adicionais | `.pdata`: 1375 funções (1371 V1, 4 V2), 4 epílogos, 4 `SET_FPREG` estendidos, 233 handlers e 6 cadeias |
| Resultado do `--report` | `supported`, `114/114`; resolução estática concluída |
| Primeiro teste de execução | Prefixo temporário, timeout de 20 s, exit `72`; stdout vazio e nenhum arquivo criado |
| Fonte | análise local de 2026-08-25 |

O `--report` atual resolve todos os imports estáticos e atrasados (`114/114`),
incluindo `InitializeCriticalSectionAndSpinCount`, `FormatMessageA`,
`AreFileApisANSI` e `InitializeCriticalSectionEx`. A execução entrou na fase de
execução do convidado, ficou bloqueada durante a inicialização e expirou pelo
limite do runner; isso não declara o instalador compatível nem prova o fluxo de
instalação. O estado correto é `execution-failed`, não `supported` como nível
funcional.

### Lacunas cobertas no incremento KERNEL32

| DLL | Lacuna histórica |
|---|---:|
| `ADVAPI32.dll` | 5, entregues na Fase 13.10 |
| `KERNEL32.dll` | 4, entregues nesta etapa |

As quatro APIs de `KERNEL32` são protegidas pela fixture `tl_k32_gap.exe`, que
valida inicialização/uso de seções críticas, flags inválidas, retorno fixo de
`AreFileApisANSI` e `FormatMessageA` com buffer insuficiente e mensagem do
sistema.

```text
InitializeCriticalSectionAndSpinCount
FormatMessageA
AreFileApisANSI
InitializeCriticalSectionEx
```

### Próxima investigação

O bloqueio deixou de ser resolução de imports e passou a ser comportamento do
entry point. A próxima análise deve localizar a espera durante a inicialização
com diagnóstico adicional ou uma fixture de instalação equivalente; não se
deve declarar suporte ao fluxo Logitech apenas porque o `--report` passou.

## `Rockstar-Games-Launcher.exe` (Rockstar Games Launcher)

### Amostra e resultado

| Campo | Valor |
|---|---|
| Arquivo | `Rockstar-Games-Launcher.exe` |
| Formato | PE32+ GUI x86-64, 6 seções |
| SHA-256 | `c70131cb0427d146c9489297822e99ad87d4d5e141fd999d19f00975ab1a31f2` |
| Imports estáticos | 205 em 5 DLLs |
| Delay imports | 133 em 11 DLLs |
| Resolvidos pelo runtime | 338/338 (205 estáticos + 133 atrasados), reanálise de 2026-08-31 |
| Ausentes | 0, reanálise de 2026-08-31 |
| Mecanismo adicional | `delay-import` RVA resolvido antecipadamente; `.pdata`: 2663 funções (2661 V1, 2 V2), 2 epílogos, 6 `SET_FPREG` estendidos, 356 handlers e 584 cadeias |
| Resultado do `--report` | `supported`, 338/338; execução termina com exit `3` |
| Fonte | análise local de 2026-08-31 |

O `--report` atual resolve os 338 imports, mas a execução termina com exit `3`;
isso não valida o fluxo principal do launcher. As fixtures de memória,
certificados, WTHelper, WinINet e OLE continuam sendo evidências de capacidades
reutilizáveis, não suporte funcional do Rockstar.

As listas de lacunas abaixo são mantidas como histórico das reanálises de
2026-08-26. Elas não devem ser usadas para calcular o estado atual.

### Lacunas históricas por módulo e mecanismo

| DLL/mecanismo | APIs/ordinais ausentes |
|---|---:|
| `KERNEL32.dll` | 29 |
| `COMDLG32.dll` | 1 |
| `OLEAUT32.dll` | 7 |
| `COMCTL32.dll` | 2 |
| delay `USER32.dll` | 19 |
| delay `GDI32.dll` | 6 |
| delay `ADVAPI32.dll` | 5 |
| delay `SHELL32.dll` | 2 |
| delay `SHLWAPI.dll` | 2 |

### Imports estáticos ausentes (histórico)

#### `KERNEL32.dll` (29)

```text
SetDllDirectoryW
K32GetModuleFileNameExW
SetThreadLocale
SetThreadUILanguage
UnregisterWaitEx
RegisterWaitForSingleObject
SetSearchPathMode
GetUserDefaultUILanguage
GetTimeZoneInformation
GetLogicalDrives
GetPhysicallyInstalledSystemMemory
GetVolumePathNameA
QueryFullProcessImageNameW
GetProcessId
VirtualQueryEx
FileTimeToLocalFileTime
OutputDebugStringA
OutputDebugStringW
SetNamedPipeHandleState
TransactNamedPipe
WaitNamedPipeW
WaitForSingleObjectEx
GetExitCodeThread
TryAcquireSRWLockExclusive
InterlockedPushEntrySList
FreeLibraryAndExitThread
PeekNamedPipe
SystemTimeToTzSpecificLocalTime
TzSpecificLocalTimeToSystemTime
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

Os 11 imports de `WININET.dll` estão resolvidos pelo subconjunto validado em
`tl_wininet.exe`: HTTPS direto para `localhost`/`127.0.0.1`, CA TLS fornecida
pelo host, URL, cabeçalho, status, leitura e falha para CA não confiável. Isso
não declara o Rockstar suportado: o binário comercial não foi reexecutado e
permanecem automação, GUI, confiança e demais lacunas.

Os cinco imports atrasados de `ole32.dll`, incluindo `CreateStreamOnHGlobal`,
estão resolvidos pelas fixtures `tl_stream.exe` e `tl_com.exe`; isso não implica
suporte aos sete ordinais de `OLEAUT32` nem execução do Rockstar.

### Imports atrasados ausentes

#### `USER32.dll` (19)

```text
GetDesktopWindow
DrawTextW
ReleaseCapture
SetCapture
GetCapture
MessageBoxExW
RedrawWindow
GetFocus
EmptyClipboard
SetClipboardData
CloseClipboard
OpenClipboard
BringWindowToTop
CallWindowProcW
MonitorFromWindow
DrawIconEx
LoadImageW
PtInRect
ClientToScreen
```

#### `GDI32.dll` (6)

```text
GetTextExtentPoint32W
AbortDoc
EndPage
StartPage
EndDoc
StartDocW
```

#### `ADVAPI32.dll` (5)

```text
RegDeleteTreeW
RegEnumValueW
RegEnumKeyExW
RegDeleteKeyExW
RegDeleteKeyW
```

#### `SHELL32.dll` (2)

```text
SHBrowseForFolderW
SHGetPathFromIDListW
```

#### `SHLWAPI.dll` (2)

```text
PathStripToRootW
ordinal(176)
```

### Próxima investigação

SEH x64, locale, contexto de processo/console, enumeração de arquivos e
identidade/DACL virtual já foram entregues, mas não resolvem as dependências
restantes deste aplicativo. WinINet e o stream OLE têm fixtures genéricas
reproduzíveis; a primeira cadeia WinTrust também é coberta por `tl_trust.exe`,
e `tl_crypt32.exe` cobre somente a leitura de nomes DER. A fixture
`tl_wthelper.exe` cobre a travessia limitada de estado WinTrust. A automação
`OLEAUT32`/`IDispatch`, Authenticode/loja Windows, controles comuns
restantes e impressão ainda exigem contratos próprios antes de qualquer
tentativa de executar o Rockstar Launcher.
