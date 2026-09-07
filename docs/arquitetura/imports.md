# Resolução de imports

Este documento descreve o contrato do resolvedor de imports, o registro de módulos internos e a fronteira de ABI das funções hospedeiras. A resolução ocorre antes do entry point, valida as dependências e preenche a IAT.

## Visão geral

1. `pe::parse_pe` lê a import table em `PeInfo::imports` e a delay import table em `PeInfo::delay_imports`. Ambas entregam DLLs com símbolos por nome ou ordinal e o RVA do slot correspondente na IAT (`ImportedSymbol::iat_rva`).
2. `loader::prepare_process` mapeia a imagem (`map_image`), resolve os imports (`resolve_imports`) e prepara a pilha do thread inicial. Nas execuções nativas não-Proton, a resolução usa um `GuestModuleGraph` privado da execução para incluir providers de DLL do perfil e DLLs PE ao lado do executável.
3. `inspect_imports` classifica as duas tabelas sem alterar a imagem; `--report` usa esse resultado. Para cada símbolo resolvido, `resolve_imports` grava o endereço do export no slot da IAT (`write_image_bytes`), relaxando e restaurando as permissões das páginas cobertoras.
4. Se qualquer importação falhar, o status geral da resolução falha, o entry point não é executado e o runtime retorna `5` (`Unsupported`).

O registro de módulos genéricos é populado por `loader::register_builtin_modules()` antes do `prepare_process`. O CLI o chama automaticamente; os testes de unidade controlam o registro explicitamente (`register_module`/`clear_modules`). O grafo de DLLs PE não é global: cada execução cria seu próprio `GuestModuleGraph`, que consulta o perfil, o diretório da aplicação, `drive_c` e os módulos genéricos sem compartilhar handles ou imagens entre prefixos.

## Registro de módulos internos

`include/tradutorlinux/loader/module.hpp`:

- `register_module(const InternalModule&)`: copia nomes e exports para armazenamento próprio. Nomes de DLL são comparados **case-insensitive**; nomes de símbolos, **case-sensitive**. Retorna `false` se o módulo já estiver registrado.
- `clear_modules()`: remove todos os módulos (usado em testes).
- `is_module_registered(dll)`, `find_export(ExportQuery{dll, symbol})`, `find_export_by_ordinal(dll, ordinal)`: consultas usadas pelo resolvedor.
- `ExportLookup` devolve `found`, `ordinal` e `address`; `address == 0` significa símbolo conhecido sem implementação (`not-implemented`).
- Um `ExportedFunction` pode declarar um forwarder textual (`DLL.Simbolo` ou
  `DLL.#ordinal`) com `address == 0`. `find_export_forwarded` segue a cadeia
  até um export direto, com profundidade máxima 32; ciclos, sintaxe inválida e
  destinos ausentes retornam `found=false` e `ExportLookup::detail` descreve a
  causa. `find_export` continua sendo a consulta direta, sem seguir a cadeia.

### Módulos embutidos

Os módulos internos registram exports com ordinais internos definidos pelo projeto (não correspondem a ordinais reais do Windows):

| Módulo | Conteúdo |
|---|---|---|
| `KERNEL32.dll` | Console, erros, memória, arquivos, alocações Global/Local, seções críticas, locale e mensagens ANSI/W; lista detalhada em `include/tradutorlinux/loader/module.hpp` |
| `USER32.dll` | MessageBox, janelas, message loop, teclado, timers e pintura |
| `GDI32.dll` | Stock objects e saída de texto |
| `msvcrt.dll` | CRT mínimo, stdio, conversões e strings wide guiados pelos aplicativos-alvo |
| `SHELL32.dll` | `CommandLineToArgvW` no subconjunto usado por `dos2unix`/`unix2dos` |
| `ole32.dll` | COM mínimo e `CreateStreamOnHGlobal`/`IStream` em memória |
| `WININET.dll` | HTTPS direto de loopback com CA fornecida pelo host |
| `WINTRUST.dll` | `WinVerifyTrust` com cadeia DER explícita `TLTC` e `WTHelper*` para consultar o estado criado pela verificação |
| `CRYPT32.dll` | `CertGetNameStringW` para nomes em blob X.509 DER |

## Grafo de módulos PE por execução

Em uma execução nativa, `GuestModuleGraph` mantém a árvore de
DLLs PE32+ AMD64 carregada naquela execução. Cada nó registra a imagem mapeada,
`PeInfo` com imports/exports, base, provider, contagens de referências,
dependências e estado de carregamento. O grafo pertence ao `GuestContext` e é
descartado ao terminar; não há handles, imagens ou contagens compartilhados
entre prefixos.

Para cada módulo, o loader tenta os providers nesta ordem:

1. mapeamento explícito do perfil em `compat/dlls/`;
2. arquivo PE regular encontrado no diretório da aplicação solicitante;
3. arquivo PE regular encontrado em `drive_c`, depois `C:\Windows\System32`,
   `C:\Windows` e a raiz de `C:`;
4. export da implementação genérica registrada internamente.

A DLL do perfil não é descoberta pela listagem da pasta: inclusive suas
dependências precisam estar no manifesto para usar `compat/dlls/`. O loader
valida PE32+ AMD64, limites de seções e diretórios, aplica base relocations,
preserva W^X e resolve imports estáticos e delay imports eager antes do attach.
Exports são consultados por nome ou ordinal; forwarders `DLL.Funcao` e
`DLL.#ordinal` percorrem o mesmo grafo, com detecção de ciclos e limite de
profundidade.

Se o export não existir no provider personalizado, a consulta continua no
provider de `drive_c` e depois no genérico. Se o arquivo personalizado for
ausente, inválido, tiver import não resolvido, dependência ausente, ciclo ou
falhar no attach, ele é rejeitado inteiro antes do entry point e a resolução
recomeça com o fallback disponível. Uma falha posterior causada pelo código
da DLL já anexada é uma falha do convidado, sem fallback silencioso.

O ciclo de vida suporta imports estáticos e `LoadLibrary`/`GetProcAddress`/
`FreeLibrary`, com handle específico, refcount e unload quando não restarem
dependências. O attach chama dependências, TLS callbacks e `DllMain` em ordem
determinística; o detach chama `DllMain`, TLS callbacks e descarrega
dependências em ordem reversa. Falha de attach desfaz módulos e referências
parciais. `DLL_THREAD_ATTACH` e `DLL_THREAD_DETACH` são encaminhados às threads
convidadas. Delay imports continuam eager; carregamento lazy não é declarado.

As DLLs personalizadas são código PE executado com os privilégios do processo
filho. Esta camada não é sandbox, não verifica assinatura/hash e não carrega
bibliotecas Linux, scripts ou código nativo fora de uma imagem PE32+ AMD64.

## Backend Proton externo

O backend Proton do schema 3 não participa do `GuestModuleGraph` nem da
resolução de imports interna. Quando selecionado por `app run`, o runtime
valida uma instalação Proton configurada, estagia o aplicativo em um prefixo
separado e entrega o controle ao launcher `proton runinprefix`. O Proton resolve os
imports PE pelo próprio Wine; portanto, `dlls[]` é rejeitado nesse backend e
não há mistura entre `TL_DLL_OVERRIDES`, DLLs genéricas do runtime próprio e o
processo Proton.

`--report` continua descrevendo somente a resolução do runtime próprio e não
carrega nem executa o Proton. A seleção explícita, a versão validada, o
staging, o launcher, o stderr prefixado e o resultado do processo aparecem no
componente `proton` de `app run --trace`.

A fixture `tl_graphics_probe.exe` exerce, pelo backend externo, o primeiro
componente gráfico validado: uma janela X11 e uma apresentação D3D11 sob
Proton Experimental. O teste `integration_proton_graphics` é opcional e só é
registrado quando `TL_PROTON_ROOT` aponta para uma instalação real; ele valida
o caminho D3D11→DXVK/Vulkan em X11, sem alterar a resolução de imports do
`GuestModuleGraph` e sem declarar suporte a D3D12, áudio, entrada ou jogos.

A fixture `tl_d3d12_probe.exe` valida separadamente a inicialização e a
submissão de comandos D3D12, a criação de uma swapchain flip de dois buffers
e uma chamada `Present`. `integration_proton_d3d12` usa a mesma seleção
explícita de Proton e confirma esse caminho D3D12→VKD3D-Proton/Vulkan, sem
transformar a prova controlada em suporte geral de D3D12 ou de jogos.

A entrada Win32 é exercida por `tl_input_probe.exe` e
`integration_proton_input`. O driver nativo localiza a janela no Xvfb e envia
eventos X11/XTest à árvore da janela; a fixture confirma a conversão para
`WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP` e `WM_KEYDOWN/CHAR/UP`. Esse teste não
altera o `GuestModuleGraph` e não declara suporte a raw input, gamepad/XInput
ou jogos.

O áudio Win32 é exercido por `tl_audio_probe.exe` e
`integration_proton_audio`. A fixture resolve `XAudio2_8.dll!XAudio2Create`,
cria engine, voz master e voz PCM, submete um buffer, inicia/paralisa o
processamento e libera as vozes. O teste valida o caminho de engine e vozes
no Proton real, sem alterar o `GuestModuleGraph` e sem declarar fidelidade
sonora ou suporte multimídia geral.

## Fronteira de ABI (`ms_abi`)

`include/tradutorlinux/runtime/winapi.hpp` define `TL_MSABI` como `__attribute__((ms_abi))` em GCC/Clang x86-64 e declara as funções hospedeiras com vinculação C (`extern "C"`), `noexcept` e a convenção Microsoft x64. Tipos mínimos Win32 usados nas assinaturas ficam em `tradutorlinux::abi` (`Handle`, `Bool`, `Dword`, `Uint` e as constantes de handle padrão).

Regras da fronteira (ver também `docs/arquitetura/abi-x64.md`):

- Nenhuma exceção C++ pode atravessar a fronteira; por isso as funções são `noexcept`.
- A chamada entra como código Windows (MS x64); o compilador gera o trampolim de convenção automaticamente via atributo.
- As APIs registram chamadas e resultados no componente `runtime`; parâmetros fora do contrato retornam erro Win32 e diagnóstico estruturado.

Assinaturas hospedadas:

| API | Assinatura | Comportamento |
|---|---|---|
| `tl_GetStdHandle` | `void* (Dword)` | Retorna token opaco para um handle padrão ou `NULL`. |
| `tl_WriteFile` | `Bool (Handle, const void*, Dword, Dword*, void*)` | Escreve bytes em stdout/stderr; `overlapped` precisa ser nulo. |
| `tl_ReadFile` | `Bool (Handle, void*, Dword, Dword*, void*)` | Lê bytes de stdin; `overlapped` precisa ser nulo. |
| `tl_ExitProcess` | `void (Dword)` | Registra o código e retorna o controle ao runner. |
| `tl_RtlCaptureContext` | `void (CONTEXT*)` | Captura o contexto AMD64 do chamador para o núcleo de unwinding. |
| `tl_RtlLookupFunctionEntry` | `RUNTIME_FUNCTION* (DWORD64, DWORD64*, void*)` | Consulta `.pdata` somente na imagem PE ativa. |
| `tl_RtlVirtualUnwind` | `void* (DWORD, DWORD64, DWORD64, RUNTIME_FUNCTION*, CONTEXT*, void**, DWORD64*, void*)` | Desempilha um frame e opcionalmente devolve handler, sem invocá-lo. |
| `tl_RtlPcToFileHeader` | `void* (void*, void**)` | Devolve a base da imagem PE ativa que contém o PC. |
| Ambiente/locale/FLS | Assinaturas Win32 `W` e `TL_MSABI` | `Set/GetEnvironment*`, bloco UTF-16, expansão, CP1252/437/UTF-8, FLS por thread e locale `en-US` estático (consulta/validação, enumeração única, `CT_CTYPE1`, data/hora); ver `ambiente-locale-fls.md`. |
| Processo/console | Assinaturas Win32 `W` e `TL_MSABI` | Handles padrão mutáveis, `STARTUPINFOW` AMD64, console UTF-16, diretório lógico, recursos do processador, ponteiros codificados e SList vazia; ver `console.md`. |
| Alocação Global/Local | `void* (Dword, size_t)` e `void* (void*)` / `int (void*)` | `GlobalAlloc`/`LocalAlloc` aceitam `GMEM_MOVEABLE`/`GMEM_ZEROINIT`; `GlobalLock`/`Unlock` controlam contagem de locks e `GlobalFree`/`LocalFree` rejeitam handles arbitrários. |
| Certificados DER | `Dword (PCCERT_CONTEXT, Dword, Dword, void*, LPWSTR, Dword)` | `CertGetNameStringW` extrai nomes subject/issuer nos tipos simple, friendly, DNS, email e atributo OID; consulta de capacidade e `ERROR_INSUFFICIENT_BUFFER` são validadas. |

O subconjunto adicional usado pelo instalador Logitech é protegido pela
fixture `tl_k32_gap.exe`: `InitializeCriticalSectionAndSpinCount` e
`InitializeCriticalSectionEx` mantêm a tabela lateral de seções críticas;
`AreFileApisANSI` informa ACP `1252`; `FormatMessageA` compartilha o catálogo
limitado de mensagens de `FormatMessageW`. Essas APIs são exports diretos de
`KERNEL32.dll`, e imports ausentes continuam impedindo o entry point antes de
qualquer execução.

`GlobalAlloc`/`GlobalLock`/`GlobalUnlock`/`GlobalFree` e `LocalAlloc`/`LocalFree`
formam o subconjunto de alocação compartilhado observado em WinRAR e Rockstar.
Flags desconhecidas e handles arbitrários são rejeitados; `GMEM_MOVEABLE` e
`GMEM_ZEROINIT` usam blocos `malloc`/`calloc` registrados por processo. A
fixture `tl_globalmem.exe` protege o contrato e a resolução estática.

`CRYPT32.dll!CertGetNameStringW` é um subconjunto independente de confiança:
valida um `GuestCertContext` de 40 bytes, percorre a estrutura DER do certificado
e converte atributos de nome para UTF-16. Somente `X509_ASN_ENCODING` e os tipos
de nome documentados na fixture `tl_crypt32.exe` são aceitos; SAN, propriedades
friendly, loja de certificados, cadeia e Authenticode permanecem fora do
contrato. O export é registrado separadamente de `WINTRUST.dll`.

Os três helpers `WTHelperProvDataFromStateData`,
`WTHelperGetProvSignerFromChain` e `WTHelperGetProvCertFromChain` aceitam
somente ponteiros retornados pela mesma chamada `WinVerifyTrust` em estado
`VERIFY`; a tabela transitória contém signer `0` e certificados `0..1` e é
invalidada no `CLOSE`. O contrato não expõe loja, contra-assinaturas ou
Authenticode.

## Patch da IAT

`loader::write_image_bytes` (em `image_mapper.hpp/.cpp`) escreve `size` bytes no RVA informado:

- Localiza a região de mapa que cobre o intervalo (RVA + tamanho). Fora de qualquer região → `InvalidAddress`.
- `mprotect` das páginas cobertoras para `PROT_READ | PROT_WRITE`, grava, e restaura as permissões da região.
- Falha de `mprotect` → `MprotectFailed`.

O resolvedor serializa o endereço do export em little-endian de 8 bytes e o grava no slot `iat_rva` de cada símbolo estático ou atrasado. A IAT nunca fica gravável após a resolução.

## Delay imports

O leitor aceita descritores de 32 bytes do diretório 13
(`IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`) quando `grAttrs == 0x1` (`dlattrRva`).
Nome da DLL, INT e IAT são obrigatórios, precisam caber na imagem/arquivo e a
INT deve terminar em thunk nulo; símbolos por nome e por ordinal usam o mesmo
formato PE32+ da import table normal. O terminador do diretório precisa ter os
oito campos nulos.

O runtime resolve a tabela de modo antecipado (política `eager` do subconjunto
atual): antes de executar o entry point, substitui cada slot da delay IAT pelo
export interno já registrado. Assim, a primeira chamada chega diretamente ao
export, sem executar o helper de delay-load do Windows. `hmod`, binding e
unload não são emulados nesta etapa; portanto, isso não é uma equivalência com
a resolução sob demanda do Windows.

`ResolvedImport::mechanism` diferencia `Static` de `Delay`; o trace usa
`mechanism="import"` ou `mechanism="delay-import"`, e o relatório separa as
DLLs atrasadas, mas soma ambos os grupos no percentual total.

## Status e diagnóstico

`ImportStatus` (em `import_resolver.hpp`): `Resolved`, `UnknownDll`, `UnknownSymbol`, `UnknownOrdinal`, `NotImpl`, `UnsupportedMechanism`.

O resolvedor reporta **todas** as entradas: para cada uma, um `ResolvedImport` com `dll`, `mechanism`, `symbol`/`ordinal`, `iat_rva`, `address`, `status` e `detail`. O status geral (`ResolveResult::status`) reflete o primeiro problema encontrado e `error_message` resume a primeira falha. O evento `unresolved` no trace informa `status`, `detail` e `mechanism` (ver `docs/diagnostico.md`).

## Mecanismos fora de escopo

- Delay imports com atributos diferentes de `grAttrs=0x1` continuam `unsupported-mechanism`. O helper de carregamento sob demanda e as semânticas de binding/unload não são executados: a resolução antecipada ignora essas tabelas.
- Forwarders registrados pelo runtime são resolvidos na resolução estática por
  `find_export_forwarded`, incluindo cadeias de múltiplos saltos e destino por
  ordinal. API Sets continuam sendo aliases de módulo separados de forwarders
  textuais; ciclos, destinos ausentes e profundidade acima de 32 falham de
  forma controlada. No grafo de `app run`, `GetProcAddress` usa o handle
  específico e suporta ordinais via `MAKEINTRESOURCE`; a busca global fica
  somente no caminho legado sem DLLs PE do perfil.
- `RtlUnwind`, `RtlUnwindEx`, `RaiseException`, VEH,
  `UnhandledExceptionFilter` e `__C_specific_handler` são exports funcionais
  somente para despacho SEH explícito da imagem ativa: `__try/__except`, V1/V2
  fora de epílogos e tabelas estáticas `.pdata`. Exceções C++, `__finally`,
  sinais Linux, tabelas dinâmicas e epílogos V2 continuam fora do contrato;
  veja [unwinding-x64.md](unwinding-x64.md).

### Carregamento dinâmico (Fase 12+)

`KERNEL32.dll!LoadLibraryA/W`/`LoadLibraryExA/W`, `FreeLibrary`, `GetModuleHandleA/W`/`GetModuleHandleExA/W` e `GetProcAddress` usam o `GuestModuleGraph` em execuções nativas não-Proton. `LoadLibrary` normaliza o nome (filename após `\/:`, case-insensitive, adicionando ".dll"), procura o provider do perfil, o diretório da aplicação, `drive_c` ou o módulo genérico, incrementa a referência e executa attach. `GetProcAddress` usa o handle específico do módulo, aceita nome ou ordinal `<=0xFFFF` e ainda permite fallback por export conforme a política do grafo. `FreeLibrary` decrementa a referência e descarrega somente quando não há referências estáticas, dinâmicas ou dependências.

Em execuções nativas não-Proton, a busca do grafo inclui o diretório da
aplicação antes das pastas do prefixo. Isso permite carregar uma DLL PE32+
AMD64 lado a lado com um executável chamado diretamente, sem transformar essa
DLL em módulo compartilhado do runtime.

Sem `GuestModuleGraph`, o caminho legado continua restrito às implementações
genéricas registradas por `loader::register_builtin_modules`. A fixture
`tl_dynload.exe` protege o fluxo genérico
`LoadLibrary→GetProcAddress→call→FreeLibrary→GetModuleHandleEx`; a fixture
`tl_compat_dll_app.exe` protege o fluxo de DLL PE do perfil, incluindo
dependência, TLS, attach/detach e isolamento entre prefixos.

Antes da execução, o processo valida que o entry point está dentro de uma seção
`r-x`. A pilha inicial possui uma guard page e é instalada no contexto Microsoft
x64 por um trampolim assembly pequeno; o entry point não usa a pilha do host.

## Dependências não suportadas

Qualquer dependência não suportada falha de forma controlada e reproduzível:

- O entry point nunca é executado.
- O trace registra `[tl][imports][error] unresolved ...` para cada entrada com problema.
- O runtime retorna `5` (`Unsupported`) e libera a imagem e a pilha.
