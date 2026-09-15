# Roadmap atual — TradutorLinux

## Estado do documento

Este é o roadmap de trabalho atual do TradutorLinux. Ele substitui as listas
de análise como fonte de próximas etapas, mas não altera o histórico preservado
em [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md).

O escopo abaixo contém somente pendências que permaneceram após a triagem de
2026-09-12. Não são itens de compatibilidade já concluídos nem autorização
para ampliar famílias de DLL sem alvo, contrato e regressão.

## Auditoria das pendências — 2026-09-12

- **R1 é um defeito confirmado:** `MPR.dll` retorna sucesso para operações não
  realizadas e `WNetOpenEnumW` publica um handle literal que não pertence ao
  runtime. A cobertura atual reproduz esse comportamento em vez de protegê-lo.
- **R4 é um defeito de classificação confirmado:** `ExportSupport::Full` é o
  padrão e os exports MPR aparecem como `full` no relatório por omissão. A
  parte sobre `direct_export` é somente uma auditoria de regressão: endereços
  zero já terminam como `NotImpl` no resolvedor.
- **R2 não é um defeito de produção reproduzido:** o caminho de falha de
  `ARCH_SET_GS` já existe e limpa o estado; falta uma injeção determinística
  para comprovar esse caminho.
- **R3 foi uma limitação arquitetural real e foi encerrada nesta rodada:** a
  validação por `/proc/self/maps` não é atômica com o acesso posterior. A
  primitiva protegida, os consumidores comuns e os contratos especiais de
  imagem/SEH foram auditados e cobertos por regressões; o predicado advisory
  permanece apenas como suporte interno da própria primitiva.

## Regras de execução

Cada etapa deve produzir:

1. implementação mínima no runtime;
2. teste unitário ou de robustez;
3. fixture ou integração com aplicativo-alvo quando a mudança afetar uma API
   Win32 observável;
4. atualização da documentação e da matriz de compatibilidade;
5. validação reproduzível no Linux x86-64;
6. um commit próprio antes da etapa seguinte.

Uma exportação resolvida não pode ser tratada como suporte funcional. Quando a
operação ainda não existir, o runtime deve retornar uma falha controlada,
atualizar o erro correspondente e informar a limitação no diagnóstico.

## Ordem atual

### R1 — Corrigir falso sucesso de MPR.dll

**Problema:** ~~`src/runtime/dlls/net/mpr.cpp` retornava `NO_ERROR` em operações
que não eram executadas e `WNetOpenEnumW` publicava o valor constante `WNet` como
se fosse um handle válido.~~ Corrigido nesta etapa.

**Tarefas:**

- [x] inventariar quais APIs MPR.dll são consumidas pelas fixtures e pelo
  7-Zip File Manager;
- [x] definir o subconjunto realmente implementável: estado lógico validado ou
  stub controlado com ERROR_NOT_SUPPORTED;
- [x] remover o handle fictício e aceitar somente handles emitidos por uma
  tabela de ownership do runtime, se a enumeração for implementada;
- [x] validar ponteiros, contagens, buffers e GetLastError em todos os caminhos
  de sucesso e falha;
- [x] classificar cada export como Full, Limited ou Stub;
- [x] adicionar regressão unitária e atualizar a fixture/integração do 7-Zip
  sem promover resolução de import a suporte funcional.

**Aceitação:** nenhuma API retorna sucesso para uma operação não realizada;
nenhum handle inventado atravessa a ABI; os testes cobrem entradas válidas,
inválidas e a limitação publicada; a matriz registra o resultado observado.

**Evidência 2026-09-12:** `MPR.dll` foi reduzida a stubs explícitos. Os seis
exports validam `NETRESOURCEW`, strings, buffers e saídas; operações válidas
retornam `ERROR_NOT_SUPPORTED`, `WNetOpenEnumW` zera o handle e handles não
emitidos retornam `ERROR_INVALID_HANDLE`. `MprTest.*` passou, o fixture
`tl_7zfm_gui.exe` terminou com `7ZFM GUI APIS OK`/exit `0`, e `--report` mostrou
`MPR.dll (3/3 resolved)` com `support=stub`. O teste agregado
`SevenZipGuiCoverageTest.AllApisAndModules` continua com a falha pré-existente
de `SHGetSpecialFolderPathW`, registrada sem ser atribuída a R1.

### R4 — Tornar explícita a classificação de exports

**Problema confirmado:** ~~`ExportSupport::Full` era o valor padrão de
estruturas de registro. A omissão fazia os exports MPR parecerem completos no
`--report`, embora a implementação fosse um stub com falso sucesso.~~ Corrigido
nesta etapa.

**Tarefas:**

- [x] levantar registros que dependem do valor padrão e separar os casos
  intencionais dos acidentais, começando por MPR;
- [x] exigir classificação explícita para novos exports e migrar registros
  existentes sem alterar a ABI dos módulos já publicados;
- [x] auditar aliases e `direct_export` somente para preservar a distinção
  entre export de DLL convidada, stub, forwarder e endereço vazio; não mudar a
  classificação de um export PE convidado sem um caso de regressão;
- [x] adicionar teste de registro e diagnóstico que detecte classificação
  ausente ou incompatível com o contrato;
- [x] atualizar a documentação da API e a matriz quando uma classificação
  mudar.

**Aceitação:** cada exportação nova tem classificação, comportamento testado
e limitação publicada; nenhum export MPR não implementado aparece como
`Full`; endereço zero não é resolvido como função; nenhuma alteração de
classificação é feita apenas por nome de símbolo ou resolução de import.

**Evidência 2026-09-12:** `ExportedFunction` agora exige `ExportSupport` no
construtor; os registros existentes foram migrados explicitamente, enquanto
exports PE convidados continuam classificados como `Full` somente no caminho
`direct_export`. `register_module` rejeita valores inválidos antes de publicar
o módulo, coberto por `ModuleTest.RejectsInvalidExportSupportLevel`. As suítes
`ModuleTest.*`, `ImportResolverTest.*` e `Win32StubTest.*` passaram (43 testes),
e o relatório do 7-Zip confirmou os três imports MPR como `support=stub`.

### R2 — Adicionar regressão determinística para falha de inicialização de thread (E7)

**Lacuna de evidência:** ~~a criação de thread já valida o endereço inicial,
trata falhas de alocação e possui um caminho de falha para a configuração de
`ARCH_SET_GS`, mas ainda não existe uma fixture capaz de provocar essa falha de
modo determinístico.~~ Corrigida nesta etapa.

**Tarefas:**

- [x] introduzir uma fronteira de teste estreita para injetar a falha de
  arch_prctl, sem alterar o caminho normal nem a ABI Microsoft x64;
- [x] verificar limpeza de stack, TEB, descritores de thread, handles e
  sinalização de término quando a inicialização falhar;
- [x] garantir que a thread convidada não execute o entry point após a falha;
- [x] adicionar regressão de concorrência e trace do erro controlado;
- [x] atualizar a documentação de ABI e concorrência.

**Aceitação:** a falha injetada produz resultado reproduzível, não deixa estado
parcial observável e não permite execução convidada após `ARCH_SET_GS` falhar.
O caminho normal de `CreateThread` permanece protegido pelos testes
existentes, sem transformar uma lacuna de teste em alegação de incompatibilidade.

**Evidência 2026-09-12:** `Win32ConcurrencyTest.CreateThreadCleansUpAfterInjectedGsFailure`
usa um entry point convidado executável, injeta apenas a fronteira de
`set_guest_gs_base`, espera a conclusão, verifica exit code `0`, zero chamadas
ao entry point, handle inválido após `CloseHandle` e slot com TEB/stack limpos.
Também confirma no JSONL o evento `api-failure` de `CreateThread`/`guest-teb`.
Os dois testes de rejeição de entry point inválido continuam passando.

### R3 — Delimitar acesso seguro à memória convidada (E11)

**Risco arquitetural:** a validação por `/proc/self/maps` é uma fotografia.
Entre a validação e o acesso, o mapeamento pode mudar; isso deixa uma janela
TOCTOU em rotinas que leem ou escrevem memória do convidado. A fronteira
protegida, os consumidores comuns e os contratos especiais de imagem/SEH têm
regressões determinísticas e tratamento documentado.

**Tarefas:**

- [x] catalogar as categorias e os consumidores já auditados em
  `docs/arquitetura/memoria-convidada.md`, incluindo os contratos especiais
  de imagem/SEH;
- [x] escolher `process_vm_readv`/`process_vm_writev`, com fallback controlado
  para `/proc/thread-self/mem`, sem exceção C++ atravessar a ABI;
- [x] definir comportamento para páginas desmontadas, somente leitura,
  desalinhamento, overflow e buffers parcialmente acessíveis por meio de
  `GuestMemoryAccessStatus`;
- [x] adicionar testes de robustez para acesso concorrente e truncado em
  `RuntimeMemoryValidationTest.*`;
- [x] publicar a garantia efetiva e os limites residuais em
  `docs/arquitetura/memoria-convidada.md` e
  `docs/compatibilidade-runtime.md`.

**Aceitação — concluída em 2026-09-14:** nenhum caminho comum documentado
depende apenas de uma fotografia de `/proc/self/maps`; acessos inválidos falham
de forma controlada no processo convidado, sem corrupção do host nem falso
sucesso; os testes cobrem páginas desmontadas, permissões, overflow, buffers
parciais, estruturas Rtl e callbacks. Os contratos sem cópia genérica são
explicitamente limitados a objetos locais mantidos vivos pelo despachante
SEH/C++ e a endereços de código dentro da imagem ativa mantida pelo loader.

**Progresso da triagem 2026-09-12:** além da primitiva, `ReadFile`/`WriteFile`,
`FindFirstFileA/W`, `GetMessageA`, conversões comuns de caminho e entradas de
`WININET`, PSAPI, WINMM e DWM usam cópias protegidas. Os testes focados de
memória, arquivos, metadados, GUI, MPR, WININET e stubs auxiliares passaram; a busca de auditoria ainda encontra
rotas legadas em módulos como segurança, sincronização e APIs de processo/GUI.
Os sublotes registrados abaixo concluíram essa migração e encerraram a
aceitação do R3/E11 em 2026-09-14.

O lote seguinte migrou também estruturas e buffers de console/tempo, com os
testes de `Win32ConsoleTest`, `Win32ProcessConsoleTest`, `Win32TimeTest` e
`Win32WideTest` passando.

O sublote `KERNEL32/console-input` removeu pré-validações por fotografia nas
leituras/escritas de console e migrou `OutputDebugStringA` para uma cópia de
string protegida. As transferências efetivas continuam usando as primitivas
de memória, e `Win32ConsoleTest.*`/`Win32ProcessConsoleTest.*` passaram.

O sublote `runtime/tls-loader` migrou a inicialização do template TLS para
`read_guest_memory` e a publicação do índice TLS para `write_guest_memory`;
`OpenFile` passou a delegar a validação de nome diretamente a `CreateFileA`.
Os testes de TLS e de I/O protegido continuam passando, sem usar a fotografia
de mapas como garantia de acesso.

O sublote `KERNEL32/environment` migrou nomes, valores e expansões de ambiente
ANSI/Wide para cópias host e resultados para `write_guest_memory`. A regressão
`Win32EnvTest.ProtectedEnvironmentInputsAndOutputsRejectUnmappedPointers` e os
demais oito testes de ambiente passaram no preset sanitizado.

O sublote `KERNEL32/module` migrou nomes de módulos e símbolos para cópias
host, além dos handles de `GetModuleHandleExA/W` para `write_guest_value`.
Ordinais e endereços continuam seguindo seus contratos de token. A regressão
`Win32ModuleTest.ProtectedModuleNamesAndOutputsRejectUnmappedPointers` passou
junto com os 15 testes de registro/loader e as demais APIs de módulo.

O sublote `KERNEL32/toolhelp-process` migrou os registros de
`Process32FirstW/NextW` e da variante ANSI para objetos host e
`write_guest_memory`. A regressão de ponteiro inválido, a fixture `tl_toolhelp`
e seus testes de metadados, execução e relatório passaram.

O sublote `KERNEL32/process-outputs` migrou startup info, código de saída,
nome completo, afinidade, tempos, contadores de memória, informação de CPU e
estado de rede para transferências protegidas. A regressão
`Win32ProcessTest.ProtectedProcessOutputsRejectUnmappedPointers`, as coberturas
unitárias relacionadas e as fixtures `tl_k32_system`/`tl_process_parent`
passaram no preset sanitizado.

O sublote `KERNEL32/process-create` migrou os nomes e diretórios de
`CreateProcessA/W` para cópias protegidas e a publicação de
`PROCESS_INFORMATION` para `write_guest_memory`, com limpeza do filho em caso
de falha na saída. A regressão
`Win32ProcessTest.ProtectedCreateProcessInputsAndOutputsRejectUnmappedPointers`
e a fixture `tl_process_parent` passaram após reconstruir o executável do
runtime.

O sublote `KERNEL32/resource-input` migrou nomes nomeados de `FindResourceW`
 para cópias UTF-16 host, preservando IDs `MAKEINTRESOURCE` como tokens. A
 regressão `Win32ResourceTest.ProtectedResourceNamesRejectUnmappedPointers` e
 a fixture `tl_resources` passaram.

O lote de caminhos migrou as saídas de diretório, módulo, temporários,
capacidade de disco, nomes completos/finais e `file_part`, além das leituras de
strings A/W usadas por essas rotas. As coberturas existentes e a regressão
`Win32DirTest.ProtectedPathOutputsRejectUnmappedPointers` passaram.

O sublote `KERNEL32/file-input` removeu as leituras diretas de nome em
`CreateFileA/W`; o normalizador agora é a única etapa que copia e valida os
caminhos convidados antes do acesso ao sistema de arquivos. A regressão
`Win32FileTest.ProtectedIoOutputsRejectUnmappedPointers` também cobre entradas
A/W inacessíveis.

O sublote `MSVCRT/input` migrou os caminhos de `_open`, `_fdopen` e `fopen`,
`atoi`, `getenv`, os formatos `printf`/`fwprintf` e a saída `%n` para cópias e
transferências protegidas. A regressão
`MsvcrtInputTest.ProtectedStringInputsRejectUnmappedPointers` cobre formatos,
strings de argumento, caminhos e modos inválidos.

O lote de memória virtual migrou as estruturas e escalares de
`GlobalMemoryStatusEx`, `GlobalMemoryStatus`, `VirtualQuery`, `VirtualQueryEx`,
`VirtualProtect` e `GetPhysicallyInstalledSystemMemory`; a regressão
`Win32VirtualTest.ProtectedMemoryOutputsRejectUnmappedPointers` passou junto
com as coberturas de alocação, proteção e consulta.

O lote de sincronização migrou arrays de handles, `WaitOnAddress`, contadores
de semáforo, `INIT_ONCE`, SRW/condição e `RegisterWaitForSingleObject`; a
regressão `Win32ConcurrencyTest.ProtectedSynchronizationPointersRejectUnmappedMemory`
passou com a cobertura existente de eventos, semáforos, mutexes e múltiplos
waits.

O lote WININET migrou o corpo opcional de `HttpSendRequestW`, leitura e
disponibilidade de resposta, consultas HTTP, opções de timeout e
`InternetCrackUrlW`; `WininetTest.*` passou, incluindo a regressão de ponteiro
não mapeado para `URL_COMPONENTS`.

O lote `IPHLPAPI` migrou `GetAdaptersInfo`, `GetAdaptersAddresses` e
`if_nametoindex`: tamanhos e strings agora são copiados com a fronteira
protegida, os registros encadeados são montados em memória do host e publicados
em uma única transferência, e ponteiros internos são derivados com verificação
de overflow. `IphlpapiTest.RejectsUnmappedGuestPointers` passou; a enumeração
real foi executada quando o ambiente forneceu interfaces e é pulada de forma
controlada quando não há dados de rede. O lote foi compilado no preset
`validation-sanitize`.

O lote de perfil INI migrou `GetPrivateProfileStringA/W` e
`GetPrivateProfileSectionA/W`: strings opcionais são copiadas do convidado,
resultados truncados recebem terminador por escrita protegida e o caminho do
arquivo é traduzido somente depois de estar em memória do host. A regressão
`Win32FileTest.PrivateProfileStringsUseProtectedGuestBuffers` passou junto com
os testes da primitiva de memória.

O lote de threads migrou as saídas de `GetExitCodeThread`, `GetThreadTimes`,
`CreateThread`, `InitializeSListHead` e `InitializeProcThreadAttributeList`,
além dos ponteiros de `InterlockedPushEntrySList`. A regressão
`Win32ConcurrencyTest.ProtectedThreadBuffersRejectUnmappedMemory` passou com
os testes de concorrência e da primitiva protegida.

O lote de stubs comuns migrou saídas de WTS, impressão, SetupAPI, TDH,
memória de processo e DirectX/DXGI para cópias protegidas; `lstrcmpA` também
passou a comparar cópias locais das strings convidadas. A regressão
`Win32StubTest.ProtectedCoreStubOutputsRejectUnmappedGuestPointers` passou
junto com `Win32StubTest.*`, `WtsApiTest.*` e os testes da primitiva.

O primeiro lote de locale migrou `GetVersionExA/W`, `VerifyVersionInfoW`,
`GetUserDefaultLocaleName`, `LocaleNameToLCID`, `FormatMessageA/W`,
`GetSystemInfo` e `GetComputerNameA/W` para snapshots e publicações protegidas.
`Win32LocaleTest.ProtectedSystemAndMessageBuffersRejectUnmappedPointers`
passou junto com 23 testes de locale/code page, os testes de `FormatMessage` e
a primitiva de memória; as APIs de locale restantes continuam pendentes.

O segundo lote de locale migrou cópias contadas de strings ANSI/UTF-16 e
publicações protegidas para conversões de code page, `GetCPInfo`,
`GetLocaleInfoA/W`, `GetStringTypeW/ExA/ExW`, `LCMapStringA/W`,
`FoldStringW`, `GetNumberFormatW`, diretórios Windows e formatação de data/hora.
`Win32LocaleTest.ProtectedConversionAndFormattingBuffersRejectUnmappedPointers`
passou junto com os 31 testes focados de locale, code page, `FormatMessage` e
memória. A auditoria do arquivo de locale não encontra mais
`mapped_guest_*` nem acesso direto a buffers convidados; `EnumSystemLocalesW`
continua limitado ao callback explicitamente validado.

O lote inicial de `WS2_32` migrou `WSAStartup`, cópia de `sockaddr`,
`getsockname`/`getpeername`, `getaddrinfo`, `inet_addr`, `inet_ntop`,
`inet_pton`, `WSAAddressToStringA` e `gethostname` para buffers host
temporários e cópias protegidas. `WinSockTest.ProtectedAddressBuffersRejectUnmappedGuestPointers`
passou junto com os testes de cobertura de PuTTY, Notepad++ e memória.

O lote seguinte de `WS2_32` migrou payloads de `send`/`recv` e
`sendto`/`recvfrom`, opções de socket, `ioctlsocket`, `WSAIoctl` e
`getnameinfo`. Os syscalls de rede agora recebem cópias host dos dados de
entrada e só publicam respostas após `write_guest_memory`; a regressão
`WinSockTest.ProtectedPayloadAndOptionBuffersRejectUnmappedPointers` cobre
ponteiros inválidos e é pulada de forma controlada quando o sandbox não
permite sockets UDP locais. A cobertura de PuTTY, Notepad++ e memória também
passou.

O lote de polling e eventos de `WS2_32` migrou `WSAPoll`, `select`,
`WSAWaitForMultipleEvents` e `WSAEnumNetworkEvents`: descritores, conjuntos de
handles, timeouts e estruturas de eventos são lidos em snapshots host, e os
resultados são publicados com a primitiva protegida. A regressão
`WinSockTest.ProtectedPollingAndEventArraysRejectUnmappedPointers` passou,
junto com as coberturas de PuTTY, Notepad++ e memória.

O lote GDI migrou o texto de `TextOut`, o `RECT` de `FillRect`,
`GetObjectA/W`, o cabeçalho de `CreateDIBSection`, `GetTextExtentPoint32W`,
`GetTextMetricsA/W` e `GetClipBox` para snapshots locais e publicações
protegidas. A regressão
`Gdi32Test.ProtectedDrawingAndBitmapBuffersRejectUnmappedPointers` passou com
as coberturas existentes de bitmap, DIB e extensão de texto. As consultas e
buffers auxiliares foram fechados no lote seguinte.

O lote auxiliar de GDI migrou `GetCharWidthA/W`, `GetCharABCWidthsA`,
`GetCharABCWidthsFloatA`, `GetTextExtentPointA/W`, `GetTextExtentExPointA/W`,
`TranslateCharsetInfo`, as APIs de origem de janela e `GetDeviceGammaRamp`.
`Gdi32Test.ProtectedAuxiliaryTextAndDeviceBuffersRejectUnmappedPointers`
passou junto com os demais testes GDI, e a auditoria de `src/runtime/gdi32.cpp`
não encontra mais `mapped_guest_*` nem acesso direto aos buffers convidados.

O lote de segurança básica migrou snapshots de SID, token e caminhos wide,
além das publicações de `OpenProcessToken`, `GetTokenInformation`,
`AllocateAndInitializeSid`, `CopySid`, `CreateWellKnownSid`,
`CheckTokenMembership` e `BuildTrusteeWithSidW`. A regressão
`Win32SecurityTest.ProtectedTokenAndSidBuffersRejectUnmappedPointers` passou
com os testes existentes de token/SID e memória.

O lote seguinte migrou snapshots e publicações de ACLs e descritores em
`InitializeSecurityDescriptor`, `SetSecurityDescriptorDacl`, `SetEntriesInAclW`,
`GetNamedSecurityInfoW` e `SetFileSecurityW`; também removeu o consumo direto de
`mapped_range` desse módulo. A regressão
`Win32SecurityTest.ProtectedAclAndDescriptorBuffersRejectUnmappedPointers`
protege entradas, saídas e rollback de alocações inválidas.

O lote inicial de registro migrou as entradas e saídas de
`RegOpenKeyExA/W`, `RegCreateKeyExA/W`, `RegSetValueExA/W`,
`RegQueryValueExA/W` e `RegDeleteValueA/W` para snapshots e cópias protegidas,
incluindo dados contados, tamanhos e handles emitidos. A regressão
`Win32RegistryTest.ProtectedRegistryInputsAndOutputsRejectUnmappedPointers`
passou junto com as coberturas existentes de registro e conversão A/W.

O lote criptográfico migrou as publicações e buffers contados de
`CryptAcquireContextA/W`, `CryptGenRandom`, `CryptCreateHash`,
`CryptGetHashParam`, `CryptSignHashW`, `CryptExportKey`, `CryptGetUserKey`,
`CryptGetProvParam` e `SystemFunction036` para a fronteira protegida; os
stubs continuam sem expor provedores ou chaves reais. A regressão
`Win32CryptoTest.ProtectedCryptoBuffersRejectUnmappedPointers` passou junto
com as coberturas existentes de hash e aleatoriedade.

O lote de identidade e consultas auxiliares migrou `LookupPrivilegeValueW`,
`AdjustTokenPrivileges`, `GetFileSecurityW`, `GetUserNameA/W`,
`LookupAccountNameW`, `LsaOpenPolicy` e as saídas escalares de
`RegQueryInfoKeyA/W` para publicações protegidas. A regressão
`Win32AdvapiTest.ProtectedIdentityAndRegistryQueryOutputsRejectUnmappedPointers`
passou com as coberturas existentes de identidade, LSA e registro.

O fechamento do grupo ADVAPI32 migrou `IsTextUnicode` para snapshot do buffer
de entrada e publicação protegida do indicador. A regressão
`Win32AdvapiTest.ProtectedIsTextUnicodeBuffersRejectUnmappedPointers` passou
com as coberturas de ADVAPI32, registro, memória e segurança.

O sublote final de `ADVAPI32/crypto-enumeration` migrou `CryptEnumProvidersW`:
capacidade, tipo do provedor, nome UTF-16 e tamanho agora atravessam
`read_guest_value`, `write_guest_value` e `write_guest_memory`. A regressão
`Win32CryptoTest.CryptEnumProvidersUsesProtectedUtf16Buffers` cobre entradas e
saídas inacessíveis, consulta de capacidade e enumeração válida.

O lote de `dbghelp.dll` migrou `SymFromAddr` e `ImageNtHeader` para snapshots
e publicações protegidas, preservando o stub controlado de símbolos e a leitura
mínima da assinatura PE. A cobertura `Win32StubTest.DebugAndShellDialogStubsReportUnsupported`
passou com a regressão de memória e as demais coberturas de stubs.

O lote final de pequenas rotas KERNEL32 migrou `ReadDirectoryChangesW` para
publicação protegida, removeu a pré-validação TOCTOU de `InitializeSListHead`
(que já publicava por cópia protegida) e deixou `FlushViewOfFile` depender da
validação do `msync(2)` no kernel. A regressão
`Win32FileTest.ProtectedDirectoryChangeAndFlushInputsRejectUnmappedPointers`
cobre saídas inválidas e mapeamento válido.

O lote de metadados de arquivos removeu a pré-validação por snapshot das
consultas de volume, tempos e informações por handle. Leituras e publicações
agora passam pela cópia protegida, com a regressão
`Win32FileMetadataTest.ProtectedMetadataBuffersRejectUnmappedPointers`; as
11 coberturas de metadados, arquivo wide e memória passaram.

O lote de I/O síncrono migrou as saídas de `ReadFile`, `WriteFile`,
`SetFilePointer`, `SetFilePointerEx`, `DeviceIoControl` e `DuplicateHandle`
para cópias protegidas. A mesma regressão de arquivo cobre ponteiros inválidos;
as coberturas de I/O, duplicação de handles e memória passaram, e
`CloseHandle` foi corrigido para respeitar `ref_count` e fechar o descritor
somente no último handle.

O lote de tempo removeu as pré-validações por snapshot de contadores, relógios,
fusos e conversões FILETIME/SYSTEMTIME/DOS. Leituras e publicações agora usam
cópias protegidas; `Win32TimeTest.ProtectedTimeBuffersRejectUnmappedPointers`
passou junto das três regressões de memória.

O fechamento do lote de sincronização removeu as pré-validações por snapshot
dos atributos de segurança em criações de objetos e de `previous_count` em
`ReleaseSemaphore`. A regressão
`Win32ConcurrencyTest.ProtectedSynchronizationPointersRejectUnmappedMemory`
passou com as 28 coberturas de concorrência e memória.

O lote de stubs pequenos migrou a validação do prefixo de estruturas do
`COMDLG32` e a saída de `WINMM!timeGetDevCaps` para cópias protegidas. A
regressão `Win32StubTest.ComdlgStubsRejectFalseSuccessAndReportDialogFailure`
e a cobertura de WINMM passaram dentro das 16 regressões de stubs e memória.

O lote de composição e imagem migrou as saídas de `DWMAPI`, `gdiplus` e
`UxTheme` para cópias protegidas, mantendo os stubs sem fabricar objetos ou
estado visual. As regressões de stubs cobrem ponteiros nulos e inacessíveis;
as 16 coberturas de stubs e memória passaram.

O lote `POWRPROF` migrou o GUID de `PowerGetActiveScheme`, a validação dos
buffers de entrada e a saída zerada de `CallNtPowerInformation` para cópias
protegidas em blocos. A regressão cobre ponteiros de saída inacessíveis, sem
alterar os retornos determinísticos das APIs.

O lote `version.dll` migrou as saídas de tamanho de arquivo e de
`VerQueryValueA/W` para cópias protegidas. Os stubs continuam sem fabricar
metadados `RT_VERSION`, e a regressão cobre `handle` e ponteiro de consulta
inacessíveis, preservando `ERROR_NOT_SUPPORTED` nas consultas válidas.

O lote `PSAPI` removeu as validações prévias por snapshot de mapas nas
enumerações, nomes de módulos e contadores de memória. As saídas agora usam
cópias protegidas diretamente; a regressão cobre buffers inacessíveis nas
rotas A/W, enumeração e `GetProcessMemoryInfo`.

O lote MPR migrou as saídas `enum_handle` e `system` de `WNetOpenEnumW` e
`WNetGetResourceInformationW` para cópias protegidas. Os stubs continuam sem
criar conexões ou handles; buffers usados apenas para validar operações
rejeitadas permanecem sem alteração, e a regressão cobre os dois ponteiros de
saída inacessíveis.

O lote de console migrou as saídas escalares e estruturas de
`Read/WriteConsole`, `PeekNamedPipe`, `CreatePipe`, `GetCommState`,
`GetOverlappedResult` e `GetConsoleScreenBufferInfo` para cópias protegidas.
As validações de buffers que precedem I/O potencialmente bloqueante continuam
explícitas; a regressão cobre saídas inacessíveis sem alterar os retornos
limitados dessas APIs.

O lote `COMCTL32` migrou as saídas de `TaskDialog*`, dimensões e informações de
image list e `LoadIconWithScaleDown` para cópias protegidas. Os contratos
determinísticos dos stubs foram preservados; a regressão cobre todos esses
ponteiros de saída inacessíveis.

O sublote `COMCTL32/input` migrou `InitCommonControlsEx`, o texto inicial de
`CreateStatusWindowW` e o array de botões de `CreateToolbarEx` para leituras
protegidas e cópias locais. As regressões existentes de controles agora também
cobrem estruturas, strings e arrays de entrada inacessíveis.

O lote `SHELL32` migrou caminhos wide e saídas de `SHGetKnownFolderPath`,
`SHGetMalloc`, ícones, pastas, item Shell e `DragQueryPoint` para cópias
protegidas. As operações continuam limitadas ao prefixo ou a stubs
determinísticos; estruturas de entrada complexas permanecem para uma etapa
posterior, e a regressão cobre ponteiros de saída inacessíveis.

O lote `USER32/misc` migrou as saídas determinísticas de cursor, caret,
rolagem e informações de ícone para cópias protegidas. A regressão cobre
ponteiros inacessíveis sem alterar os retornos limitados dessas APIs.

O lote `USER32/dialog` migrou as saídas simples de `GetDlgItemTextA/W` e
`GetDlgItemInt` para cópias protegidas. Os stubs continuam retornando texto e
indicadores determinísticos; a regressão cobre ponteiros de saída inacessíveis.

O lote adicional de `USER32/misc` migrou `GetUpdateRect`,
`GetUserObjectInformationW`, `GetMonitorInfoW` e `GetComboBoxInfo` para
cópias protegidas. As estruturas continuam determinísticas, e a regressão
cobre seus ponteiros de saída inacessíveis.

O lote `SHELL32` migrou estruturas de entrada e strings de
`Shell_NotifyIconA/W`, `SHGetKnownFolderPath`, `SHGetFolderPathAndSubDirW`,
`ShellExecuteA/W`, `ShellExecuteExW`, `SHFileOperationW`, `SHGetFileInfoW` e
`SHBrowseForFolderW` para snapshots e cópias protegidas. Os stubs continuam
com falha controlada e sem iniciar processos ou alterar arquivos; a regressão
também cobre GUID, `SHELLEXECUTEINFO`, `SHFILEOPSTRUCT` e strings inválidos.

O lote `USER32/misc` migrou `BeginPaint`/`EndPaint`, operações de `RECT`,
`CharUpperW`/`CharLowerW`, `DrawTextA/W`, `LoadStringA/W`, enumeração de
display e `wsprintfW` para snapshots e publicações protegidas. A regressão
exercita ponteiros inválidos em entradas, retângulos e buffers de saída, e os
smokes existentes de GUI continuam passando.

O lote `USER32/window` migrou saídas de geometria, texto e nome de classe,
`FindWindowA/W`, `GetClassInfoW`, identificação de processo, transformação de
pontos e saídas de região para snapshots e cópias protegidas. A regressão cobre
buffers inválidos de janela, texto, classe, pontos e retângulos.

O sublote `USER32/window-registration` migrou `RegisterClass(A/W)`,
`RegisterClassEx(A/W)` e as strings de `CreateWindowExA/W` para estruturas e
cópias locais antes de qualquer consulta, conversão ou callback. A validação do
endereço do procedimento de janela continua separada da cópia dos dados, e a
regressão cobre estruturas, nomes e criações com ponteiros inválidos.

O sublote `USER32/menu` migrou `MENUITEMINFO` de entrada e saída, textos de
`AppendMenuA/W`, o nome de recurso de `LoadMenuW` e as saídas de
`GetMenuBarInfo`/`GetMenuStringW` para cópias e publicações protegidas. A
regressão cobre strings, estruturas aninhadas e buffers inacessíveis, mantendo
os retornos limitados dos stubs.

O sublote `USER32/dialog` migrou os nomes de template usados por diálogos
modeless e modais, as entradas de `MessageBoxA/W`, `SetDlgItemTextW` e
`IsDialogMessageW` para cópias locais. A regressão cobre texto, caption e
mensagens inválidos; os smokes existentes continuam protegendo o fluxo de
diálogo válido.

O sublote `USER32/message-core` migrou as estruturas de `GetMessageA/W`,
`PeekMessageA/W`, `TranslateMessage`, `DispatchMessageA/W` e os arrays de
`MsgWaitForMultipleObjectsEx` para transferências protegidas. Também protegeu
`GetKeyboardState`, `ToAsciiEx` e a saída de `SendMessageTimeoutA`; a regressão
cobre mensagens, handles, teclado e ponteiros de resultado inacessíveis.

O sublote `USER32/message-tree` migrou as estruturas e textos de inserção,
alteração e consulta do `SysTreeView32` para snapshots, cópias locais e saídas
protegidas. A regressão cobre buffers de entrada, texto aninhado e texto de
saída inacessíveis, mantendo a seleção e o modelo lógico existentes.

O sublote `USER32/message-controls` migrou as mensagens estruturadas de
toolbar e list-view, os arrays de partes da status bar e as strings ANSI/Wide
de status bar, edit e combo box para cópias protegidas. As saídas de item e
texto do list-view são publicadas somente por `write_guest_memory`. A regressão
`CommonControls.ControlMessagesRejectUnmappedNestedBuffers` cobre estruturas,
arrays, strings aninhadas e buffers de saída inacessíveis; os testes de modelo
lógico de toolbar, árvore e controles continuam passando.

O sublote `SHLWAPI` migrou as strings de caminho e comparação para snapshots
host e as mutações de `PathCombine`, `PathRemoveFileSpec`, `PathAddBackslash`,
`PathRemoveBackslash`, `PathStripPath`, `PathAddExtension`, `PathAppend`,
`PathRemoveExtension` e `PathRenameExtension` para publicações protegidas.
`AssocQueryStringW` e `ColorRGBToHLS` também não escrevem mais diretamente em
saídas convidadas; as variantes wide com assinatura `wchar_t*` reinterpretam
somente o endereço como UTF-16 convidado depois da cópia. A regressão
`ShlwapiTest.ProtectedPathInputsAndOutputsRejectUnmappedPointers` passou junto
com os testes de caminho e as integrações `tl_shell_path` no preset
`validation-sanitize`.

O sublote `OLE32/istream` migrou `QueryInterface`, `Read`, `Write`, `Seek`,
`Stat` e `Clone` da vtable `IStream` para transferências efetivas protegidas.
Buffers de entrada são copiados antes da mutação do stream e estruturas,
contadores, posições e referências de saída são publicados sem acesso direto
após uma fotografia de mapas. `OleStreamTest.ProtectedGuestBuffersRejectUnmappedPointers`
passou, assim como os cinco testes de stream e as integrações PE `tl_com` e
`tl_stream`.

O sublote `OLE32/basic-outputs` migrou `CoCreateGuid`, `CoGetMalloc`,
`CreateStreamOnHGlobal`, `CoCreateInstance`, `CLSIDFromString`, `DoDragDrop`,
`StringFromGUID2` e `CLSIDFromProgID` para cópias protegidas de estruturas,
GUIDs, nomes UTF-16 e ponteiros de saída. `Ole32Test.ProtectedGuidAllocatorAndDragOutputsRejectUnmappedPointers`
passou junto com os testes de `IStream` e as integrações PE de COM/stream.

O sublote `CRYPT32/name-output` migrou `CertGetNameStringW` para uma fotografia
protegida de `CERT_CONTEXT` e do certificado DER, cópia do OID convidado e
publicação UTF-16 por `write_guest_memory`. A regressão
`Crypt32Test.CertGetNameStringReadsSubjectIssuerAndValidatesBuffers` passou com
os seis testes `Crypt32Test` e as quatro integrações PE de `tl_crypt32` no
preset `validation-sanitize`.

O sublote `CRYPT32/blob-output` migrou `CertNameToStrW` para fotografia
protegida de `CERT_NAME_BLOB` e publicação UTF-16 protegida, incluindo o caso
de buffer insuficiente que precisa zerar somente a primeira unidade. A regressão
`Crypt32Test.CertNameToStrConvertsValidatedNameBlobAndBoundsOutput` passou junto
com as mesmas seis validações unitárias e quatro integrações PE de `tl_crypt32`.

O sublote `CRYPT32/context-copy` migrou `CertDuplicateCertificateContext` para
fotografia protegida de `CERT_CONTEXT` e do DER, mantendo a cópia rastreada e o
refcount internos do runtime. Contextos ou blobs inválidos falham sem leitura
direta; `Crypt32Test.CertContextAndStoreManagement` e as quatro integrações PE
de `tl_certcontext` passaram no preset `validation-sanitize`.

O sublote `CRYPT32/store-input` migrou `CertOpenStore` e os wrappers
`CertOpenSystemStoreA/W` para cópia protegida dos nomes de provedor e parâmetros
ANSI/UTF-16, preservando os identificadores especiais dos provedores de memória
e sistema. A mesma regressão de gestão de contexto e as quatro integrações PE
de `tl_certcontext` passaram novamente no preset `validation-sanitize`.

O sublote `CRYPT32/property-output` migrou `CertGetCertificateContextProperty`
para fotografia protegida de `CERT_CONTEXT`, leitura protegida de `data_size` e
publicação protegida das propriedades SHA-1 e friendly name. As respostas de
capacidade e `ERROR_MORE_DATA` permanecem preservadas; a regressão
`Crypt32Test.CertContextAndStoreManagement` e as quatro integrações PE de
`tl_certcontext` passaram no preset `validation-sanitize`.

O sublote `CRYPT32/store-query` migrou os parâmetros de busca de
`CertFindCertificateInStore`: o blob SHA-1 e o subject UTF-16 são copiados antes
da consulta, sem desreferenciar `find_para` durante a varredura. A regressão de
gestão de contexto cobre ponteiros inválidos e mantém a regra de não fabricar
certificados em uma loja vazia.

O sublote `WINTRUST/verify-input` migrou `WinVerifyTrust` para snapshots
protegidos da ação, `WINTRUST_DATA`, `WINTRUST_BLOB_INFO` e payload `TLTC`, além
de publicar `state_data` por escrita protegida. `WintrustTest` passou nos 3 casos
de entrada e as oito integrações PE de `tl_trust`/`tl_wthelper` passaram no
preset `validation-sanitize`.

O sublote `CRYPT32/rejection-output` migrou as saídas de rejeição de
`CertFreeCertificateContext`, `CryptMsgGetParam` e `CryptQueryObject` para
leituras/escritas protegidas, mantendo handles não fabricados, tamanho zero e
`ERROR_NOT_SUPPORTED` nos caminhos publicados. Os 4 testes CRYPT32 de contexto,
consulta de objeto, mensagem e fechamento passaram no preset
`validation-sanitize`.

O sublote `CRYPT32/usage-output` migrou as saídas de
`CertGetEnhancedKeyUsage` e `CertGetIntendedKeyUsage` para leitura e escrita
protegidas, incluindo preenchimento chunked para contagens maiores. O
comportamento continua sendo o stub determinístico publicado (estrutura vazia
ou bytes `0xFF`), sem afirmar parsing de EKU; `Crypt32Test` passou no caso novo
de buffers de uso.

O sublote `MPR/VERSION/rejeicao` removeu as sondagens por fotografia de mapas
dos stubs que rejeitam a operação antes de consumir seus buffers. `NETRESOURCEW`
e suas strings aninhadas são copiados por `copy_guest_wstring`; tamanhos e
saídas efetivamente publicados continuam usando `read_guest_value`/
`write_guest_memory`. As regressões de MPR e VERSION cobrem strings aninhadas,
saídas inválidas e a rejeição controlada de blocos não consumidos.

O sublote `SEH/stack-transfer` removeu `validate + memcpy` e desreferências
diretas dos endereços de retorno e registros salvos na pilha convidada. O
desempilhamento agora lê por `read_guest_memory`, e os trampolins C++ escrevem
por `write_guest_memory` após a restrição de faixa do TEB; a suíte `UnwindTest`
continua cobrindo pilha ativa, alocação, registradores salvos e cadeias.

O sublote `SEH/callback-boundary` migrou `RaiseException`,
`RtlVirtualUnwind`, `RtlLookupFunctionEntry`, `RtlPcToFileHeader` e
`UnhandledExceptionFilter` para cópias e publicações protegidas. Os handlers
SEH/C++ recebem somente os objetos locais criados pelo despachante, e callbacks
USER32 são validados pela imagem ativa mantida pelo loader; esses contratos
especiais estão documentados em `docs/arquitetura/memoria-convidada.md`. A
regressão `UnwindTest.ProtectedUnwindBoundariesRejectUnmappedPointers` cobre
ponteiros inválidos nas fronteiras Rtl e no filtro não tratado.

O fechamento `R3/E11` removeu os wrappers `mapped_guest_*` sem consumidores e
atualizou a matriz de compatibilidade para não descrever validações advisory
como garantia de acesso. A busca final não encontra `mapped_guest_*` no código
do runtime, e as ocorrências de `validate_mapped_*` ficam restritas ao
validador, ao fallback protegido de escrita e aos testes da primitiva.

**Evidência 2026-09-14:** o preset `validation-sanitize` compilou
`tradutorlinux_unit_tests`; os 23 testes focados de memória, virtual memory,
MPR, VERSION e unwind passaram; os cenários SEH/unwind, V2, C++ EH typed,
nested e unhandled passaram, e os três cenários C++ EH sem relocations que o
ASan não consegue mapear passaram com o binário debug atualizado. O CTest
completo também foi executado: as falhas restantes são externas a R3/E11
(isolamento de rede, backend X11 ausente, contratos Rust não construídos,
limitação de matriz de `missing_dll`, testes dinâmicos de WS2/gui e a sombra do
ASan para fixtures sem relocations); nenhuma falha pertence aos testes focados
da etapa.

### R5 — Alinhar a validação Rust/C++ de diretórios de exceções file-backed

**Problema reproduzido:** `Rufus_x64.exe` declara `.pdata` em `RVA 0xc5000`,
dentro da seção virtual-only `UPX0` (`SizeOfRawData=0`). O parser C++ rejeitava
essa tabela antes do mapeamento, mas o parser Rust a aceitava silenciosamente e
produzia um relatório divergente para a mesma imagem.

**Tarefas:**

- [x] reproduzir a divergência em ambos os backends com o Rufus real;
- [x] exigir que o diretório de exceções possua intervalo file-backed também no
  parser Rust, preservando a rejeição segura do C++;
- [x] adicionar regressão diferencial para uma tabela `.pdata` virtual-only;
- [x] atualizar as matrizes de `--report`/execução e a documentação corrente;
- [x] validar o corpus selecionado, recursivo, nativo e de instalação nos dois
  backends.

**Aceitação:** Rust ON e C++ OFF produzem a mesma categoria e código para o
Rufus e para as matrizes do corpus; nenhuma tabela virtual-only é lida ou
mapeada, e imagens empacotadas continuam exigindo uma etapa explícita de
desempacotamento fora deste runtime.

**Evidência 2026-09-14:** o teste
`RustPeParserTest.RejectsVirtualOnlyExceptionDirectoryLikeCpp` passou. As
matrizes de relatório selecionado (`27/27`), relatório recursivo (`64/64`),
execução nativa (`6/6`) e instalação (`8/8`) passaram nos builds Rust ON e C++
OFF. O corpus recursivo ficou classificado como `24` sucessos, `3` rejeições
estruturais e `37` formatos/arquiteturas não suportados.

### R6 — Despachar catches C++ FH4 do Notepad++

**Problema reproduzido:** o `notepad++.exe` real usa `__GSHandlerCheck_EH4` e
metadados FH4 comprimidos. O runtime reconhecia apenas `FuncInfo` v3, ignorava
os handlers FH4 e terminava no primeiro `0xE06D7363`, embora os imports e a
inicialização anterior já estivessem validados.

**Tarefas:**

- [x] identificar FH4 sem confundir seus dados com `__C_specific_handler`;
- [x] decodificar com limites `FuncInfo4`, `UnwindMap`, `TryBlockMap`, `IPMap`
  e `HandlerMap` relativos à imagem ativa;
- [x] selecionar `catch` tipado/catch-all e materializar captura por referência
  com `PMD` validado, mantendo cópia arbitrária fora do subconjunto seguro;
- [x] preparar o funclet convidado com retorno sintético alinhado e restaurar a
  continuação FH4 sem deslocar o frame em 8 bytes;
- [x] criar o smoke real headless do Notepad++ e atualizar a matriz/diagnóstico.

**Aceitação:** um executável real com FH4 atravessa seleção, objeto de captura
e `catchret` sem executar `__GSHandlerCheck_EH4` como handler v3; o caso não
produz `guest-signal` nem `guest-timeout` e termina com `ExitProcess(0)` no
smoke. Cleanups FH4, copy constructors arbitrários e GUI completa permanecem
fora da aceitação desta etapa.

**Evidência 2026-09-14:** `notepadpp_fh4_headless_smoke` passou no build C++
OFF (`build/debug`), confirmando dois `fh4-catch-typed`, os retornos pelo
trampoline e `ExitProcess(0)`. A suíte focada C++ EH/unwind passou com 23/23
testes. O smoke GUI foi pulado porque o Xvfb do ambiente não conseguiu abrir
`/tmp/.X11-unix`; isso é limitação ambiental, não resultado do convidado.

### R7 — Executar cleanup FH4 com fixture genérica

O cleanup FH4 do frame-alvo agora possui um subconjunto executável explícito.
A fixture PE32+ cobre `UnwindMap` FH4 com destrutor por objeto e ação por RVA;
o dispatcher valida `ScopeIndex`/estado, rejeita auto-links cíclicos e limita a
cadeia executável a quatro ações com destino, argumentos, stack e slots de
retorno validados. Cadeias maiores continuam no caminho legado e registram
`fh4-cleanup-limit`; copy constructors complexos, rethrow e GUI interativa
continuam dependentes de evidência própria.

- [x] criar fixture genérica FH4 e verificar metadata/imports;
- [x] executar destrutor por objeto e ação por RVA por trampolines MS x64;
- [x] proteger ciclos, estado, `ScopeIndex`, stack e limite de quatro ações;
- [x] repetir a fixture e o smoke headless do Notepad++ nos builds Rust ON e
  C++ OFF, sem `guest-signal`/`guest-timeout`.

**Evidência 2026-09-15:** nos builds `build/debug` (Rust OFF) e
`build/debug-rust` (Rust ON), os testes
`fixture_tl_cxx_eh_fh4_cleanup_metadata`,
`fixture_tl_cxx_eh_fh4_cleanup_cycle_metadata`,
`runtime_tl_cxx_eh_fh4_cleanup`, `runtime_tl_cxx_eh_fh4_cleanup_cycle` e
`notepadpp_fh4_headless_smoke` passaram (5/5 em cada build). A variante cíclica
foi rejeitada como `invalid-fh4-unwind-map` e terminou com o código controlado
da exceção; o Notepad++ terminou com `ExitProcess(0)` e `fh4-cleanup-limit`.

### R8 — Revalidar o corpus de aplicativos populares nos dois backends

Com a R7 fechada, a próxima unidade de evidência é repetir a análise e os
cenários autorizados do diretório `Aplicativos_Windows_Populares/`. A análise
estática deve cobrir todos os PE/DLL/MSIX encontrados recursivamente, mas a
execução direta permanece limitada a casos PE32+ já definidos e as instalações
continuam isoladas em prefixos temporários. DLLs independentes, instaladores
sem contrato e binários PE32/x86 não devem ser iniciados por esta matriz.

- [x] executar `popular_apps_report_matrix` e
  `popular_apps_recursive_report_matrix` nos builds Rust ON e C++ OFF;
- [x] executar `popular_apps_native_matrix` nos dois builds, com timeout e
  limites de CPU/memória;
- [x] executar `popular_apps_install_matrix` nos dois builds, verificando
  rejeição pré-extração e ausência de arquivos/cadastro parcial;
- [x] comparar os resultados e atualizar a matriz sem promover limitações a
  suporte geral.

**Evidência 2026-09-15:** o corpus contém 64 PE/DLL/MSIX; as duas matrizes de
`--report`, a matriz nativa e a matriz de instalação passaram nos builds
`build/debug` (Rust OFF) e `build/debug-rust` (Rust ON), sem divergência. Os
seis casos nativos autorizados e os oito casos de instalação reproduziram os
códigos e diagnósticos já publicados. Nenhum novo defeito do runtime foi
reproduzido nesta rodada.

### R9 — Corrigir `PeekMessageA/W` para eventos nativos e pendências GUI

A auditoria do fluxo GUI encontrou um problema real: embora os exports de
`PeekMessageA/W` estivessem marcados como disponíveis, a implementação só
consultava mensagens internas e cross-thread. Ela não fazia polling dos eventos
nativos, timers expirados ou pinturas pendentes; uma aplicação que dependesse de
polling não receberia entrada X11 e poderia girar indefinidamente.

- [x] preservar `PM_NOREMOVE` numa cache por janela e remover a mesma mensagem
  somente com `PM_REMOVE`;
- [x] traduzir eventos nativos de teclado, mouse, fechamento e pintura para o
  caminho não bloqueante, além de expor timers expirados;
- [x] adicionar a fixture PE32+ `tl_peek.exe` e o cenário `peek` do
  `runtime_gui_smoke`;
- [x] repetir o smoke GUI e os testes de metadata/`--report` no build C++ OFF,
  registrando o subconjunto suportado.

**Critério de aceite:** `tl_peek.exe` termina com exit `7` depois de observar
`WM_KEYDOWN('Q')` com `PM_NOREMOVE` e removê-lo com `PM_REMOVE`; o trace contém
as duas chamadas; os cenários GUI anteriores continuam passando.

**Evidência 2026-09-15:** `runtime_gui_smoke` passou no build `build/debug`
(Rust OFF), incluindo autoclose, fechamento, teclado, duas janelas, timer,
GDI, pintura, diálogo e `PeekMessageA`. O cenário `peek` registrou as chamadas
com `remove="0"` e `remove="1"`. Os filtros `wMsgFilterMin`/`wMsgFilterMax`
continuam fora deste subconjunto. A validação do PuTTY permanece separada:
configuração e janela de sessão são alcançadas, mas o fluxo ainda não chega a
`socket`/`connect` nem envia dados ao listener local, portanto não há promoção
para suporte SSH.

O próximo alvo direto, após esta etapa, é instrumentar e validar o contrato de
inicialização da sessão do PuTTY que precede a rede; a implementação de sockets
não deve ser ampliada sem reproduzir uma chamada convidada correspondente.

### R10 — Tornar observável a fronteira WinSock do fluxo PuTTY

A investigação do PuTTY tinha uma ambiguidade operacional: os imports de
`WS2_32.dll` eram resolvidos, mas o trace não distinguia uma API nunca chamada
de uma API chamada e mal implementada. Isso impedia atribuir o timeout à fase
de inicialização da sessão com evidência do runtime.

- [x] registrar no trace a entrada de `WSAStartup`, `getaddrinfo`, `socket`,
  `connect`, `send`, `recv`, `WSAAsyncSelect` e `WSAEventSelect`;
- [x] proteger a fixture positiva `tl_dynamic_ws2.exe` com os eventos de
  `WSAStartup`, `socket`, `closesocket` e `WSACleanup`;
- [x] fazer o probe SSH local exigir `WSAStartup` e rejeitar como limitação
  qualquer tentativa de `getaddrinfo`/`socket`/`connect` sem troca de dados;
- [x] repetir o probe PuTTY e confirmar a ausência de syscalls de rede do
  convidado com um listener TCP local.

**Critério de aceite:** a fixture dinâmica passa com os eventos de fronteira;
o PuTTY alcança a janela de sessão, registra `WSAStartup`, não registra
`getaddrinfo`/`socket`/`connect`/`send`/`recv` e termina com `guest-timeout 72`
como limitação controlada. Nenhum socket é implementado ou alterado nesta
etapa.

**Evidência 2026-09-15:** `putty_ssh_local_probe` passou no build `build/debug`
(Rust OFF), agora verificando o contrato de diagnóstico. A fixture
`tl_dynamic_ws2.exe` passou com acesso de rede controlado; sem essa permissão,
o runtime rejeitou a syscall Linux com o erro esperado de ambiente. A captura
`strace -f -e trace=network` do probe PuTTY mostrou somente o `socket`/`bind`/
`listen` do listener do harness, sem `AF_INET` do convidado. O bloqueio seguinte
continua sendo localizar a transição interna após a criação da janela de
sessão; não há evidência para ampliar WinSock ou promover SSH.

O próximo alvo é um smoke de transição da sessão que registre a última chamada
GUI/worker antes do `GetMessageA` ocioso, mantendo o critério de não adicionar
regras específicas do PuTTY ao runtime genérico.

### R11 — Registrar a transição da sessão para o `GetMessageA` ocioso

Havia uma lacuna entre a janela de sessão do PuTTY e o timeout: o trace
registrava a criação da GUI e a entrada em WinSock, mas não marcava o momento
em que o thread convidado terminava o trabalho observável e voltava ao message
loop. Sem essa fronteira, não era possível distinguir uma sessão parada no
callback GUI de uma sessão que apenas aguardava eventos nativos.

- [x] emitir uma marca única de `GetMessageA` ocioso por chamada bloqueante,
  identificando o mecanismo de polling nativo e sem expor ponteiros convidados;
- [x] registrar início e retorno de `DispatchMessageA` para que a última
  chamada GUI observável antes da espera possa ser localizada no trace;
- [x] fazer o smoke SSH local exigir um `DispatchMessageA` antes da transição
  ociosa, tanto no caminho de limitação quanto em eventual troca positiva;
- [x] manter a regressão genérica `runtime_gui_smoke` cobrindo dispatch e
  entrada em espera, sem código específico do PuTTY no runtime.

**Critério de aceite:** o trace deve conter
`GetMessageA status="idle" mechanism="native-poll"` depois de uma chamada
`DispatchMessageA`; o probe do PuTTY deve continuar classificando a ausência
de `getaddrinfo`/`socket`/`connect`/`send`/`recv` como limitação controlada,
sem alterar o contrato de WinSock.

**Evidência 2026-09-15:** o build `build/debug` (Rust OFF) recompilou somente
`tradutorlinux`, `runtime_gui_smoke` e `putty_ssh_smoke`. Os testes
`putty_ssh_local_probe` e `runtime_gui_smoke` passaram 2/2 sob Xvfb, com a
transição ordenada observada no trace. O PuTTY continua alcançando a janela de
sessão, sem enviar bytes ao listener, e terminando com `guest-timeout 72`;
isso localiza o limite depois do dispatch e antes da rede, mas não constitui
suporte SSH nem justifica shim específico.

O próximo alvo direto é isolar, por fixture ou trace já existente, o callback
de inicialização que antecede essa espera; se não houver uma chamada Win32
reproduzível, a limitação deve permanecer publicada em vez de receber uma
implementação sintética.

### R12 — Confirmar o bloqueio pós-criação sem atribuí-lo ao message loop

A R11 mostrou uma marca de espera antes da criação da janela de sessão, mas
isso não provava que o PuTTY voltava ao message loop depois de `Open`. Era
necessário separar essa lacuna temporal de um bloqueio interno do código
convidado e evitar a implementação especulativa de uma API.

- [x] capturar o trace completo do probe e ordenar `CreateWindowExA` da janela
  `PuTTY`, o último diagnóstico genérico observável e o timeout;
- [x] proteger no teste a ausência de novo `DispatchMessageA` e de novo
  `GetMessageA` ocioso depois da criação da janela de sessão;
- [x] observar o processo filho do convidado durante esse intervalo, usando
  somente estado/CPU do sistema, para distinguir execução ativa de espera;
- [x] manter o runtime sem nova API, chamada sintética ou regra específica do
  PuTTY enquanto não houver um callback Win32 reproduzível.

**Critério de aceite:** o probe deve continuar alcançando a janela `PuTTY`,
registrar o último diagnóstico genérico antes do timeout, não registrar rede,
message loop ou dispatch posteriores à janela e retornar a limitação controlada
`guest-timeout 72`. A observação do processo deve confirmar execução ativa do
convidado, sem promovê-la a suporte.

**Evidência 2026-09-15:** o trace do `putty_ssh_local_probe` mostra
`CreateWindowExA ... class="PuTTY" ... status="success"`, seguido de
`locale operation="oemcp" ... status="success"` e então
`terminated category="guest-timeout"`; não há `DispatchMessageA`,
`GetMessageA status="idle"` ou `getaddrinfo`/`socket`/`connect`/`send`/`recv`
depois da janela. Durante a reprodução, o processo filho do convidado ficou
em estado `R` com aproximadamente 99% de CPU, enquanto o processo-pai do
runtime permaneceu aguardando. O probe atualizado passou no build
`build/debug` (Rust OFF), sem alteração do comportamento WinSock.

Conclusão: o bloqueio atual está no código convidado após a criação da janela
de sessão e antes do retorno ao message loop; não há callback de inicialização
Win32 genérico reproduzido que justifique uma mudança no runtime. O próximo
trabalho deve ser uma análise de execução do código convidado (ou uma fixture
que isole o mesmo contrato), não uma nova API escolhida por inferência.

### R13 — Registrar o RIP do timeout para separar guest de hospedeiro

A observação de CPU da R12 distinguiu execução ativa de espera, mas ainda não
identificava em qual espaço de endereços a thread estava no instante do
timeout. Sem essa informação, uma amostra transitória no runtime poderia ser
confundida com um loop PE, ou vice-versa.

- [x] capturar, de forma best-effort e somente no Linux x86-64, os registradores
  da thread principal do filho antes do `SIGKILL` do timeout;
- [x] publicar `timeout-rip` no evento `terminated category="guest-timeout"` e
  derivar `rva`, `section` e `nearest-import` somente quando o endereço estiver
  dentro da imagem PE;
- [x] proteger o comportamento com a fixture `tl_hang.exe`, aceitando skip
  explícito quando a política do ambiente bloqueia `ptrace`;
- [x] repetir `putty_ssh_local_probe` fora do sandbox, correlacionando o RIP
  capturado com os mapas do mesmo processo e com o disassembly do runtime.

**Critério de aceite:** a fixture simples deve produzir `timeout-rip` dentro de
`.text` quando a captura estiver disponível; o PuTTY deve continuar retornando
`guest-timeout 72`, sem promoção de suporte, e o diagnóstico não pode inventar
uma API responsável quando o RIP estiver fora da imagem PE.

**Evidência 2026-09-15:** a fixture `tl_hang.exe` produziu
`timeout-rip="0x140001012" rva="0x1012" section=".text"` e retornou `72`.
No sandbox, `ptrace` é bloqueado e o teste foi marcado como skip; fora dele, o
mesmo teste passou com a captura ativa. O probe PuTTY continuou passando como
limitação controlada: depois de `CreateWindowExA` da janela `PuTTY` e de
`GetOEMCP`, não houve `GetMessageA` ocioso, `DispatchMessageA` ou rede. No
mesmo processo, o RIP capturado ficou fora da imagem PE em `0x140000000` e
correspondeu ao offset ELF `0x3d25ce` de
`__cyg_profile_func_enter` (`src/runtime/function_trace.cpp:30`), no código
executável do runtime. Isso elimina a hipótese de atribuir automaticamente o
timeout a uma API Win32 ou ao message loop, mas ainda não prova a causa do
loop hospedeiro; nenhuma API, shim ou regra específica do PuTTY foi adicionada.

O próximo alvo direto é uma comparação controlada do caminho hospedeiro de
instrumentação/trace e de sua fronteira com o retorno do convidado, preservando
o diagnóstico e sem desabilitar a observabilidade dos smokes.

### R14 — Corrigir a reentrância do gravador de trace JSON

A comparação revelou que o timeout amostrado em `__cyg_profile_func_enter` não
era, por si só, prova de um loop do runtime. O caminho reproduzível era outro:
com `--trace-json`, a thread que consumia a fila destruía uma string enquanto
segurava `g_trace_json_queue_mutex`; a instrumentação dessa destruição chamava
o hook, que tentava adquirir a mesma mutex. Isso congelava o gravador e a
thread principal, inclusive durante `--report`.

- [x] capturar as pilhas das threads com `gdb` e identificar a reentrada exata
  em `deque::pop_front` → `__cyg_profile_func_enter` → fila JSON;
- [x] impedir que somente a thread interna do gravador enfileire callbacks de
  instrumentação sobre sua própria fila;
- [x] proteger a correção com uma regressão que consome 256 eventos JSON e
  confirmar o `--report` normal e o `--trace-json --report` do PuTTY;
- [x] documentar que a instrumentação continua disponível para o runtime e o
  convidado, mas não para o código interno de consumo da fila.

**Critério de aceite:** o `--report` do PuTTY deve terminar com exit `0` nos
modos normal e JSON; a variante JSON deve criar `events-<pid>.jsonl`; a suíte
`TraceTest.*` deve passar sem espera indefinida; e o PuTTY interativo deve
continuar classificado como limitação `guest-timeout 72` quando o smoke GUI
estiver disponível.

**Evidência 2026-09-15:** `gdb` confirmou a autoespera na mutex da fila. Após a
correção, `TraceTest.*` passou 6/6; o `--report` normal do
`putty_x64.exe` passou com `result: supported`,
`compatibility: 100% (348/348 imports resolved)` e `execution: not-attempted`;
a variante
`--trace-json --report` passou em menos de 15 s e gerou um JSONL de 831237
bytes. O smoke GUI PuTTY foi tentado, mas esta execução encontrou
`XOpenDisplay` indisponível (`display :2`); isso foi registrado como falha
ambiental da reprodução, sem alterar a conclusão sobre o deadlock.

Com a fronteira do diagnóstico corrigida, o próximo trabalho volta ao bloqueio
real do aplicativo: reproduzir ou isolar o caminho convidado entre a criação da
janela de sessão e a primeira chamada de rede, sem inferir uma nova API a partir
de um RIP do hospedeiro.

### R15 — Separar amostras PE e hospedeiro no timeout

A R14 eliminou a reentrância do gravador JSON, mas uma única captura de RIP
continua insuficiente para explicar um timeout que atravessa código convidado e
instrumentação do runtime. A próxima etapa deve aumentar a resolução do
diagnóstico sem alterar o comportamento do guest nem escolher uma API por
inferência.

- [x] coletar, somente quando `--trace` estiver ativo, amostras best-effort do
  RIP da thread principal durante o timeout;
- [x] publicar a contagem de amostras dentro da imagem PE e fora dela no evento
  `terminated category="guest-timeout"`;
- [x] proteger a coleta com a fixture `tl_hang.exe` e repetir o smoke GUI local
  do PuTTY nos modos textual e JSON;
- [x] tornar a abertura do display no smoke tolerante à pequena janela entre o
  anúncio do Xvfb e a aceitação do primeiro cliente X11;
- [x] manter o runtime sem nova API, shim ou regra específica do PuTTY até que
  uma chamada Win32 reproduzível seja demonstrada.

**Critério de aceite:** `tl_hang.exe` deve continuar retornando
`guest-timeout 72`, o smoke PuTTY deve alcançar a janela de sessão e retornar a
limitação controlada, e o trace deve permitir distinguir execução na imagem PE
de execução no hospedeiro. A ausência de amostras por restrição de `ptrace`
deve continuar sendo um resultado best-effort, não uma falha do runtime.

**Evidência 2026-09-15:** o build `build/debug` recompilou apenas
`tradutorlinux` e `putty_ssh_smoke`. A fixture `tl_hang.exe` retornou `72`;
fora do sandbox produziu cinco amostras, todas na imagem PE
(`timeout-pe-samples="5"`, `timeout-host-samples="0"`), enquanto o sandbox
sem permissão de `ptrace` publicou contagens zero e o teste existente fez skip
somente da parte dependente dos registradores. Os testes direcionados de
timeout/trace passaram sem falhas.

O smoke PuTTY passou nos modos textual e `TL_PUTTY_TRACE_JSON`, sempre como
limitação controlada: configuração alcançada, zero bytes enviados e
`guest-timeout 72`. No JSON, o processo-pai registrou 33 amostras, todas fora
da imagem PE (`timeout-pe-samples="0"`, `timeout-host-samples="33"`); o JSON do
filho terminou sua sequência observável em chamadas genéricas de GUI/GDI,
incluindo `GetDoubleClickTime`, `GetSystemMenu`, `CreatePopupMenu` e
`AppendMenuA`, sem nova chamada de rede. A correção de prontidão do Xvfb
eliminou a corrida de `XOpenDisplay` sem mudar o runtime genérico.

Conclusão: o timeout do PuTTY continua depois da criação da janela e antes da
rede, mas a thread principal está no hospedeiro no instante das amostras; isso
não autoriza atribuir a causa a uma API Win32 ou declarar suporte SSH. A próxima
etapa direta é identificar a fronteira hospedeiro/guest desse caminho, mantendo
o smoke como limitação até existir uma causa reproduzível e um contrato geral.

### R16 — Identificar a fronteira hospedeiro/guest do timeout

A R15 separou os domínios de endereço, mas ainda não mostrava se a execução
havia retornado do trampoline de troca de pilha nem qual símbolo nativo
correspondia ao RIP fora da imagem PE. Esta etapa acrescenta somente
observabilidade genérica para fechar essa fronteira.

- [x] marcar no trace JSON a entrada e o retorno de
  `tl_call_guest_on_stack`;
- [x] resolver o `timeout-rip` hospedeiro para módulo curto, símbolo e offset,
  omitindo o caminho absoluto;
- [x] proteger os eventos de fronteira com a fixture `tl_nop.exe` (retorno
  natural) e manter o
  timeout `tl_hang.exe` controlado;
- [x] repetir o smoke PuTTY textual e JSON e confirmar que o resultado continua
  sendo limitação, sem nova API ou shim específico.

**Critério de aceite:** uma execução concluída deve conter entrada e retorno da
fronteira no JSON; um timeout deve conter entrada sem retorno, quando o trace
JSON estiver ativo, e o RIP host deve ser simbolizado quando `dladdr` permitir.
Nenhum desses eventos pode promover o PuTTY a suporte SSH ou alterar o
comportamento do runtime.

**Evidência registrada:**

- `tradutorlinux` e `tradutorlinux_unit_tests` recompilados no preset `build/debug`;
- 9 testes direcionados passaram, incluindo a entrada/retorno JSON com
  `tl_nop.exe` e a resolução de endereço host com `dladdr`;
- `tl_hang.exe` terminou com exit code 72, cinco amostras PE e evento JSON de
  entrada sem retorno;
- os smokes textual e JSON do PuTTY terminaram com exit code 0 do probe, sem
  bytes enviados e com a limitação `guest-timeout 72`; no JSON foram 33
  amostras host e nenhuma PE, com módulo `tradutorlinux` e símbolo
  `enqueue_function_json_trace` (`host-offset=0x3b`);
- não foi adicionada API Win32, DLL, shim ou regra específica do PuTTY.

### R17 — Auditar o efeito da instrumentação no bloqueio do PuTTY

A R16 mostrou que o timeout não retorna do trampoline, mas o RIP hospedeiro
variava entre funções do próprio diagnóstico. Esta etapa compara o mesmo smoke
com trace textual e com `--trace-json`, sem mudar o comportamento do runtime,
para separar custo de observabilidade da causa do bloqueio convidado.

- [x] repetir o smoke PuTTY textual e JSON com o mesmo alvo, timeout e cenário
  X11;
- [x] capturar a sequência textual após a criação da janela de sessão e
  confirmar a ausência de nova chamada Win32 ou de rede;
- [x] medir o custo observado da instrumentação JSON, identificando o
  resultado como amostra diagnóstica e não como benchmark;
- [x] manter o timeout `guest-timeout 72` e a matriz sem promoção de suporte,
  API nova ou shim específico.

**Critério de aceite:** os dois modos devem reproduzir a mesma limitação
controlada; a diferença de custo deve ser registrada sem ser confundida com a
causa do bloqueio. O próximo diagnóstico deve voltar ao stack/código convidado
ou a uma fixture que isole esse contrato.

**Evidência 2026-09-15:** no modo textual, o smoke terminou em `10,15s` (`user
8,17s`); no modo JSON terminou em `10,86s` (`user 11,57s`). A diferença é
observável, mas foi medida em uma única execução de cada modo. Ambos alcançaram
a janela `PuTTY`, não enviaram bytes e retornaram a limitação
`guest-timeout 72`. O trace textual terminou em `CreateWindowExA class="PuTTY"`,
`locale operation="oemcp"` e timeout, sem nova chamada Win32 ou de rede. O
snapshot textual caiu em `__cyg_profile_func_exit`; no JSON, em uma função do
gravador (`is_trace_json_enabled`/`enqueue_function_json_trace`, conforme a
amostra), confirmando que o diagnóstico pode alterar o custo e o local do
snapshot, mas não explica sozinho o bloqueio que também ocorre sem JSON.

Conclusão: não há correção de API justificada nesta rodada. A próxima etapa
direta é obter o contexto de retorno/stack do caminho hospedeiro e correlacioná-
lo com o código convidado após a criação da janela, mantendo PuTTY como
limitação controlada.

## Fora desta rodada

Não entram neste roadmap, por enquanto:

- otimizações sem benchmark;
- grandes refatorações de arquivos ou do runtime_context;
- Qt opcional, novos presets e reorganização de CI sem uma necessidade
  reproduzida;
- novas famílias de DLL ou suporte a PE32/x86;

Esses temas só podem entrar em uma revisão futura com evidência, alvo,
critério de aceite e etapa própria.

## Referências

- [PROJETO.md](PROJETO.md) — missão e limites do produto;
- [docs/compatibilidade.md](docs/compatibilidade.md) — matriz de estado;
- [docs/compatibilidade-runtime.md](docs/compatibilidade-runtime.md) —
  contratos do runtime;
- [docs/arquitetura/api-win32.md](docs/arquitetura/api-win32.md) — contratos
  de APIs Win32;
- as triagens de 2026-09-12, consolidadas nas etapas R1–R4 deste documento.
