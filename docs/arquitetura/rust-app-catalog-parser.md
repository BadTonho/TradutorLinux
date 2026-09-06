# Parser Rust do catálogo de aplicativos — TLAC v1.0

R24.1 define a análise Rust do `library.json` usado pelo `AppCatalog`. Nesta
etapa o parser Rust é acessível somente pela ABI e pelos testes. `AppCatalog`,
`load_from_file`, a CLI, a GUI, o loader, o Proton e a execução continuam em
C++.

## ABI

O header público é
[`rust_app_catalog_parser.h`](../../include/tradutorlinux/ffi/rust_app_catalog_parser.h).
As funções `tl_app_catalog_parse_v1_size` e
`tl_app_catalog_parse_v1_fill` são `extern "C"`, sem estado e sem ownership
compartilhado. A entrada, o buffer de saída, o erro e a mensagem pertencem ao
chamador; Rust não retém ponteiros nem devolve `String`, `Vec`, referências ou
unwind.

`size` analisa a entrada e publica o tamanho necessário. `fill` analisa a
entrada novamente, verifica a capacidade e só então publica o buffer. Se a
capacidade for insuficiente, o buffer de saída permanece inalterado. Mensagens
incluem o byte NUL em `error_required`; panics são convertidos em `internal`.

Os status estáveis são `success`, `malformed`, `unsupported-format`,
`invalid-argument`, `buffer-too-small`, `input-too-large`,
`output-too-large` e `internal`. O erro estruturado contém `code`, `phase`,
`input_offset` e `detail_value`; `UINT64_MAX` representa offset desconhecido.

## JSON aceito

O documento raiz deve ser um objeto com exatamente os campos `version` e
`apps`. A versão aceita é o número inteiro `1`. Campos desconhecidos ou
repetidos, tipos incorretos, trailing comma e bytes depois do objeto são
rejeitados.

Cada aplicativo exige `id` e `executable_path`. Os campos opcionais são
`name`, `prefix_path`, `icon_path`, `working_directory`, `app_sha256`,
`app_version`, `created_at`, `cpu_limit_seconds`, `memory_limit_mib` e `args`.
Strings opcionais ausentes são vazias, números ausentes são zero e `args`
ausente é um array vazio. IDs repetidos exatamente são rejeitados.

Os valores são byte-oriented: bytes não ASCII não precisam formar UTF-8. São
aceitos os escapes JSON padrão e `\uXXXX`; pares surrogate válidos são
convertidos para UTF-8 e surrogates incompletos ou escapes inválidos falham.
Bytes de controle literais são rejeitados. O parser não acessa filesystem e
não decide existência, symlink, permissões, confinamento ou materialização.

Limites do contrato:

| Recurso | Limite |
|---|---:|
| entrada JSON | 4 MiB |
| aplicativos | 65.535 |
| argumentos totais | 262.144 |
| strings únicas | 1.048.576 |
| string individual | 1 MiB |
| bytes de strings únicas | 16 MiB |
| saída TLAC | 64 MiB |

Todas as somas, multiplicações, alinhamentos e conversões são checked.

## Wire TLAC v1.0

Todos os inteiros usam little-endian. O cabeçalho tem 128 bytes:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic `TLAC` |
| 4 | 2 | major `1` |
| 6 | 2 | minor `0` |
| 8 | 4 | `header_size`, sempre 128 |
| 12 | 8 | `total_size` |
| 20 | 4 | quantidade de descritores, sempre 4 |
| 24 | 4 | flags reservados, zero |
| 28 | 4 | reservado, zero |
| 32 | 96 | quatro descritores de 24 bytes |

Cada descritor contém `offset:u64`, `count:u64`, `stride:u32` e `flags:u32`.
Os IDs fixos são `info=0`, `apps=1`, `args=2` e `strings=3`. Tabelas não
vazias começam em offsets alinhados a 8 bytes; descritores de tabelas vazias
usam offset e contagem zero. `strings` usa `stride=0` e a flag de registros
variáveis; as demais tabelas usam flags zero.

`info` tem um registro de 32 bytes, com versão do catálogo em 0, flags em 4,
contagem de aplicativos em 8, contagem total de argumentos em 16 e oito bytes
reservados em 24.

`apps` usa stride de 192 bytes. Os offsets 0, 16, 32, 48, 64, 80, 96, 112 e
128 são referências de 16 bytes para, respectivamente, `id`, `name`,
`executable_path`, `prefix_path`, `icon_path`, `working_directory`,
`app_sha256`, `app_version` e `created_at`. A referência tem
`payload_offset:u64` e `length:u64`. Os offsets 144 e 152 contêm o índice e a
contagem de argumentos; 160 contém `cpu_limit_seconds`; 168 contém
`memory_limit_mib`; os 16 bytes de 176 a 191 são reservados e zero.

`args` usa stride de 16 bytes, cada registro contendo uma referência de
string. Os registros seguem a ordem dos aplicativos e dos argumentos no JSON.

`strings` é uma sequência de registros variáveis. Cada registro começa com
`length:u32` e `reserved:u32=0`, seguido dos bytes e de padding zero até o
próximo alinhamento de 8 bytes. Referências apontam para o payload, nunca para
o cabeçalho ou padding. Strings vazias usam `(offset=0,length=0)` e não geram
registro. Strings não vazias são deduplicadas por bytes, preservando a primeira
ocorrência na ordem dos nove campos de cada aplicativo e, depois, dos
argumentos.

O serializer inicia todos os registros zerados e valida que cada referência
aponte para payload dentro de `total_size`. O modelo interno Rust é separado
do wire e usa `Vec<u8>` e índices apenas durante o parsing/serialização.

## Build e escopo

`TL_BUILD_RUST=ON` inclui `catalog_contract.rs` e `catalog_parser.rs` no
static library existente. `TL_BUILD_RUST=OFF` continua sendo o caminho C++
sem link, referência operacional ou seleção do parser Rust. R24.2 adicionará o
decoder/adaptador C++ e o diferencial semântico; R24.3 decidirá a promoção.
