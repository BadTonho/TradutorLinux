# Parser Rust de perfis — TLPR v1.0

## Escopo da R23.1

R23.1 implementa a análise byte-oriented de `profile.json` em Rust e congela
uma representação TLPR para o adaptador e os testes diferenciais. A seleção de
produção continua em `load_profile`/`src/compat/profile.cpp`; não há mudança no
`runner`, na materialização, no filesystem, na seleção final do backend ou na
execução. A promoção do parser para produção é o escopo separado da R23.2.

O parser Rust não abre caminhos nem retém ponteiros. O C++ fornece o conteúdo
do JSON e o contexto transitório da identidade física esperada (`app_id`,
SHA-256 e versão). O C++ continua verificando existência, tipo regular,
symlink, confinamento, colisões físicas, permissões e demais condições do
filesystem.

## ABI C

[`rust_profile_parser.h`](../../include/tradutorlinux/ffi/rust_profile_parser.h)
exporta as funções sem estado `tl_profile_parse_v1_size` e
`tl_profile_parse_v1_fill`. Todos os ponteiros são caller-owned e válidos
somente durante a chamada:

- a entrada é lida exatamente pelo comprimento informado e tem limite de 1
  MiB;
- `tl_profile_identity_v1` contém ponteiro/comprimento para `app_id`,
  `app_sha256` e `app_version` esperados;
- `size` informa `output_required` sem produzir o wire;
- `fill` analisa novamente e só escreve depois de validar a capacidade;
- capacidade insuficiente não modifica o buffer de saída;
- a mensagem de erro é caller-owned, inclui o NUL em `error_required` e pode
  exigir uma segunda chamada com capacidade maior;
- Rust não devolve `String`, `Vec`, referência, ponteiro próprio ou unwind pela
  ABI, e panics são convertidos em `internal`.

`tl_profile_error_v1` contém `code`, `phase`, `input_offset` e `detail_value`.
`TL_PROFILE_ERROR_OFFSET_UNKNOWN` (`UINT64_MAX`) representa uma falha sem
posição de entrada específica. O status estruturado é a interface para
automação; a mensagem é apenas diagnóstico humano.

Os status estáveis são:

| Status | Uso |
|---|---|
| `success` | Perfil analisado e serializado. |
| `malformed` | JSON, schema, identidade, backend ou caminho lexical inválido. |
| `unsupported-format` | Schema fora de 1, 2 ou 3. |
| `invalid-argument` | Ponteiro/comprimento ou argumento inconsistente. |
| `buffer-too-small` | Mensagem ou buffer de saída insuficiente. |
| `input-too-large` | Entrada ou tabela acima do limite. |
| `output-too-large` | Wire ou tabelas acima do limite. |
| `internal` | Panic capturado, overflow não classificável ou falha interna. |

## Wire TLPR v1.0

Todos os inteiros são little-endian. O buffer começa com um cabeçalho de 128
bytes:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic `TLPR` |
| 4 | 2 | `major = 1` |
| 6 | 2 | `minor = 0` |
| 8 | 4 | `header_size = 128` |
| 12 | 8 | tamanho total do wire |
| 20 | 4 | quantidade de descritores, `4` |
| 24 | 8 | reservado, zero |
| 32 | 96 | quatro descritores de 24 bytes |

O restante do cabeçalho é reservado e deve permanecer zerado. Cada descritor
tem `offset: u64`, `count: u64`, `stride: u32` e `flags: u32`. A ordem dos
descritores é fixa: `info` (0), `files` (1), `dlls` (2) e `strings` (3). Um
descritor vazio é totalmente zero. Tabelas não vazias começam em offset
alinhado a 8, depois do cabeçalho, e seus intervalos não podem se sobrepor.

As tabelas têm estes registros:

- `info`, um registro de stride 96: `schema` em 0, `backend` em 4, `flags` em
  8, reservado em 12, e referências de 16 bytes para `app_id`, SHA-256,
  `app_version` e `min_version` em 16, 32, 48 e 64. O intervalo 80–96 é
  reservado e zero. A flag 1 indica que `backend` foi declarado; backend 0 é
  `native` e backend 1 é `proton`.
- `files`, stride 32: referência `source` em 0 e `target` em 16.
- `dlls`, stride 32: referência `module` em 0 e `source` em 16.
- `strings`, registros variáveis: stride 0 e flag
  `TL_PROFILE_WIRE_TABLE_FLAG_VARIABLE_RECORDS`. Cada registro começa com
  `length: u32` e `reserved: u32`, seguido dos bytes e padding zero até o
  próximo offset alinhado a 8.

Uma referência tem `offset: u64` e `length: u64`; `(0, 0)` representa campo
ausente. Referências não podem apontar para padding ou para o cabeçalho: devem
identificar exatamente o payload de um registro da tabela `strings`. Strings
são bytes, não exigem UTF-8, são deduplicadas por igualdade byte a byte e
preservam a primeira ocorrência na ordem: identidade, arquivos e DLLs. Os
campos reservados, padding e bytes não usados são sempre zero.

## JSON e validação

O modelo interno Rust é proprietário e usa `Vec<u8>` para todas as strings. O
schema aceito é exatamente o atual:

- schema 1: `schema` e `app_id`, com `files` opcional;
- schema 2: adiciona `dlls` opcional;
- schema 3: adiciona `backend`, com `kind` `native` ou `proton` e
  `min_version` opcional.

Campos obrigatórios e opcionais, tipos, escapes JSON simples, campos repetidos,
campos desconhecidos, vírgula final e conteúdo após o objeto são validados
antes da serialização. Não são aceitos novos campos nem expansão implícita do
schema. `app_id`, SHA-256, versões, módulos, fontes relativas e destinos
`C:\\...` seguem as regras do parser C++ atual. Módulos são normalizados para
minúsculas e recebem `.dll` quando ausente; duplicidades de arquivos, DLLs e
destinos normalizados são rejeitadas.

O contexto de identidade é comparado sem alterar o `Profile` C++ público:
`app_id` deve coincidir e SHA-256/versão, quando presentes no perfil, devem
coincidir com o contexto. O Rust não verifica se os arquivos existem nem
materializa os mapeamentos.

Limites do contrato:

- entrada JSON: 1 MiB;
- arquivos e DLLs: 65.535 registros cada;
- strings: 262.144 registros e 1 MiB de bytes agregados;
- wire serializado: 64 MiB;
- toda soma, multiplicação, alinhamento, conversão para `usize` e capacidade
  usa aritmética checked.

Versões TLPR diferentes de major 1/minor 0 são rejeitadas pelo decoder C++
como `unsupported-format`. O decoder também verifica magic, tamanho total,
descritores, offsets, contagens, strides, referências e reservados antes de
construir o `Profile`.

## Build e evidência

O módulo entra na static library somente quando `TL_BUILD_RUST=ON`, por meio do
mesmo Cargo sem crates externas e com `--locked --offline`. O build
`TL_BUILD_RUST=OFF` não compila, liga ou referencia a ABI de perfis; o parser
C++ e `load_profile` permanecem inalterados.

R23.1 inclui contratos C/C++, testes Rust do leitor/validação/serializer,
testes de buffers e sentinelas, concorrência e diferencial semântico contra
`load_profile`. A matriz validada em 2026-09-06 registrou Cargo `28/28`,
Clippy com `-D warnings`, CTest Rust Debug `771/771`, Release `771/771`, e
CTest OFF `734/734`. Sanitize passou no subconjunto reproduzível com os cinco
testes Proton reais e `x11_popup_smoke` excluídos por dependência ambiental;
Iphlpapi, GUI e HTTPS permaneceram skips opcionais. Essas evidências validam o
contrato e a equivalência; não constituem promoção de produção.
