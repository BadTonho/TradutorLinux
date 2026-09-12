# Matriz de compatibilidade — backends

Este documento contém as políticas e evidências dos backends Rust/C++, MSIX/AppX, perfis, catálogo e Proton originalmente registradas na matriz.

## Política de backend do parser PE — R21.3–R21.5

Com `TL_BUILD_RUST=ON`, a execução direta de `--report` usa o resultado Rust
como canônico e compara sua semântica com o parser C++ nos testes. O adaptador
decodifica o TLPE com validação de magic, versão, offsets, strides,
alinhamento, referências, campos reservados, overflow e limites, mantendo
strings como bytes. Não existe fallback silencioso: falhas de parsing são
controladas e aparecem no trace com os campos estruturados da ABI.

Essa promoção não altera o estado de compatibilidade de nenhum aplicativo.
`app run --report`, execução direta normal, instalação, Proton, o loader das
DLLs dependentes e `TL_BUILD_RUST=OFF` permanecem no parser C++. No `app run`
nativo sem `--report`, somente a imagem principal usa o resultado Rust; o
`GuestModuleGraph` continua usando C++ para as DLLs. O `PeInfo` Rust é entregue
ao mesmo fluxo C++ de mapeamento e execução, sem alteração de `mmap`,
relocations, imports, ABI ou entry point.

O relatório Rust não mapeia imagem nem executa o entry point;
`execution: not-attempted` continua obrigatório. Falhas Rust não têm fallback
silencioso e encerram o `app run` antes de mapear ou executar.

Na R21.5, a seleção é uma política única do runner: Rust é canônico somente
para a imagem principal dos caminhos promovidos com `TL_BUILD_RUST=ON`. O C++
permanece produção para DLLs dependentes, Proton, instalação, `app run
--report`, execução direta normal e para o build `TL_BUILD_RUST=OFF`, que é a
variante C++ explícita e padrão. Nos caminhos promovidos, o C++ é apenas o
oráculo diferencial; nenhum resultado Rust é substituído silenciosamente.

O mapeamento de saída é `4` para `truncated`/`malformed`, `5` para arquitetura,
formato ou mecanismo não suportados e `70` para falha interna da ABI, limites,
buffer ou wire inválido. Os vetores e testes diferenciais de imports,
delay-imports, exports/forwarders, TLS, unwind V1/V2 e relocations são a
evidência do contrato, não uma declaração de suporte funcional.

## Política de backend MSIX/AppX — R22.1–R22.2

R22.1 implementou o parser Rust e o wire `TLMS` v1.0 para testes de contrato e
comparação diferencial. A partir de R22.2, com `TL_BUILD_RUST=ON`, Rust é
canônico no `--report` direto e no `install` de pacotes. A seleção usa as
extensões `.msix`, `.appx`, `.msixbundle` e `.appxbundle` antes da validação
ZIP, para que entradas truncadas e inválidas recebam o diagnóstico correto.

O relatório de um pacote válido mantém stdout, stderr sem trace e exit code.
Com `--trace`, aparece `package-parse` com `backend="rust"`; falhas incluem
`status`, `code`, `phase`, `input-offset` e `detail-value`. Não existe fallback
de produção para o parser C++ quando a análise Rust falha.

No `install`, Rust valida o pacote e C++ continua responsável por criar o
prefixo, revalidar as condições físicas, extrair os arquivos, verificar o
executável PE32+ AMD64 e salvar o catálogo. A extração usa o executável
principal validado por Rust; falha ou divergência encerra a instalação sem
substituir o resultado Rust e sem cadastro.

Bundles, ZIP64 multipartes, encryption, .NET/Mono, Authenticode, links,
traversal, NUL, colisões normalizadas, DTD e entidades externas continuam fora
do escopo. ZIP64 de disco único é lido dentro dos limites de segurança; o
limite agregado descompactado de 512 MiB continua valendo.
Rust não acessa o filesystem e não valida o PE interno do pacote.

`app run`, `app run --report`, execução direta normal, Proton, DLLs dependentes
e `TL_BUILD_RUST=OFF` continuam usando C++. O build OFF é uma variante C++
explícita e padrão, sem link operacional ou símbolos Rust. A promoção não
altera o nível funcional ou declara suporte a nenhum aplicativo adicional.

O mapeamento de erros Rust para o CLI é `4` para `truncated`/`malformed`, `5`
para `unsupported-format`/`unsupported-mechanism` e `70` para argumentos,
buffers, limites, wire inválido, panic ou falha interna.

## Parser Rust de perfis — R23.1–R23.2

R23.1 implementou o contrato TLPR v1.0 e a comparação diferencial do
`profile.json`. Em R23.2, com `TL_BUILD_RUST=ON`, Rust é canônico dentro de
`load_profile` para todos os consumidores atuais, incluindo `app run` e
`app run --report`. O C++ verifica antes a presença, o tipo regular, a leitura
e o limite do arquivo; depois do TLPR, continua responsável por existência,
tipo regular, symlink, confinamento, colisões físicas, permissões,
materialização e seleção de backend.

O wire tem cabeçalho de 128 bytes, tabelas `info`/`files`/`dlls`/`strings`,
inteiros little-endian, alinhamento de 8 bytes, referências por offset/tamanho
e strings binárias deduplicadas. Entrada acima de 1 MiB, saída acima de 64 MiB,
overflow, referências inválidas ou campos reservados não zerados são
rejeitados. A ABI usa `size`/`fill`, buffers e mensagens caller-owned, e não
expõe layout Rust.

O perfil ausente continua sendo detectado exclusivamente pelo C++ e não chama
Rust. Rejeições de conteúdo — JSON/schema/identidade/caminho lexical inválido,
`unsupported-format`, `input-too-large` ou `output-too-large` — retornam
`ProfileStatus::Invalid` e preservam o fallback genérico atual. Falhas internas
da ABI, argumentos, buffers, decoder TLPR, panic ou status inesperado retornam
`ProfileStatus::InternalError` sem fallback. O evento `compat-profile` recebe
`backend="rust" parser-status="success"` quando há sucesso Rust; rejeições
também carregam `code`, `phase`, `input-offset` e `detail-value`.

`TL_BUILD_RUST=OFF` é a variante C++ explícita e padrão: não compila, liga ou
referencia os símbolos Rust e não recebe campos Rust no trace. A promoção não
altera `Profile`, TLPR, `Cargo.lock`, loader, materializador ou o nível de
compatibilidade declarado para qualquer aplicativo.

## Parser Rust do catálogo — R24.1–R24.3

R24.1 define e testa o contrato TLAC v1.0 para `library.json`; R24.2 adiciona
o adaptador e decoder C++ para o diferencial semântico. Em R24.3, com
`TL_BUILD_RUST=ON`, Rust é o parser canônico de catálogos existentes dentro de
`AppCatalog::load_from_file`. A assinatura pública, `AppEntry`, o formato
persistido e o nível de compatibilidade dos aplicativos não mudam.

O arquivo é lido e limitado em C++, o resultado Rust é validado pelo decoder e
somente então publicado. Conteúdo inválido, limites ou falhas internas deixam
o catálogo vazio e retornam `false`; não há fallback Rust→C++. Arquivo ausente
continua sendo detectado antes da chamada Rust. Escrita do catálogo,
filesystem, permissões, materialização, seleção de backend e execução seguem
em C++. `TL_BUILD_RUST=OFF` é a variante C++ explícita e padrão, sem link ou
referência operacional ao parser Rust.

Com trace, o componente `runtime` emite `catalog-parse` para tentativas Rust,
com `backend`, `parser-status` e, em rejeições, `code`, `phase`,
`input-offset` e `detail-value`. Sem trace o stdout/stderr normal permanece
inalterado; nenhum aplicativo ou nível de compatibilidade é promovido por
esta mudança.
