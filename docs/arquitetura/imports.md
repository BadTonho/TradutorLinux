# Resolução de imports

Este documento descreve o contrato do resolvedor de imports, o registro de módulos internos e a fronteira de ABI das funções hospedeiras. A resolução ocorre antes do entry point, valida as dependências e preenche a IAT.

## Visão geral

1. `pe::parse_pe` lê a import table em `PeInfo::imports` e a delay import table em `PeInfo::delay_imports`. Ambas entregam DLLs com símbolos por nome ou ordinal e o RVA do slot correspondente na IAT (`ImportedSymbol::iat_rva`).
2. `loader::prepare_process` mapeia a imagem (`map_image`), resolve os imports (`resolve_imports`) e prepara a pilha do thread inicial.
3. `inspect_imports` classifica as duas tabelas sem alterar a imagem; `--report` usa esse resultado. Para cada símbolo resolvido, `resolve_imports` grava o endereço do export no slot da IAT (`write_image_bytes`), relaxando e restaurando as permissões das páginas cobertoras.
4. Se qualquer importação falhar, o status geral da resolução falha, o entry point não é executado e o runtime retorna `5` (`Unsupported`).

O registro de módulos é populado por `loader::register_builtin_modules()` antes do `prepare_process`. O CLI o chama automaticamente; os testes de unidade controlam o registro explicitamente (`register_module`/`clear_modules`).

## Registro de módulos internos

`include/tradutorlinux/loader/module.hpp`:

- `register_module(const InternalModule&)`: copia nomes e exports para armazenamento próprio. Nomes de DLL são comparados **case-insensitive**; nomes de símbolos, **case-sensitive**. Retorna `false` se o módulo já estiver registrado.
- `clear_modules()`: remove todos os módulos (usado em testes).
- `is_module_registered(dll)`, `find_export(ExportQuery{dll, symbol})`, `find_export_by_ordinal(dll, ordinal)`: consultas usadas pelo resolvedor.
- `ExportLookup` devolve `found`, `ordinal` e `address`; `address == 0` significa símbolo conhecido sem implementação (`not-implemented`).

### Módulos embutidos

Os módulos internos registram exports com ordinais internos definidos pelo projeto (não correspondem a ordinais reais do Windows):

| Módulo | Conteúdo |
|---|---|---|
| `KERNEL32.dll` | Console, erros, memória e arquivos; lista detalhada em `include/tradutorlinux/loader/module.hpp` |
| `USER32.dll` | MessageBox, janelas, message loop, teclado, timers e pintura |
| `GDI32.dll` | Stock objects e saída de texto |
| `msvcrt.dll` | CRT mínimo, stdio, conversões e strings wide guiados pelos aplicativos-alvo |
| `SHELL32.dll` | `CommandLineToArgvW` no subconjunto usado por `dos2unix`/`unix2dos` |
| `ole32.dll` | COM mínimo e `CreateStreamOnHGlobal`/`IStream` em memória |
| `WININET.dll` | HTTPS direto de loopback com CA fornecida pelo host |
| `WINTRUST.dll` | `WinVerifyTrust` com cadeia DER explícita `TLTC` |

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

O runtime resolve a tabela de modo antecipado: antes de executar o entry point,
substitui cada slot da delay IAT pelo export interno já registrado. Assim, a
primeira chamada chega diretamente ao export, sem executar o helper de
delay-load do Windows. `hmod`, binding e unload não são emulados nesta etapa.

`ResolvedImport::mechanism` diferencia `Static` de `Delay`; o trace usa
`mechanism="import"` ou `mechanism="delay-import"`, e o relatório separa as
DLLs atrasadas, mas soma ambos os grupos no percentual total.

## Status e diagnóstico

`ImportStatus` (em `import_resolver.hpp`): `Resolved`, `UnknownDll`, `UnknownSymbol`, `UnknownOrdinal`, `NotImpl`, `UnsupportedMechanism`.

O resolvedor reporta **todas** as entradas: para cada uma, um `ResolvedImport` com `dll`, `mechanism`, `symbol`/`ordinal`, `iat_rva`, `address`, `status` e `detail`. O status geral (`ResolveResult::status`) reflete o primeiro problema encontrado e `error_message` resume a primeira falha. O evento `unresolved` no trace informa `status`, `detail` e `mechanism` (ver `docs/diagnostico.md`).

## Mecanismos fora de escopo

- Delay imports com atributos diferentes de `grAttrs=0x1` continuam `unsupported-mechanism`. O helper de carregamento sob demanda e as semânticas de binding/unload não são executados: a resolução antecipada ignora essas tabelas.
- Forwarders de export ainda não são resolvidos na resolução estática; somente exports diretos de módulos internos registrados são aceitos. Para carregamento dinâmico, `GetProcAddress` usa busca global (`find_export_global`) e suporta ordinais via `MAKEINTRESOURCE`.
- `RtlUnwind`, `RtlUnwindEx`, `RaiseException`, VEH,
  `UnhandledExceptionFilter` e `__C_specific_handler` são exports funcionais
  somente para despacho SEH explícito da imagem ativa: `__try/__except`, V1/V2
  fora de epílogos e tabelas estáticas `.pdata`. Exceções C++, `__finally`,
  sinais Linux, tabelas dinâmicas e epílogos V2 continuam fora do contrato;
  veja [unwinding-x64.md](unwinding-x64.md).

### Carregamento dinâmico (Fase 12+)

`KERNEL32.dll!LoadLibraryA/W`/`LoadLibraryExA/W`, `FreeLibrary`, `GetModuleHandleA/W`/`GetModuleHandleExA/W` e `GetProcAddress` estão implementados sobre o mesmo registro (`loader::register_builtin_modules`). `LoadLibrary` normaliza o nome (filename após `\/:` , case-insensitive, `+ ".dll"`), aceita caminhos `C:\` e API Sets `api-ms-win-*`/`KERNELBASE` via `is_module_registered_forwarded`; `GetProcAddress` valida `proc_name` (string ou ordinal `<=0xFFFF`) e `module` (`0x1000` ou base do exe), retornando `ERROR_PROC_NOT_FOUND` (127) ou `ERROR_INVALID_HANDLE` conforme contrato. A fixture `tl_dynload.exe` protege o fluxo `LoadLibrary→GetProcAddress→call→FreeLibrary→GetModuleHandleEx`.

Antes da execução, o processo valida que o entry point está dentro de uma seção
`r-x`. A pilha inicial possui uma guard page e é instalada no contexto Microsoft
x64 por um trampolim assembly pequeno; o entry point não usa a pilha do host.

## Dependências não suportadas

Qualquer dependência não suportada falha de forma controlada e reproduzível:

- O entry point nunca é executado.
- O trace registra `[tl][imports][error] unresolved ...` para cada entrada com problema.
- O runtime retorna `5` (`Unsupported`) e libera a imagem e a pilha.
