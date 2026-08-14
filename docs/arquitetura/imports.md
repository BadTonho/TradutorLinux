# Resolução de imports (Fase 3)

Este documento descreve o contrato do resolvedor de imports, o registro de módulos internos e a fronteira de ABI das funções hospedeiras. Nesta fase o entry point não é executado; a resolução serve para validar as dependências e preencher a IAT.

## Visão geral

1. `pe::parse_pe` lê a import table e entrega `PeInfo::imports` (DLLs com símbolos por nome ou por ordinal) e, para cada símbolo, o RVA do slot correspondente na IAT (`ImportedSymbol::iat_rva`).
2. `loader::prepare_process` mapeia a imagem (`map_image`), resolve os imports (`resolve_imports`) e prepara a pilha do thread inicial.
3. Para cada símbolo, o resolvedor procura a DLL no registro de módulos internos; quando encontra, grava o endereço do export no slot da IAT (`write_image_bytes`), relaxando e restaurando as permissões das páginas cobertoras.
4. Se qualquer importação falhar, o status geral da resolução falha, o entry point não é executado e o runtime retorna `5` (`Unsupported`).

O registro de módulos é populado por `loader::register_builtin_modules()` antes do `prepare_process`. O CLI o chama automaticamente; os testes de unidade controlam o registro explicitamente (`register_module`/`clear_modules`).

## Registro de módulos internos

`include/tradutorlinux/loader/module.hpp`:

- `register_module(const InternalModule&)`: copia nomes e exports para armazenamento próprio. Nomes de DLL são comparados **case-insensitive**; nomes de símbolos, **case-sensitive**. Retorna `false` se o módulo já estiver registrado.
- `clear_modules()`: remove todos os módulos (usado em testes).
- `is_module_registered(dll)`, `find_export(ExportQuery{dll, symbol})`, `find_export_by_ordinal(dll, ordinal)`: consultas usadas pelo resolvedor.
- `ExportLookup` devolve `found`, `ordinal` e `address`; `address == 0` significa símbolo conhecido sem implementação (`not-implemented`).

### Módulos embutidos

`KERNEL32.dll` registra os quatro exports do marco, com ordinais internos definidos pelo projeto (não correspondem a ordinais reais do Windows, pois não são usados na ABI x64):

| Símbolo | Ordinal interno | Endereço |
|---|---|---|
| `GetStdHandle` | 1 | `tl_GetStdHandle` |
| `WriteFile` | 2 | `tl_WriteFile` |
| `ReadFile` | 3 | `tl_ReadFile` |
| `ExitProcess` | 4 | `tl_ExitProcess` |

## Fronteira de ABI (`ms_abi`)

`include/tradutorlinux/runtime/winapi.hpp` define `TL_MSABI` como `__attribute__((ms_abi))` em GCC/Clang x86-64 e declara as funções hospedeiras com vinculação C (`extern "C"`), `noexcept` e a convenção Microsoft x64. Tipos mínimos Win32 usados nas assinaturas ficam em `tradutorlinux::abi` (`Handle`, `Bool`, `Dword`, `Uint` e as constantes de handle padrão).

Regras da fronteira (ver também `docs/arquitetura/abi-x64.md`):

- Nenhuma exceção C++ pode atravessar a fronteira; por isso as funções são `noexcept`.
- A chamada entra como código Windows (MS x64); o compilador gera o trampolim de convenção automaticamente via atributo.
- As APIs registram chamadas e resultados no componente `runtime`; parâmetros fora do contrato registram um aviso `stub` com status `not-implemented`.

Assinaturas hospedadas:

| API | Assinatura | Comportamento |
|---|---|---|
| `tl_GetStdHandle` | `void* (Dword)` | Retorna token opaco para um handle padrão ou `NULL`. |
| `tl_WriteFile` | `Bool (Handle, const void*, Dword, Dword*, void*)` | Escreve bytes em stdout/stderr; `overlapped` precisa ser nulo. |
| `tl_ReadFile` | `Bool (Handle, void*, Dword, Dword*, void*)` | Lê bytes de stdin; `overlapped` precisa ser nulo. |
| `tl_ExitProcess` | `void (Dword)` | Registra o código e retorna o controle ao runner. |

## Patch da IAT

`loader::write_image_bytes` (em `image_mapper.hpp/.cpp`) escreve `size` bytes no RVA informado:

- Localiza a região de mapa que cobre o intervalo (RVA + tamanho). Fora de qualquer região → `InvalidAddress`.
- `mprotect` das páginas cobertoras para `PROT_READ | PROT_WRITE`, grava, e restaura as permissões da região.
- Falha de `mprotect` → `MprotectFailed`.

O resolvedor serializa o endereço do export em little-endian de 8 bytes e o grava no slot `iat_rva` de cada símbolo. A IAT nunca fica gravável após a resolução.

## Status e diagnóstico

`ImportStatus` (em `import_resolver.hpp`): `Resolved`, `UnknownDll`, `UnknownSymbol`, `UnknownOrdinal`, `NotImpl`, `UnsupportedMechanism`.

O resolvedor reporta **todas** as entradas: para cada uma, um `ResolvedImport` com `dll`, `symbol`/`ordinal`, `iat_rva`, `address`, `status` e `detail`. O status geral (`ResolveResult::status`) reflete o primeiro problema encontrado e `error_message` resume a primeira falha. O evento `unresolved` no trace informa `status` e `detail` (ver `docs/diagnostico.md`).

## Mecanismos fora de escopo

- Delay imports: a presença do diretório de dados 13 (`IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT`) torna a resolução `unsupported-mechanism`.
- Forwarders de export ainda não são resolvidos (a Fase 3 só usa exports diretos de módulos internos registrados).

Antes da execução, o processo valida que o entry point está dentro de uma seção
`r-x`. A pilha inicial possui uma guard page e é instalada no contexto Microsoft
x64 por um trampolim assembly pequeno; o entry point não usa a pilha do host.

## Dependências não suportadas

Qualquer dependência não suportada falha de forma controlada e reproduzível:

- O entry point nunca é executado.
- O trace registra `[tl][imports][error] unresolved ...` para cada entrada com problema.
- O runtime retorna `5` (`Unsupported`) e libera a imagem e a pilha.
