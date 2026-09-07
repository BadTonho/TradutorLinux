# Parser Rust do catálogo de aplicativos — TLAC v1.0

R24.1 define a análise Rust do `library.json` usado pelo `AppCatalog`. R24.2
acrescenta o adaptador C++ interno e o decoder TLAC reutilizável. Em R24.3,
com `TL_BUILD_RUST=ON`, `AppCatalog::load_from_file` usa Rust como parser
canônico para catálogos existentes, sem fallback para o parser C++. A escrita,
filesystem, CLI, GUI, loader, Proton e execução continuam em C++. O build
`TL_BUILD_RUST=OFF` permanece explicitamente C++.

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
vazias começam em offsets alinhados a 8 bytes. Em uma tabela vazia, offset e
contagem são zero, mas o descritor mantém o stride e as flags canônicos:
`apps` mantém stride 192 e flags zero, `args` mantém stride 16 e flags zero, e
`strings` mantém stride zero e a flag de registros variáveis. `info` nunca é
vazio.

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

## Decoder C++ e diferencial — R24.2

`decode_tlac_v1` lê o buffer apenas com acessores little-endian; casts para
estruturas host não são usados. Antes de construir qualquer `AppEntry`, o
decoder verifica magic, versão, tamanho total, descritores, strides, flags,
alinhamento, limites, overflow, ordem e sobreposição das tabelas.

As tabelas fixas devem seguir `info`, `apps`, `args` e `strings`. As lacunas
entre tabelas e o padding dos registros de strings precisam estar zerados.
Cada registro de string é não vazio, único por bytes e termina dentro do
buffer; uma referência não vazia deve coincidir exatamente com um payload
registrado. O decoder também confirma contagens do `info`, intervalos
contíguos de argumentos, IDs válidos e IDs únicos.

O adaptador `parse_app_catalog_rust` chama `size` e `fill` com buffers
caller-owned, repete a chamada quando `error_required` excede a capacidade,
exige NUL na mensagem e trata divergência entre `size` e `fill` como falha
interna. O resultado é materializado em um vetor temporário e só é publicado
após toda a validação; `AppCatalog::add_app` não é chamado.

Nos vetores válidos, o decoder compara semanticamente todos os campos com o
oráculo C++ `AppCatalog::load_from_file`. Entradas JSON inválidas e mutações
TLAC são verificadas por rejeição estruturada e atomicidade, sem exigir que o
parser C++ permissivo reproduza a mesma rejeição.

## Integração em `AppCatalog` — R24.3

`load_from_file` limpa o catálogo antes de iniciar. Arquivo ausente, erro de
abertura ou falha de leitura continuam sendo tratados pelo C++ e não chamam o
parser Rust. Quando o arquivo está disponível, o ramo Rust verifica o limite
de 4 MiB antes de alocar, lê o conteúdo integralmente em buffer caller-owned,
chama `parse_app_catalog_rust` e só move o vetor para `apps_` depois de o TLAC,
as referências, os limites e todos os registros estarem validados. Conteúdo
inválido ou falha interna retorna `false` e deixa o catálogo vazio; o parser
C++ não é executado como fallback no build ON.

Falhas de transporte, status desconhecido, divergência de `size`/`fill`,
decoder ou wire inválido são falhas internas. Rejeições de conteúdo e limites
seguem o retorno booleano existente. O caminho C++ original fica isolado no
build `TL_BUILD_RUST=OFF`, sem referência operacional ao adaptador Rust.

Quando o trace é solicitado e o componente `runtime` está habilitado, o ramo
Rust emite `catalog-parse` com `backend="rust"`, `parser-status` e a contagem
de aplicativos. Sucesso usa nível `Info`; rejeições de conteúdo/limite usam
`Warning`; falhas internas usam `Error`. Rejeições também incluem `code`,
`phase`, `input-offset`, `detail-value` e uma mensagem diagnóstica. Sem
`--trace` a saída normal não muda, e arquivo inexistente não gera evento Rust.

## Build e escopo

`TL_BUILD_RUST=ON` inclui `catalog_contract.rs`, `catalog_parser.rs` e o
adaptador/decoder C++ no static library existente. `TL_BUILD_RUST=OFF`
continua sendo o caminho C++ sem link, referência operacional ou seleção do
parser Rust. A promoção R24.3 não altera `AppEntry`, o schema persistido,
`Cargo.lock`, loader ou Proton.
