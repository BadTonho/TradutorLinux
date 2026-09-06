# Contrato FFI do parser PE em Rust

Este documento define o contrato congelado em R21.1, registra a implementação
de R21.2 e a integração seletiva de R21.3. Com `TL_BUILD_RUST=ON`, o parser
Rust é canônico somente na execução direta de `--report`; o loader, o fluxo
`app run` e o build sem Rust continuam usando o parser C++.

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

Os campos de cada registro têm os offsets abaixo. Todos os offsets são dentro
do registro, não offsets de structs C ou Rust. Campos reservados são zero.

| Registro | Campos little-endian por offset |
|---|---|
| `info` | `0:u32 flags`, `4:u16 machine`, `6:u16 sections`, `8:u32 entry`, `12:u64 image_base`, `20:u32 section_alignment`, `24:u32 size_of_image`, `28:u32 size_of_headers`, `32:u16 subsystem`, `34:u16 reserved`, `36..92` sete pares `(rva:u32,size:u32)` na ordem import, export, resource, exception, relocation, delay-import, TLS, `92:u32 export_ordinal_base`, `96:u64 tls_start`, `104:u64 tls_end`, `112:u64 tls_index`, `120:u64 tls_callbacks`, `128:u32 tls_zero_fill`, `132:u32 tls_characteristics` |
| `sections` | `0:u64 name_offset`, `8:u32 name_length`, `12:u32 reserved`, `16:u32 virtual_address`, `20:u32 virtual_size`, `24:u32 raw_data_pointer`, `28:u32 raw_data_size`, `32:u32 characteristics`, `36:u32 reserved` |
| import/delay DLL | `0:u64 name_offset`, `8:u32 name_length`, `12:u32 reserved`, `16:u64 symbols_index`, `24:u64 symbols_count`, `32:u64 reserved` |
| import/delay symbol | `0:u32 flags`, `4:u16 ordinal`, `6:u16 reserved`, `8:u64 name_offset`, `16:u32 name_length`, `20:u32 reserved`, `24:u32 iat_rva`, `28:u32 reserved` |
| `exports` | `0:u32 flags`, `4:u16 ordinal`, `6:u16 reserved`, `8:u32 rva`, `12:u32 reserved`, `16:u64 name_offset`, `24:u32 name_length`, `28:u32 reserved`, `32:u64 forwarder_offset`, `40:u32 forwarder_length`, `44:u32 reserved` |
| `tls_callbacks` | `0:u64 callback_va` |
| `runtime_functions` | `0:u32 begin_rva`, `4:u32 end_rva`, `8:u32 unwind_info_rva`, `12:u64 unwind_info_index`, `20:u32 reserved` |
| `unwind_infos` | `0:u8 version`, `1:u8 pe_flags`, `2:u8 prolog_size`, `3:u8 frame_register`, `4:u8 frame_offset`, `5:u8 wire_flags`, `6:u16 reserved`, `8:u64 codes_index`, `16:u64 codes_count`, `24:u64 epilogs_index`, `32:u64 epilogs_count`, `40:u32 handler_rva`, `44:u32 handler_data_rva`, `48:u32 chained_begin_rva`, `52:u32 chained_end_rva`, `56:u32 chained_unwind_info_rva`, `60:u32 reserved`, `64:u64 reserved` |
| `unwind_codes` | `0:u8 code_offset`, `1:u8 operation`, `2:u8 operation_info`, `3:u8 reserved`, `4:u32 operand` |
| `unwind_epilogs` | `0:u32 begin_rva`, `4:u32 end_rva` |
| `reloc_blocks` | `0:u32 page_rva`, `4:u32 reserved`, `8:u64 entries_index`, `16:u64 entries_count`, `24:u64 reserved` |
| `reloc_entries` | `0:u16 type`, `2:u16 offset`, `4:u32 reserved` |

Os flags de `info` são `PE32_PLUS=1` e `DLL=2`. Os flags de importação,
exportação e unwind são os constantes no header público. `pe_flags` preserva
os flags PE de `UNWIND_INFO`; `wire_flags` usa
`EXTENDED_SET_FPREG=1` e `CHAINED_FUNCTION=2`. O registro `info` contém os
sete pares de RVA/tamanho dos diretórios, o ordinal base de exports e os
campos escalares do TLS.

Uma referência a string contém `offset:u64`, `length:u32` e `reserved:u32`.
Offset zero e comprimento zero significam ausência. A tabela de strings é uma
sequência de registros `[length:u32][reserved:u32][bytes][padding]`, alinhada
a 8 bytes; a referência aponta para o primeiro byte da string. Os bytes são
preservados exatamente como aparecem no PE e não precisam ser UTF-8.

`import_dlls` e `delay_import_dlls` apontam para intervalos, por índice, nas
respectivas tabelas de símbolos. `runtime_functions` aponta por índice para
`unwind_infos`; cada `unwind_info` aponta para seus códigos e epílogos.
`reloc_blocks` aponta para seus `reloc_entries`.

O produtor canônico emite tabelas não vazias na ordem dos IDs, alinhando cada
início a 8 bytes. A tabela de strings é deduplicada por bytes: a primeira
ocorrência durante a travessia de seções, imports, delay-imports e exports
define o registro reutilizado pelas referências seguintes. O registro de
string é `[length:u32][reserved:u32][bytes][padding]`; uma referência zero
representa ausência, enquanto uma referência não zero com comprimento zero
representa uma string vazia.

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

## Implementação R21.2

O parser Rust implementado em `src/rust/pe_parser.rs` mantém um modelo interno
próprio e só publica o resultado através das funções C acima. A análise cobre
PE32+ AMD64, headers, seções, imports, delay-imports, exports e forwarders,
TLS, relocations e `UNWIND_INFO` V1/V2 com seus códigos, epílogos, handlers e
encadeamentos. O serializer faz primeiro um plano de tamanho checked e só
preenche a saída depois de confirmar a capacidade recebida.

O C++ continua sendo o oráculo diferencial. A R21.3 compartilha o decoder TLPE
entre o adaptador de produção e os testes; ele valida novamente todo o buffer
antes de convertê-lo em `PeInfo`. Nenhum resultado Rust é consumido pelo
loader ou por `app run`.

## Integração R21.3 no `--report`

O caminho Rust é selecionado apenas quando `CommandMode::DirectRun` e
`report_only` são verdadeiros. O adaptador C++ chama `size` e `fill` com
buffers caller-owned, repete a capacidade da mensagem quando necessário e
decodifica o TLPE sem expor layout de Rust. A análise é stateless e ocorre
antes do registro de módulos e da resolução usada para compor o relatório;
uma falha não mapeia a imagem nem executa o entry point.

Falhas Rust não fazem fallback silencioso para C++. `truncated` e `malformed`
retornam `MalformedPe` (`4`); arquitetura, formato e mecanismo não suportados
retornam `Unsupported` (`5`). Argumentos inválidos, buffers, limites,
inconsistências do wire, panic e falhas internas retornam `InternalError`
(`70`). O stdout do relatório e as mensagens normais permanecem iguais ao
relatório C++.

No trace, os eventos derivados do resultado Rust carregam
`backend="rust"`. Um `parse-failed` Rust também carrega `code`, `phase`,
`input-offset` e `detail-value`, além de `status` e `detail`. O `app run`
(inclusive `app run --report`) e todo o caminho `TL_BUILD_RUST=OFF` não usam o
adaptador nem emitem esse backend.

## Testes e evolução

R21.1 protege o contrato com:

- compilação do header tanto em C quanto em C++;
- asserts de tamanhos, offsets, constantes e strides no lado C++;
- asserts `repr(C)`, tamanho e alinhamento do diagnóstico no lado Rust;
- vetores sintéticos para imagem mínima, imports, exports/forwarders,
  delay-imports, TLS, unwind e relocations;
- rejeição de magic/versão inválidos, offsets fora do buffer, strides
  incompatíveis, multiplicações com overflow e strings truncadas.

Esses testes verificam o contrato e o wire format. A R21.2 acrescenta o
corpus diferencial e os testes de robustez do parser Rust, incluindo as duas
chamadas stateless, buffers com sentinelas, erros estruturados, strings
deduplicadas, concorrência e entradas malformadas bounded. Até a promoção,
`TL_BUILD_RUST=OFF` permanece o padrão. Com Rust habilitado, somente o
`--report` direto chama a ABI; o loader e a matriz de compatibilidade de
execução não mudam.
