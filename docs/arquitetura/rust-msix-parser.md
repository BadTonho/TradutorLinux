# Contrato do analisador MSIX/AppX em Rust

R22.1 adiciona uma análise estrutural opcional de pacotes MSIX/AppX. O
contrato é usado pela ABI C e pelos testes diferenciais; o backend de produção
continua sendo `src/package/msix.cpp` até R22.2. Nenhum caminho de `--report`,
`install`, `app run` ou do loader seleciona Rust nesta etapa.

## Escopo

O alvo é um pacote simples `.msix`/`.appx` representado integralmente por um
buffer fornecido pelo chamador. Rust não abre arquivos, não resolve caminhos e
não valida o PE interno. O resultado preserva a identidade do pacote, as
aplicações e o primeiro executável não vazio do manifesto como
`main_executable`. Bundles (`AppxBundleManifest.xml`), .NET/Mono, assinatura
Authenticode e formatos ZIP além de `stored` e raw DEFLATE são rejeitados.

O parser trata o pacote como entrada hostil: todos os offsets, tamanhos,
contagens e conversões para `usize` usam aritmética limitada. O limite do
buffer de entrada é 2 GiB; o manifesto descompactado é limitado a 16 MiB, a
entrada comprimida do manifesto a 64 MiB, as entradas ZIP a 10.000 e a saída
TLMS a 64 MiB.

## ABI C

O header público é
[`rust_msix_parser.h`](../../include/tradutorlinux/ffi/rust_msix_parser.h).
As funções `tl_msix_parse_v1_size` e `tl_msix_parse_v1_fill` são stateless:

1. `size` analisa o buffer e informa `output_required`;
2. `fill` analisa novamente e só escreve depois de confirmar a capacidade.

O chamador é proprietário da entrada, da saída e do buffer de mensagem. Um
buffer de saída insuficiente retorna `TL_MSIX_STATUS_BUFFER_TOO_SMALL`, informa
o tamanho necessário e não altera nenhum byte da saída. `error_required`
inclui o NUL final. O ponteiro de entrada nulo só é aceito com tamanho zero;
`error_required` e `output_required` são obrigatórios. Nenhum `String`, `Vec`,
ponteiro Rust, exceção ou unwind atravessa a fronteira. Panics são capturados e
convertidos em `internal`.

Os status estáveis são `success`, `truncated`, `malformed`,
`unsupported-format`, `unsupported-mechanism`, `invalid-argument`,
`buffer-too-small`, `input-too-large`, `output-too-large` e `internal`.
`tl_msix_error_v1` tem layout C de 24 bytes:

| Campo | Tipo | Conteúdo |
|---|---|---|
| `code` | `uint32_t` | categoria ZIP, XML, manifesto, limite ou wire |
| `phase` | `uint32_t` | entrada, EOCD, central, entrada, DEFLATE, XML, manifesto, serialização ou wire |
| `input_offset` | `uint64_t` | offset no pacote, ou `UINT64_MAX` quando não aplicável |
| `detail_value` | `uint64_t` | tamanho, índice, flag, método ou valor relacionado |

A mensagem caller-owned é diagnóstico humano; automação deve usar o status e o
registro estruturado.

## Wire format `TLMS` v1.0

O resultado é independente do layout de structs C/Rust. Todos os inteiros são
little-endian e todos os offsets são relativos ao início do buffer.

### Cabeçalho e descritores

O cabeçalho tem 128 bytes. Os campos são:

| Offset | Tamanho | Campo |
|---:|---:|---|
| 0 | 4 | magic ASCII `TLMS` |
| 4 | 2 | `major = 1` |
| 6 | 2 | `minor = 0` |
| 8 | 4 | `header_size = 128` |
| 12 | 8 | tamanho total do buffer |
| 20 | 4 | quantidade de descritores (`4`) |
| 24 | 4 | flags do cabeçalho, zero |
| 28 | 4 | reservado, zero |
| 32 | 96 | quatro descritores de 24 bytes |

Cada descritor contém `offset:u64`, `count:u64`, `stride:u32` e `flags:u32`.
Os IDs são `0=info`, `1=applications`, `2=strings` e `3=reserved`. Tabelas
não vazias começam em offset alinhado a 8. O descritor `reserved` deve estar
completamente vazio. Versões desconhecidas, flags não definidos, ranges
sobrepostos, overflow ou campos reservados não zerados são `unsupported-format`
na ABI do parser e `wire-format` no decoder C++.

### Registros

As tabelas fixas têm stride de 64 bytes. Cada campo abaixo é uma referência de
string de 16 bytes: `offset:u64` seguido de `length:u64`. Offset e comprimento
zero representam ausência.

| Tabela | Registro |
|---|---|
| `info` | offset 0 `package_name`, 16 `publisher`, 32 `version`, 48 `main_executable` |
| `applications` | offset 0 `id`, 16 `executable`, 32 `display_name`, 48 `entry_point` |

`info` tem exatamente um registro. Aplicações são emitidas na ordem do
manifesto. Todos os bytes dos registros são inicialmente zerados; os quatro
campos de cada registro são então preenchidos.

A tabela `strings` usa `stride=0` e a flag
`TL_MSIX_WIRE_TABLE_FLAG_VARIABLE_RECORDS`. Cada registro é:

```text
u32 length
u32 reserved = 0
u8[length] bytes
padding até o próximo offset múltiplo de 8, todo zero
```

Strings são bytes, não texto UTF-8 obrigatório e não dependem de NUL. Valores
repetidos são deduplicados por bytes e a primeira ocorrência é mantida. A
ordem é identidade (`package_name`, `publisher`, `version`, executável
principal), seguida pelos campos de cada aplicação (`id`, executável, nome
visual e entry point). A referência aponta para o primeiro byte dos dados do
registro, não para o cabeçalho de tamanho.

O decoder C++ valida magic, versão, tamanho exato, descritores, alinhamento,
strides, contagens, ranges, padding, referências e registros reservados antes
de converter para `AppxPackageInfo`.

## ZIP e manifesto

Rust localiza o EOCD dentro da janela de comentário ZIP, rejeita EOCD/central
directory truncados, Zip64, multipartes, encryption, data fora do pacote,
CRC inválido, links simbólicos e métodos diferentes de `stored`/raw DEFLATE.
Tamanhos comprimidos e descompactados, soma dos tamanhos, contagem e nomes
passam pelos limites fixados no header. O DEFLATE usa uma ponte C mínima para a
zlib já vinculada pelo projeto; Rust continua dono do modelo e do buffer de
saída.

Nomes ZIP são bytes: NUL, nomes absolutos, prefixo de unidade, `.`/`..`,
segmentos vazios ambíguos e colisões após trocar `\` por `/` são rejeitados.
O mesmo endurecimento foi aplicado ao inspector C++ para que o diferencial
tenha a mesma decisão de aceitação.

O parser XML é limitado e não carrega DTD nem entidades externas. Ele aceita
BOM, comentários, CDATA, namespaces locais, aspas simples ou duplas, entidades
predefinidas e entidades numéricas; não exige UTF-8 para os bytes de atributos.
Há limites de profundidade e atributos. A raiz deve ser `Package`, o manifesto
deve existir uma única vez e aplicações com executável vazio preservam a
semântica existente. Um caminho de executável não pode ser absoluto,
traversal, NUL ou diretório; a existência física e a validação PE continuam
fora deste marco.

## Build e validação

`TL_BUILD_RUST=OFF` permanece o padrão e não compila nem liga o módulo Rust.
Com Rust habilitado, o static library existente inclui os módulos do contrato,
parser e ponte DEFLATE; nenhum crate externo ou alteração em `Cargo.lock` é
necessário. O adaptador C++ fica disponível para os testes e para a etapa de
integração futura, mas não é chamado pelo runner em R22.1.

O contrato é protegido por testes C e C++ de largura, offsets e constantes;
testes Rust cobrem leitor little-endian, ranges, overflow, normalização,
entidades e invariantes do serializer. O teste diferencial constrói pacotes
stored/DEFLATE, compara semanticamente `inspect_msix_package`, verifica
`size`/`fill`, sentinelas, wire inválido e chamadas concorrentes. Os testes de
produção C++ existentes continuam sendo executados separadamente para
confirmar que o backend atual não mudou.

R22.2 poderá reutilizar exatamente este header, o wire TLMS e o decoder para
selecionar Rust no inspection/report e depois na instalação. Essa promoção
exigirá novo gate de integração; R22.1 não declara compatibilidade adicional
para nenhum aplicativo.
