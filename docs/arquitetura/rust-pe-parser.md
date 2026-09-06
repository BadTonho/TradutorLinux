# Contrato FFI do parser PE em Rust

Este documento define a R21.1. Ele congela a fronteira entre um futuro parser
PE em Rust e o C++ do TradutorLinux; não implementa o parser e não altera o
loader, o `--report` ou o fluxo `app run`.

O contrato é específico para o alvo atual: PE32+ AMD64 em Linux x86-64. A
implementação Rust será responsável somente por analisar bytes e construir o
resultado normalizado. O C++ continuará responsável por mapear imagens,
aplicar relocations na memória, resolver endereços, preparar ABI e executar o
convidado.

## ABI C

O contrato público fica em
[`rust_pe_parser.h`](../../include/tradutorlinux/ffi/rust_pe_parser.h).
Todas as funções usam somente inteiros de largura fixa e ponteiros para
buffers pertencentes ao chamador. Nenhuma exceção C++, panic Rust ou unwind
pode atravessar a fronteira.

As funções são stateless:

1. `tl_pe_parse_v1_size` analisa a entrada e retorna o tamanho necessário;
2. `tl_pe_parse_v1_fill` analisa a mesma entrada e preenche o buffer fornecido.

O chamador deve manter a entrada imutável entre as chamadas. A segunda chamada
não reutiliza estado da primeira. Se o buffer de saída for insuficiente,
`output_required` recebe o tamanho real e nenhum byte de saída é alterado.
Em erro de parsing, `output_required` é zero.

`input == NULL` só é permitido com comprimento zero; nesse caso a entrada é
tratada como um arquivo vazio e resulta em `truncated`. `output == NULL` só é
permitido com capacidade zero. `error` e `error_required` são obrigatórios.
O buffer textual de erro segue exatamente as regras de ownership, tamanho,
terminação NUL e truncamento descritas em [`rust-ffi.md`](rust-ffi.md).

Os valores `0..5` de `tl_pe_status_t` preservam a ordem de `ParseStatus` em
[`pe_reader.hpp`](../../include/tradutorlinux/pe/pe_reader.hpp). Os valores
posteriores representam falhas da própria fronteira FFI ou da serialização e
não são códigos Win32.

`tl_pe_error_v1` é um registro fixo de 24 bytes no alvo atual:

| Campo | Tipo | Significado |
|---|---|---|
| `code` | `uint32_t` | Categoria estável do erro |
| `phase` | `uint32_t` | Etapa do parser/serializador |
| `input_offset` | `uint64_t` | Offset no arquivo ou `UINT64_MAX` |
| `detail_value` | `uint64_t` | RVA, tamanho, índice ou valor relacionado |

A mensagem textual é apenas uma explicação para humanos. Automação deve usar
`status`, `code`, `phase` e os campos numéricos.

## Wire format `TLPE` v1.0

O resultado é um buffer binário independente do layout de structs C ou Rust.
Todos os inteiros são little-endian e todos os offsets são relativos ao início
do próprio buffer.

### Cabeçalho

O cabeçalho tem 416 bytes:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic ASCII `TLPE` |
| 4 | 2 | major (`1`) |
| 6 | 2 | minor (`0`) |
| 8 | 4 | tamanho do cabeçalho (`416`) |
| 12 | 8 | tamanho total do buffer |
| 20 | 4 | número de descritores (`16`) |
| 24 | 8 | reservado, zero |
| 32 | 384 | 16 descritores de tabela de 24 bytes |

Cada descritor contém `offset:u64`, `count:u64`, `stride:u32` e
`flags:u32`. Tabelas fixas usam stride não zero; a tabela de strings usa
`stride=0` e `TL_PE_WIRE_TABLE_FLAG_VARIABLE_RECORDS`.

O descritor reservado 15 deve permanecer vazio. Uma versão major ou minor não
suportada deve resultar em `unsupported-format`; nenhum campo desconhecido
pode ser interpretado silenciosamente.

### Tabelas

As tabelas são flat. Índices e intervalos entre tabelas são representados por
offsets e contagens, nunca por ponteiros:

| ID | Tabela | Stride |
|---:|---|---:|
| 0 | `info` | 136 |
| 1 | `sections` | 40 |
| 2 | `strings` | variável |
| 3 | `import_dlls` | 40 |
| 4 | `import_symbols` | 32 |
| 5 | `delay_import_dlls` | 40 |
| 6 | `delay_import_symbols` | 32 |
| 7 | `exports` | 48 |
| 8 | `tls_callbacks` | 8 |
| 9 | `runtime_functions` | 24 |
| 10 | `unwind_infos` | 72 |
| 11 | `unwind_codes` | 8 |
| 12 | `unwind_epilogs` | 8 |
| 13 | `reloc_blocks` | 32 |
| 14 | `reloc_entries` | 8 |

Os campos de cada registro seguem, na ordem e nos tipos, os campos de
`PeInfo`, `SectionInfo`, `ImportedDll`, `ImportedSymbol`, `ExportedSymbol`,
`TlsDirectoryInfo`, `RuntimeFunction`, `UnwindInfo`, `UnwindCode`,
`UnwindEpilog`, `BaseRelocBlock` e `BaseRelocEntry` do parser C++ atual.
Booleanos são `u8`; flags e campos reservados ocupam posições fixas e devem
ser zero quando não usados. O registro `info` contém também os sete pares de
RVA/tamanho dos diretórios e os campos escalares do TLS.

Uma referência a string contém `offset:u64`, `length:u32` e `reserved:u32`.
Offset zero e comprimento zero significam ausência. A tabela de strings é uma
sequência de registros `[length:u32][reserved:u32][bytes][padding]`, alinhada
a 8 bytes; a referência aponta para o primeiro byte da string. Os bytes são
preservados exatamente como aparecem no PE e não precisam ser UTF-8.

`import_dlls` e `delay_import_dlls` apontam para intervalos nas respectivas
tabelas de símbolos. `runtime_functions` aponta por índice para
`unwind_infos`; cada `unwind_info` aponta para seus códigos e epílogos.
`reloc_blocks` aponta para seus `reloc_entries`.

O consumidor deve validar magic, versão, tamanho total, offsets, strides,
contagens, alinhamento, intervalos aninhados e overflow antes de ler qualquer
registro.

## Limites

Os limites existentes no parser C++ são parte do contrato: 1.024 DLLs de
imports por tabela, 4.096 símbolos por DLL, 4.096 blocos de relocation,
65.536 runtime functions, 65.536 funções/nomes de export e 65.534 bytes por
string. O limite de seções é `UINT16_MAX`, conforme o campo COFF.

Para cobrir lacunas do parser atual, a R21.1 fixa no header 65.536 callbacks
TLS e 256 MiB para o resultado serializado. Qualquer contagem, soma de
intervalos, conversão para `usize` ou cálculo de tamanho que exceda esses
limites falha de modo controlado com `input-too-large`, `output-too-large` ou
`malformed`, conforme a etapa.

Diretórios PE continuam limitados pelos campos `u32` do formato e precisam
caber simultaneamente no arquivo e na imagem lógica. Não há uma conversão de
strings para UTF-8 nem uma permissão para NUL substituir limites explícitos.

## Testes e evolução

R21.1 protege o contrato com:

- compilação do header tanto em C quanto em C++;
- asserts de tamanhos, offsets, constantes e strides no lado C++;
- asserts `repr(C)`, tamanho e alinhamento do diagnóstico no lado Rust;
- vetores sintéticos para imagem mínima, imports, exports/forwarders,
  delay-imports, TLS, unwind e relocations;
- rejeição de magic/versão inválidos, offsets fora do buffer, strides
  incompatíveis, multiplicações com overflow e strings truncadas.

Esses testes verificam o contrato e o wire format, não afirmam que o parser
Rust já funciona. A implementação Rust e a comparação contra
`parse_pe` começam na R21.2. Até a promoção, `TL_BUILD_RUST=OFF` permanece o
padrão, nenhum símbolo FFI é chamado pelo runtime e a matriz de
compatibilidade não muda.
