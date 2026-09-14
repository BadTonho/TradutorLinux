# Memória convidada e fronteira host (R3/E11)

O ponteiro recebido de uma API Win32 pertence ao espaço de endereço do
programa convidado, mesmo quando o convidado roda no mesmo processo Linux que o
runtime. Ele deve ser tratado como entrada hostil: o mapa pode mudar entre
qualquer duas instruções, a faixa pode atravessar páginas com permissões
diferentes e um endereço aritmeticamente válido pode não estar mapeado.

## Inventário da fronteira

| Categoria | Exemplos no runtime | Risco | Tratamento atual |
|---|---|---|---|
| Strings de entrada | caminhos, `WININET`, `MPR`, `IPHLPAPI`, `GetPrivateProfile*` e `WS2_32` | Leitura após snapshot ou string sem terminador | Validação por blocos copiados com `copy_guest_cstring`/`copy_guest_wstring` nos caminhos migrados |
| Estruturas de entrada | `NETRESOURCEW`, arrays de handles, `URL_COMPONENTS`, `sockaddr`, parâmetros de arquivos, rede, janela, thread e segurança | Tamanho/layout inválido e ponteiros internos inválidos | Validar faixa, copiar para objeto host e validar campos apontados quando o caminho já foi migrado |
| Buffers de saída | `ReadFile`/`WriteFile`, `FindFirstFileA/W`, caminhos, `GetProcessMemoryInfo`, console/tempo, memória virtual, sincronização, `IPHLPAPI`, `GetPrivateProfile*`, thread, `WS2_32`, WININET, locale, GUI e handles | Escrita em página desmontada ou somente leitura | `write_guest_memory` nos caminhos migrados; stubs que rejeitam antes de consumir a saída validam somente o contrato nulo/tamanho |
| Imagem PE e contexto ABI | IAT, entry point, TEB, callbacks e registros de exceção | Endereços controlados pelo convidado e conversão de ABI | Imagem/contextos pertencem a regiões controladas pelo loader; callbacks têm contrato de dados runtime-owned e a pilha SEH usa transferências protegidas |
| Handles opacos | Arquivos, threads, janelas, rede e side-tables | Token arbitrário confundido com ponteiro host | Tabelas de ownership e validação de tipo; não são cópia de memória |

O inventário completo deve ser mantido por busca de `memcpy`, atribuições por
ponteiro, conversões `wide_to_utf8` e chamadas de callback nos módulos
`src/runtime/`. Código que opera exclusivamente sobre imagem mapeada pelo
loader ou sobre uma estrutura já copiada para o host não é um acesso de API
convidada e deve ser distinguido dos demais casos.

## Primitivas protegidas

`read_guest_memory(source, destination, size)` e
`write_guest_memory(destination, source, size)` fazem a transferência através
do kernel Linux. A primeira tentativa usa `process_vm_readv` ou
`process_vm_writev` para que a verificação de acessibilidade e a transferência
sejam uma operação do kernel. Em ambientes que negam essas syscalls para o
próprio processo, o runtime usa `/proc/thread-self/mem`; para escrita, ele
atualiza a fotografia de permissões antes do fallback e recusa uma faixa que
não esteja gravável.

O contrato do resultado é:

| Estado | Significado |
|---|---|
| `Success` | Todos os `size` bytes foram transferidos |
| `Partial` | O kernel transferiu apenas `transferred` bytes |
| `Unmapped` | A faixa não está acessível ou foi desmontada |
| `PermissionDenied` | A operação foi recusada por permissão |
| `InvalidArgument` | Ponteiro nulo, tamanho zero ou overflow de endereço |
| `SystemError` | Falha do mecanismo host sem classificação Win32 específica |

O host nunca transforma `Partial` em sucesso. Cada chamador deve rejeitar
qualquer estado diferente de `Success` antes de interpretar a cópia ou publicar
uma saída ao convidado.

`MPR.dll` copia `NETRESOURCEW` e suas strings aninhadas antes de rejeitar
conexões e enumeração. Os buffers de enumeração que o stub nunca consome não
são sondados por `/proc/self/maps`; a operação retorna `ERROR_NOT_SUPPORTED`
ou `ERROR_INVALID_HANDLE` sem falso sucesso e sem escrever nesses buffers.
`version.dll` segue o mesmo contrato para os blocos `RT_VERSION`: os stubs
não fabricam metadados nem leem o bloco; `GetFileVersionInfoSize*` e
`VerQueryValue*` publicam apenas suas saídas por cópia protegida.

O desempilhamento x64 preserva a restrição adicional de que cada endereço de
retorno e registro XMM lido da pilha passa por `read_guest_memory`. Os
trampolins de C++ publicam palavras de retorno por `write_guest_memory` depois
de conferir os limites do TEB; a conferência de limites não é usada como
substituta da transferência protegida. `RtlVirtualUnwind`,
`RtlLookupFunctionEntry`, `RtlPcToFileHeader` e `UnhandledExceptionFilter`
também copiam estruturas e publicam resultados pela mesma fronteira.

Há dois contratos especiais que não são rotas genéricas de buffers. Handlers
SEH/C++ recebem `EXCEPTION_RECORD`, `CONTEXT` e `DISPATCHER_CONTEXT` locais,
criados e mantidos vivos pelo despachante durante o callback; callbacks USER32
recebem apenas endereços de código dentro da imagem ativa, que o loader mantém
executável durante a chamada. Esses objetos não são lidos por validação de
`/proc/self/maps`; qualquer estrutura convidada que alimenta esses contratos é
copiada antes do callback.

O sublote `KERNEL32/file-input` usa o normalizador de caminhos como única
fronteira para `CreateFileA/W`: ele copia o nome A/W para memória host antes de
validar o caminho e abrir o arquivo. Assim, a rotina não acessa novamente o
ponteiro convidado depois de uma fotografia de `/proc/self/maps`.

O sublote `COMCTL32/input` lê `INITCOMMONCONTROLSEX` e o array de botões de
`CreateToolbarEx` com `read_guest_memory`, e converte o texto inicial de
`CreateStatusWindowW` a partir de uma cópia UTF-16 local. Nenhuma dessas rotas
interpreta novamente a estrutura, o array ou a string convidada após a cópia.

O sublote `MSVCRT/input` copia caminhos, modos, nomes de ambiente e formatos
ANSI/Wide antes de interpretar seus caracteres. Argumentos `%s` e `%ls` do
motor de formatação também são copiados, e `%n` publica sua saída por
`write_guest_memory`; o CRT não usa mais `validate_mapped_*` para ler essas
entradas ou escrever esse contador.

O sublote `KERNEL32/console-input` usa a transferência protegida diretamente
para buffers de `ReadConsoleW`/`WriteConsoleW` e para a saída de
`GetConsoleMode`, sem uma validação de mapa anterior. `OutputDebugStringA` copia
a mensagem para memória host antes de gerar o trace, como já fazia a variante
Wide.

O sublote `runtime/tls-loader` trata o template TLS como imagem convidada já
validada pelo loader, mas ainda faz a transferência efetiva por
`read_guest_memory`; o índice TLS é publicado por `write_guest_memory`. A API
legada `OpenFile` não mantém uma validação de mapa própria: delega o nome à
fronteira protegida de `CreateFileA`.

O sublote `KERNEL32/environment` copia nomes, valores e textos de expansão
UTF-16/ANSI para memória host antes de consultar o ambiente. Os resultados de
`GetEnvironmentVariableA/W` e `ExpandEnvironmentStringsW` são publicados com
`write_guest_memory`, incluindo o terminador, e a regressão
`Win32EnvTest.ProtectedEnvironmentInputsAndOutputsRejectUnmappedPointers`
cobre entradas e saídas inacessíveis.

O sublote `KERNEL32/module` copia nomes de módulos e símbolos antes de buscar
handles, bibliotecas ou exports. Os handles de `GetModuleHandleExA/W` são
publicados por `write_guest_value`; ordinais e endereços usados pelo contrato
de `GetProcAddress`/`GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS` continuam sendo
tratados como tokens, não como strings. A regressão
`Win32ModuleTest.ProtectedModuleNamesAndOutputsRejectUnmappedPointers` cobre
nomes e saídas inacessíveis.

O sublote `KERNEL32/toolhelp-process` lê `dwSize` por `read_guest_value`, monta
`PROCESSENTRY32W` em memória host e publica o registro completo com
`write_guest_memory`; a variante ANSI usa a mesma separação para seu registro
local. `Win32ToolhelpTest.ProtectedProcessEntriesRejectUnmappedPointers` e a
fixture `tl_toolhelp` cobrem os caminhos inválido e de enumeração real.

O sublote `KERNEL32/process-outputs` monta startup info, tempos, máscaras,
contadores, informações de CPU e estado de rede em objetos host. Capacidades e
tamanhos são lidos com `read_guest_value`; saídas, inclusive o tamanho
necessário de `QueryFullProcessImageNameW`, usam `write_guest_memory` ou
`write_guest_value` e rejeitam falhas de transferência.

O sublote `KERNEL32/process-create` usa `translate_windows_path` e cópias
UTF-16 locais para os nomes/diretórios de `CreateProcessA/W`. O
`PROCESS_INFORMATION` é inicializado e publicado por `write_guest_memory`; se
a publicação final falhar, o filho criado é encerrado e o slot é liberado.

O sublote `KERNEL32/resource-input` copia nomes UTF-16 nomeados antes de
consultar a árvore de recursos. IDs pequenos continuam sendo tokens
`MAKEINTRESOURCE`; a árvore PE, por outro lado, é lida somente na imagem já
validada pelo loader. `Win32ResourceTest.ProtectedResourceNamesRejectUnmappedPointers`
e a fixture `tl_resources` cobrem a fronteira.

O lote `SHELL32` segue esse contrato para suas estruturas de entrada e strings:
`NOTIFYICONDATA`, `GUID`, `SHELLEXECUTEINFO`, `SHFILEOPSTRUCT`,
`CommandLineToArgvW` e as entradas de `ShellExecuteA/W` são primeiro copiados
para objetos host. As saídas de `SHELLEXECUTEINFO` e `SHFILEOPSTRUCT` são
publicadas de volta com `write_guest_memory`; nenhum desses caminhos
desreferencia o ponteiro convidado após uma validação por snapshot.

O lote `USER32/misc` aplica o mesmo contrato a `PAINTSTRUCT`, `RECT`,
`DrawTextA/W`, `LoadStringA/W`, `CharUpperW`/`CharLowerW`, estruturas de
display e `wsprintfW`. Os cálculos são feitos em cópias host e somente os
resultados finais são publicados pela primitiva protegida.

O lote `USER32/window` aplica o contrato a saídas de geometria, texto e
classe, a `FindWindowA/W`, `GetClassInfoW`, `MapWindowPoints` e às saídas de
região. Os nomes são copiados antes da busca e as estruturas/arrays são
transformados em objetos host antes da publicação.

O sublote `USER32/window-registration` aplica o contrato a
`RegisterClass(A/W)`, `RegisterClassEx(A/W)` e às strings de
`CreateWindowExA/W`. As estruturas e nomes são copiados para objetos locais
antes de conversão, busca de classe ou montagem de `CREATESTRUCT`; o endereço do
procedimento de janela é validado como callback convidado, sem desreferência
direta de campos da estrutura original.

O sublote `USER32/menu` aplica o contrato a `MENUITEMINFO`, aos textos de
`AppendMenuA/W`, ao nome de recurso de `LoadMenuW` e às saídas de
`GetMenuBarInfo`/`GetMenuStringW`. Estruturas aninhadas são lidas para objetos
host, e buffers de texto e estruturas de saída são publicados somente por
`write_guest_memory`.

O sublote `USER32/dialog` aplica o contrato aos nomes de template de diálogos,
às entradas de `MessageBoxA/W`, `SetDlgItemTextW` e `IsDialogMessageW`. Os
templates, strings e mensagens são copiados para objetos host antes de buscar
recursos, abrir a GUI ou despachar comandos.

O sublote `USER32/message-core` aplica o contrato às estruturas de mensagem de
`GetMessageA/W`, `PeekMessageA/W`, `TranslateMessage` e `DispatchMessageA/W`,
aos arrays de handles de `MsgWaitForMultipleObjectsEx` e às saídas de teclado e
timeout. Mensagens e arrays são lidos para objetos locais, e cada publicação
usa a primitiva protegida.

O sublote `USER32/message-tree` aplica o contrato às estruturas e textos do
`SysTreeView32`. Os pedidos de inserção, alteração e consulta são copiados para
buffers host; textos aninhados são lidos com cópias limitadas, e as respostas
são publicadas por `write_guest_memory`.

O sublote `USER32/message-controls` aplica o contrato às estruturas de
`TB_ADDBUTTONS` e `LVM_*`, ao array de `SB_SETTEXT`/partes e às strings de
status bar, edit e combo box em variantes ANSI/Wide. Estruturas e strings são
copiadas para objetos host; saídas de item e texto do list-view são publicadas
por `write_guest_memory`, sem desreferenciar ponteiros convidados depois de
uma validação por fotografia.

O sublote `SHLWAPI` copia strings ANSI/UTF-16 de caminhos e comparações antes
de normalizar, buscar ou comparar seus caracteres. As APIs mutáveis publicam
os resultados de `PathCombine`, `PathRemoveFileSpec`, `PathAddBackslash`,
`PathRemoveBackslash`, `PathStripPath`, `PathAddExtension`, `PathAppend`,
`PathRemoveExtension` e `PathRenameExtension` com `write_guest_memory`.
`AssocQueryStringW` e `ColorRGBToHLS` usam a mesma fronteira para suas saídas.
Nas funções wide declaradas com `wchar_t*`, `wchar_t` não é interpretado como
um elemento do buffer: o endereço é tratado como unidades UTF-16 convidadas
somente após a cópia protegida. `ShlwapiTest.ProtectedPathInputsAndOutputsRejectUnmappedPointers`
protege entradas e saídas inacessíveis, e `tl_shell_path` mantém a integração
válida.

O sublote `OLE32/istream` lê `riid` e buffers de `IStream::Write` com
`read_guest_memory`, publica `Read`, `Seek`, `Stat`, `QueryInterface` e `Clone`
com `write_guest_memory`/`write_guest_value`, e só altera o estado do stream
depois da transferência necessária. A vtable e o objeto `IStream` são memória
privada do runtime; os buffers apontados pelos métodos continuam pertencendo ao
convidado. `OleStreamTest.ProtectedGuestBuffersRejectUnmappedPointers` cobre
entradas e saídas inacessíveis, e `tl_com`/`tl_stream` cobrem a chamada pela ABI.

O sublote `OLE32/basic-outputs` usa cópias protegidas para GUIDs, objetos COM,
alocadores, efeitos de drag/drop e estruturas de saída. `StringFromGUID2` e
`CLSIDFromProgID` tratam seus parâmetros wide como UTF-16 convidado, apesar da
assinatura host com `wchar_t*`; `CoCreateInstance` lê os identificadores para
objetos host antes de publicar `ppv`. A regressão
`Ole32Test.ProtectedGuidAllocatorAndDragOutputsRejectUnmappedPointers` cobre
entradas e saídas inacessíveis.

O sublote `ADVAPI32/crypto-enumeration` copia a capacidade de
`CryptEnumProvidersW` antes de enumerar e publica o tipo, o nome UTF-16 e o
tamanho por transferências protegidas. A assinatura host usa `wchar_t*` por
compatibilidade, mas o conteúdo convidado é tratado como unidades UTF-16; a
regressão `Win32CryptoTest.CryptEnumProvidersUsesProtectedUtf16Buffers` cobre
consulta de capacidade, enumeração válida e destinos inacessíveis.

O sublote `CRYPT32/name-output` fotografa `CERT_CONTEXT` e o blob DER por
`read_guest_memory`, copia o OID usado por `CERT_NAME_ATTR_TYPE` e publica o
nome UTF-16 de `CertGetNameStringW` por `write_guest_memory`. Contextos, blobs,
OID e saídas inacessíveis são rejeitados sem desreferenciar memória convidada;
`Crypt32Test.CertGetNameStringReadsSubjectIssuerAndValidatesBuffers` e as
integrações de `tl_crypt32` protegem esse contrato.

O sublote `CRYPT32/blob-output` fotografa `GuestDataBlob` e seu DER por
`read_guest_memory` e publica `CertNameToStrW` por `write_guest_memory`. No
buffer insuficiente, a primeira unidade é zerada pela mesma primitiva; blob,
DER e destino inacessíveis falham de modo controlado. A regressão
`Crypt32Test.CertNameToStrConvertsValidatedNameBlobAndBoundsOutput` cobre esse
contrato na integração `tl_crypt32`.

O sublote `CRYPT32/context-copy` fotografa `CERT_CONTEXT` e o DER antes de
criar a cópia rastreada de `CertDuplicateCertificateContext`. O contexto
publicado é memória privada do runtime, e entradas convidadas inválidas são
rejeitadas sem desreferenciação direta; `Crypt32Test.CertContextAndStoreManagement`
e `tl_certcontext` cobrem o fluxo.

O sublote `CRYPT32/store-input` copia os nomes de provedor de `CertOpenStore` e
os parâmetros de `CertOpenSystemStoreA/W` para strings host antes de validar o
subconjunto e criar o handle. Os identificadores especiais de provedor não são
tratados como strings; nomes ANSI/UTF-16 inválidos são rejeitados sem acesso
direto à memória convidada.

O sublote `CRYPT32/property-output` fotografa `CERT_CONTEXT`, lê a capacidade
de `CertGetCertificateContextProperty` com `read_guest_memory` e publica hash
SHA-1 ou friendly name com `write_guest_memory`. A capacidade requerida e o
caso `ERROR_MORE_DATA` também são publicados pela fronteira protegida; contexto,
capacidade e buffers inacessíveis falham de modo controlado.

O sublote `CRYPT32/store-query` copia para memória host os parâmetros de
`CertFindCertificateInStore`: `GuestDataBlob`/SHA-1 usa snapshot do blob e dos
20 bytes, enquanto subject wide usa `copy_guest_wstring`. A enumeração compara
somente ponteiros de contextos emitidos pelo runtime; buscas inválidas falham
antes da consulta.

O sublote `WINTRUST/verify-input` lê a ação, `WINTRUST_DATA` e
`WINTRUST_BLOB_INFO` com `read_guest_memory`, copia o payload `TLTC` antes do
parser DER e publica o `state_data` de `VERIFY`/`CLOSE` por transferência
protegida. O estado e os registros devolvidos pelos `WTHelper*` continuam em
memória privada do runtime; ação, estruturas ou payload inacessíveis falham sem
desreferenciação direta.

O sublote `CRYPT32/rejection-output` usa transferências protegidas para validar
contextos externos em `CertFreeCertificateContext`, zerar `pcbData` em
`CryptMsgGetParam` e limpar as saídas opcionais de `CryptQueryObject`. Assim,
os caminhos de rejeição não escrevem diretamente em ponteiros convidados nem
fabricam handles ou contextos.

O sublote `CRYPT32/usage-output` lê `usage_size` com `read_guest_memory` e
publica a estrutura vazia de `CertGetEnhancedKeyUsage` com
`write_guest_memory`; `CertGetIntendedKeyUsage` preenche `key_usage` em blocos
protegidos. Essas APIs permanecem stubs determinísticos e não interpretam
certificados ou extensões EKU.

## Limites residuais

`validate_mapped_range` permanece deliberadamente como predicado advisory:
consulta `/proc/self/maps` e pode ficar desatualizado imediatamente depois.
A auditoria dos consumidores comuns não encontra mais chamadas que usem esse
predicado como garantia para um acesso posterior; os usos restantes ficam no
próprio validador/fallback interno e nos testes da primitiva. Os únicos
contratos sem cópia genérica são as estruturas locais mantidas vivas pelo
despachante SEH/C++ e os endereços de código dentro da imagem ativa para
callbacks, ambos descritos acima e controlados pelo loader.

O fallback de `/proc/thread-self/mem` é uma compatibilidade de ambiente, não
uma sandbox. Ele não transforma a execução de um `.exe` em operação segura e
não amplia o escopo de arquiteturas suportadas.
