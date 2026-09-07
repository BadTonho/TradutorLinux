# Roadmap de migração seletiva para Rust

Este documento contém somente os componentes do TradutorLinux cuja migração
para Rust tem benefício técnico claro. Ele complementa o `ROADMAP.md` e não
transforma Rust em requisito geral do projeto.

## Objetivo

Usar Rust onde o projeto processa dados externos, potencialmente malformados,
com muitas validações de limites e pouca dependência da execução Win32. O Rust
deve proteger a parte de análise; o C++ continua responsável por integrar o
resultado ao loader e executar o convidado.

A regra de promoção é ter uma implementação canônica, corpus diferencial,
integração reproduzível e diagnóstico equivalente. Durante a transição, uma
implementação C++ pode permanecer como oráculo de comparação ou fallback
explícito, mas nunca deve haver escolha silenciosa entre resultados diferentes.

## Estado atual

- [x] **B20 — Validação lexical de caminhos.** O Rust já valida, de modo
  opt-in, caminhos relativos, destinos `C:\...`, UTF-8 e UTF-16. O filesystem,
  o materializador e o runtime continuam em C++.
- [x] **B20 — Fronteira FFI e robustez.** ABI C estável, handles opacos,
  buffers pertencentes ao chamador, ausência de unwind pela ABI, Cargo fixado,
  testes determinísticos e comparação Rust/C++ foram concluídos.

## Prioridade 1 — Parser de PE

O leitor em `src/pe/pe_reader.cpp` é o melhor próximo candidato. Ele tem cerca
de 1.394 linhas, recebe bytes externos e analisa headers, seções, imports,
exports, relocations, TLS, delay imports e unwind sem precisar executar código.

### R21.1 — Contrato do parser Rust

- [x] Definir uma representação normalizada e versionada para o resultado do
  parsing PE32+ AMD64.
- [x] Manter todos os buffers de entrada e saída sob propriedade do C++.
- [x] Não devolver ponteiros, `String`, `Vec` ou referências Rust pela ABI.
- [x] Definir códigos para arquivo truncado, malformado, arquitetura/formato
  incompatível e mecanismo não suportado.
- [x] Definir limites para seções, diretórios, strings, imports, exports,
  relocations, runtime functions e callbacks TLS.
- [x] Provar que o contrato não depende de layout interno do Rust.

### R21.2 — Implementação e robustez

- [x] Implementar no Rust um leitor de bytes com aritmética checked e acesso
  sempre limitado ao arquivo recebido.
- [x] Migrar a validação e extração de headers e seções.
- [x] Migrar imports estáticos e delay imports.
- [x] Migrar exports por nome, ordinal e forwarder textual.
- [x] Migrar relocations, TLS e diretório de exceções somente como dados.
- [x] Manter zero crates externas inicialmente.
- [x] Adicionar testes unitários, property-based determinístico e corpus de
  arquivos PE válidos, truncados e malformados.
- [x] Comparar o resultado com o parser C++ existente sem divergência não
  justificada.

Evidência reproduzível em 2026-09-06: `cargo test --locked --offline` passou
17/17 e `cargo clippy --locked --offline --all-targets -- -D warnings` passou;
os testes selecionados passaram 55/55 no Debug, 9/9 no Sanitize, e 55/55 no
Release após construir explicitamente o contrato C. Com `TL_BUILD_RUST=OFF`,
os 42 testes `PeReaderTest` e o contrato C passaram 43/43. A implementação
continua fora do loader e do `app run`; o uso no relatório direto é o escopo
exclusivo de R21.3.

### R21.3 — Integração sem risco de execução

- [x] Integrar somente no `--report` direto, sem alterar o mapeamento ou
  executar o entry point do convidado; `app run --report` permanece em C++.
- [x] Confirmar equivalência semântica de headers, seções, imports,
  delay-imports, exports/forwarders, relocations, TLS e unwind, além de status
  e diagnóstico estruturado.
- [x] Executar o corpus de relatório em Debug Rust e no baseline C++ com
  `TL_BUILD_RUST=OFF`, além dos testes afetados em Sanitize e Release.
- [x] Registrar a política de backend, limites e falhas estruturadas no trace,
  no diagnóstico e na matriz de compatibilidade.

Evidência reproduzível em 2026-09-06: o conjunto completo do Debug passou
464/464 testes executados (1 teste ambiental pulado); os testes diferenciais
do wire format e do caminho `--report` passaram 14/14 em Debug, Sanitize e
Release. O corpus `report_*_support` passou 60/60 serialmente com Rust e
60/60 no baseline `TL_BUILD_RUST=OFF`; a comparação do stdout do relatório
mínimo passou sem diferenças. Cargo test passou 17/17 e Clippy offline com
`-D warnings` passou. O teste com LeakSanitizer não pôde inicializar neste
ambiente por restrição de `ptrace`; a rodada Sanitize foi repetida com
`detect_leaks=0` e passou 14/14.

### R21.4 — Integração no `app run`

- [x] Usar o resultado Rust para preparar o modelo consumido pelo loader,
  somente na imagem principal do `app run` nativo com Rust habilitado.
- [x] Manter em C++ o `mmap`, `mprotect`, relocação aplicada na memória,
  resolução de endereços, ABI Microsoft x64 e execução.
- [x] Validar que uma falha de parsing impede o entry point e preserva os
  códigos de saída existentes.
- [x] Reexecutar todas as fixtures PE32+ e os testes de imports, TLS, unwind e
  crash, incluindo o baseline sem Rust e os caminhos Proton/`--report`.

Evidência reproduzível em 2026-09-06:

- Debug com `TL_BUILD_RUST=ON`: matriz `runtime` sem Proton passou 192/192,
  incluindo 61/61 testes `app-run`; as verificações Rust/contrato passaram
  4/4 e o teste de DLL dependente confirmou que somente a imagem principal usa
  `backend="rust"`.
- Debug com `TL_BUILD_RUST=OFF`: matriz `app-run` passou 61/61 e os testes
  operacionais, Proton, instalação, relatório, rejeição e DLL dependente
  passaram 7/7, sem referência ao backend Rust.
- Release com Rust: matriz `app-run` passou 61/61; os gates Rust, rejeições,
  delay-imports e unwind passaram 12/12.
- Sanitize com Rust: os casos estáveis selecionados passaram 12/12 e a
  dependência DLL passou. A matriz completa passou 54/61; sete casos foram
  limitados por condições ambientais/fixtures já conhecidas (imagem sem
  relocations sob a base do sanitizer, UBSan em stores desalinhados de
  fixtures, limites de memória do ASan e saídas específicas de fixtures).
  Portanto, não se declara a matriz Sanitize completa como verde.
- Cargo `test --locked --offline`, Clippy offline com `-D warnings` e os
  contratos C/C++ passaram nos gates executados. O conjunto de testes não
  modificou `PeInfo`, o header FFI, o wire format, o loader ou o backend
  Proton.

### R21.5 — Promoção

- [x] Escolher Rust como implementação canônica do parsing PE no `--report`
  direto e na imagem principal do `app run` nativo sem `--report` quando
  `TL_BUILD_RUST=ON`.
- [x] Centralizar a seleção do backend e manter o parser C++ em produção nos
  caminhos excluídos: DLLs dependentes, Proton, instalação, `app run
  --report`, execução direta normal e builds `TL_BUILD_RUST=OFF`.
- [x] Não permitir fallback silencioso quando a análise Rust falhar ou quando
  o wire format for inválido; falhas terminam antes de mapeamento e execução.
- [x] Manter `TL_BUILD_RUST=OFF` como variante C++ explícita e padrão, sem
  linkar ou referenciar a biblioteca Rust.
- [x] Preservar o fluxo C++ após o `PeInfo` Rust e manter o parser C++ como
  oráculo diferencial somente nos caminhos promovidos.

Evidência reproduzível em 2026-09-06:

- Debug Rust, Release Rust e Sanitize Rust passaram `748/748` testes executados
  cada, incluindo a matriz nativa `app-run`, relatório direto, `app run
  --report`, DLL dependente, Proton mockado, contratos e corpus diferencial.
  Em cada rodada, `IphlpapiTest.EnumeratesLinuxAdaptersWithWin32BufferContracts`
  e `runtime_tl_wininet_https_loopback` foram os únicos skips de ambiente.
- Os dois testes GUI/X11 opcionais foram executados separadamente e retornaram
  `SKIPPED` por indisponibilidade do display; o Proton real permanece um teste
  opcional dependente da instalação local. O Proton mockado passou no Sanitize.
- O baseline C++ Debug e Release com `TL_BUILD_RUST=OFF` passou `725/725`
  testes executados cada. `nm` não encontrou símbolos `tl_pe_parse_v1` ou
  `tl_rust_validator` nos executáveis OFF.
- `cargo test --locked --offline` passou `17/17` e `cargo clippy --locked
  --offline --all-targets -- -D warnings` passou. `git diff --check` passou.

R21.5 não altera o estado de compatibilidade de aplicativos nem promove
suporte funcional; a promoção cobre apenas a seleção do parser e preserva o
loader, o backend Proton e a separação Rust/C++ definida acima.

## Prioridade 2 — Parser de pacotes MSIX/AppX

O parser em `src/package/msix.cpp` também recebe entrada externa e combina
ZIP, XML, caminhos e seleção de executável. A migração deve ficar limitada à
análise dos bytes e do manifesto.

### R22.1 — Análise segura do pacote

- [x] Migrar a leitura limitada da estrutura ZIP e do manifesto XML para a ABI
  `TLMS` v1.0, sem alterar a seleção de backend de produção.
- [x] Validar tamanhos, contagens, compressão, entidades e caminhos no Rust,
  com ponte C mínima para raw DEFLATE/zlib.
- [x] Rejeitar traversal, links, NUL, colisões após normalização, bundles,
  Zip64, multipartes, encryption, DTD e entidades externas de forma
  determinística; o inspector C++ recebeu a mesma política.
- [x] Preservar a seleção exclusiva de PE32+ AMD64 nativo no fluxo C++ futuro;
  R22.1 não valida nem executa o PE interno do pacote.
- [x] Comparar semanticamente Rust e C++ em pacotes stored/DEFLATE e manter
  regressões para manifesto, wire, buffers, erros e concorrência.

Implementação concluída em R22.1:

- `include/tradutorlinux/ffi/rust_msix_parser.h` congela a ABI, os status,
  erros estruturados, limites e layouts do wire; os contratos C/C++ verificam
  largura, alinhamento, offsets e constantes.
- `src/rust/msix_parser.rs` implementa leitor LE bounded, EOCD/central/local
  headers, stored/raw DEFLATE, normalização de nomes, XML limitado e serializer
  determinístico; `src/package/rust_msix_parser.cpp` valida/decodifica TLMS e
  adapta `AppxPackageInfo`.
- `TL_BUILD_RUST=OFF` continua padrão: o runner, `inspect_msix_package`,
  `install`, `app run` e o loader continuam no backend C++.

Evidência reproduzível em 2026-09-06:

- `cargo test --locked --offline`: `23/23`; Clippy com
  `--locked --offline --all-targets -- -D warnings`: aprovado.
- Rust Release: `RustMsixParserTest.*` `6/6`, `MsixParserTest.*` `8/8`,
  contratos C/C++ `2/2` e CTest `rust_msix_differential` aprovado.
- C++ Debug com `TL_BUILD_RUST=OFF`: `MsixParserTest.*` `8/8`, contratos C/C++
  `2/2`; `nm` não encontrou os símbolos `tl_msix_parse_v1`,
  `tl_msix_inflate_raw` ou `tl_rust_validator` na biblioteca C++.
- `git diff --check` deve permanecer limpo antes do commit; a execução dos
  presets Rust Debug/Sanitize depende dos presets já configurados no ambiente.

### R22.2 — Integração

- [x] Integrar Rust no `--report` direto e no fluxo de instalação quando
  `TL_BUILD_RUST=ON`, selecionando pacotes por extensão antes da validação ZIP.
- [x] Manter em C++ a criação do prefixo, extração física, permissões,
  cadastro no catálogo e validação do PE interno; a extração usa o executável
  principal validado por Rust e não substitui seus metadados.
- [x] Rejeitar bundles sem fallback e preservar os caminhos C++ de execução
  direta normal, `app run`, `app run --report`, Proton, DLLs dependentes e
  `TL_BUILD_RUST=OFF`.
- [x] Emitir `package-parse` apenas nos caminhos Rust, com `backend`, `status`
  e os campos estruturados de falha; mapear erros para os códigos `4`, `5` e
  `70` sem alterar a saída normal.
- [x] Expandir a integração de testes e CI para a matriz de pacotes ON/OFF,
  incluindo instalação, rejeições, diferencial, contratos e verificação de
  ausência dos símbolos Rust no build OFF.
- [x] Fechar o gate de conclusão após evidência local dos presets exigidos,
  mantendo como skips somente os testes opcionais sem display, rede local ou
  execução Proton real disponíveis no ambiente.

Implementação concluída para R22.2:

- `select_msix_parser_backend` centraliza a política: Rust só é canônico no
  relatório direto e no `install`; os demais fluxos continuam no inspector C++.
- O runner lê o pacote inteiro com limite de 2 GiB e chama `parse_msix_rust`;
  falhas encerram antes de extração/cadastro e não acionam fallback. O
  `extract_msix_package` sobrecarregado recebe `main_executable` do resultado
  Rust, enquanto o extrator mantém as validações físicas e o PE interno C++.
- As bibliotecas de teste Rust declaram explicitamente a ponte DEFLATE e zlib;
  isso mantém `tl_rust_ffi_probe` e `tl_rust_path_validation` independentes do
  link transitivo do `tradutorlinux_core`.
- `integration_msix_install` e `integration_msix_rejections` verificam
  `RUST_ENABLED`, stdout, trace, códigos, bundles, pacote truncado, manifesto
  inválido, instalação e ausência de catálogo após falha.

Evidência reproduzível em 2026-09-06:

- Rust Release: CTest completo `763/763` passou, com quatro skips ambientais;
  a seleção MSIX, contratos e diferencial passaram.
- Rust Debug: após instalar Ninja e reutilizar a cópia local do GoogleTest para
  manter o configure offline, CTest completo `763/763` passou, com quatro
  skips ambientais; Cargo/Clippy e os probes FFI também passaram.
- Rust Sanitize: CTest completo com exclusão explícita dos cinco testes Proton
  reais e do smoke X11 impedidos pelo sandbox passou `762/762`, com três skips
  opcionais (Iphlpapi, GUI e loopback HTTPS). A execução sem essa exclusão
  registrou as falhas ambientais reais e não foi promovida a verde.
- `cargo test --locked --offline` passou `23/23`; Clippy offline com
  `--all-targets -- -D warnings` passou. O baseline Release OFF passou
  `732/732`, e `nm` não encontrou símbolos `tl_msix_parse_v1`,
  `tl_msix_inflate_raw` ou `tl_rust_validator` na biblioteca C++.
- `git diff --check` passou; o worktree será verificado novamente após este
  registro.

## Prioridade 3 — Parser de perfis

O parser em `src/compat/profile.cpp` é um candidato válido, mas posterior ao
PE e ao MSIX. O ganho principal é eliminar outra rotina manual de parsing de
entrada, não melhorar o runtime em si.

### R23.1 — Schema e validação

- [x] Implementar a análise byte-oriented dos schemas de perfil já
  documentados e o contrato TLPR v1.0 sem alterar `Profile` público.
- [x] Preservar rejeição de campos desconhecidos, duplicidades, fontes ausentes,
  traversal, identidades incompatíveis e backend inválido.
- [x] Manter a materialização, o acesso ao filesystem e a seleção final do
  backend em C++.
- [x] Comparar perfis válidos, ausentes, inválidos e incompatíveis com o
  parser C++ atual.

Implementação concluída em R23.1:

- `include/tradutorlinux/ffi/rust_profile_parser.h` congela a ABI C, o contexto
  de identidade, status, erros estruturados, limites e layouts TLPR.
- `src/rust/profile_parser.rs` implementa o modelo proprietário, leitor JSON
  byte-oriented para schemas 1/2/3, validação de identidade/caminhos e
  serializer determinístico com aritmética checked.
- `src/compat/rust_profile_parser.cpp` valida e decodifica TLPR para o
  `Profile` existente; `load_profile`, produção, materialização e seleção de
  backend continuam em C++.
- Os testes cobrem ABI C/C++, magic/versão/offsets/strides/reservados,
  buffers/sentinelas, limites, concorrência e diferencial contra
  `load_profile`. O build OFF não liga a staticlib nem referencia símbolos Rust.

Evidência reproduzível em 2026-09-06:

- `cargo test --locked --offline`: `28/28`; Clippy com
  `--locked --offline --all-targets -- -D warnings`: aprovado.
- Rust Debug: CTest completo `771/771`; Rust Release: CTest completo
  `771/771`; em ambos, quatro skips ambientais opcionais.
- Rust Sanitize: subconjunto reproduzível passou sem falhas nos testes de
  produto; os cinco Proton reais e `x11_popup_smoke` foram excluídos por
  dependências ambientais, e Iphlpapi, GUI e HTTPS permaneceram skips
  opcionais. A execução sem exclusões registrou somente essas limitações.
- Baseline `TL_BUILD_RUST=OFF`: CTest `734/734`; os contratos C/C++ passaram e
  `nm` não encontrou símbolos do parser Rust na biblioteca C++.
- `git diff --check` foi executado antes do commit final. R23.2 permanece
  limitada à promoção do backend e ao fallback genérico para perfil ausente ou
  lexicalmente inválido.

### R23.2 — Promoção

- [x] Promover Rust dentro de `load_profile` para perfis existentes em todos os
  consumidores atuais quando `TL_BUILD_RUST=ON`, mantendo `Profile`, TLPR e a
  assinatura pública inalterados.
- [x] Preservar a detecção C++ de perfil ausente e o fallback genérico para
  rejeições de conteúdo Rust (`malformed`, schema/identidade/caminho inválido,
  `unsupported-format`, `input-too-large` e `output-too-large`).
- [x] Encerrar sem fallback em falhas internas da ABI, transporte/decoder TLPR,
  buffers, panic ou status inesperado, com `ProfileStatus::InternalError` e
  diagnóstico estruturado no evento `compat-profile`.
- [x] Manter no C++ as validações físicas, filesystem, materialização, seleção
  de backend e execução; `TL_BUILD_RUST=OFF` continua a variante C++ explícita,
  sem link ou símbolos Rust.
- [x] Atualizar CI, testes diferenciais, diagnóstico, compatibilidade e o
  contrato arquitetural, sem alterar `Cargo.lock` nem o schema de perfis.

Implementação concluída em R23.2:

- `load_profile` chama `parse_profile_rust` e `decode_tlpr_v1` somente depois
  dos pré-checks C++ de presença, tipo regular, leitura e limite de entrada;
  o resultado Rust alimenta diretamente o `Profile` existente.
- `ProfileLoadResult` preserva diagnósticos do backend, status, código, fase,
  offset e valor estruturado. O trace `compat-profile` acrescenta os campos
  Rust somente quando houve tentativa; perfil ausente e build OFF não recebem
  esses campos.
- A matriz CI constrói explicitamente os contratos C/C++ e executa o CTest
  completo serialmente, evitando colisões dos prefixos compartilhados pelos
  testes.

Evidência reproduzível em 2026-09-06:

- `cargo test --locked --offline`: `28/28`; Clippy com
  `--locked --offline --all-targets -- -D warnings`: aprovado.
- Rust Debug: CTest completo passou `768` testes, com quatro skips ambientais;
  Rust Release teve o mesmo resultado. As matrizes de perfil, contratos,
  diferenciais e integrações passaram.
- Rust Sanitize: a matriz reproduzível passou `771/771`, com três skips
  ambientais. Os cinco testes Proton real e `x11_popup_smoke` foram excluídos
  explicitamente porque o Proton tentou escrever `dist.lock` em uma instalação
  somente leitura e o Xvfb não iniciou sob LeakSanitizer/ptrace; a execução
  completa sem exclusões registrou somente essas limitações ambientais.
- Baseline `TL_BUILD_RUST=OFF`: Debug e Release passaram `735/735` cada, com
  quatro skips ambientais; os contratos C/C++ passaram e `nm` não encontrou
  símbolos `tl_profile_parse_v1`, `tl_rust_profile`, `tl_pe_parse_v1` ou
  `tl_msix_parse_v1` nas bibliotecas C++.
- `git diff --check` passou. R23.2 está concluída; R23.3 pode tratar a próxima
  promoção de componente sem reabrir o contrato TLPR ou o `Profile` público.

## Prioridade 4 — Catálogo persistente de aplicativos

O catálogo em `src/catalog/app_catalog.cpp` é uma fronteira de dados externos
usada pela CLI, pela GUI, pela instalação e pelo `app run`. O leitor atual é
permissivo: localiza o campo `"apps"` por texto, pode aceitar objetos
parcialmente inválidos e não valida de forma estrita a versão nem o conteúdo
posterior. A migração será limitada à análise dos bytes do `library.json` e
não ampliará a compatibilidade de aplicativos.

### R24.1 — Contrato e parser Rust do catálogo

- [x] Criar `rust_app_catalog_parser.h` com as funções
  `tl_app_catalog_parse_v1_size` e `tl_app_catalog_parse_v1_fill`, status
  estáveis, erro estruturado e buffers caller-owned.
- [x] Definir o wire `TLAC` v1.0 com cabeçalho de 128 bytes, inteiros
  little-endian, tabelas alinhadas a 8 bytes para `info`, `apps`, `args` e
  `strings`, registros de stride fixo, campos reservados zerados e strings
  deduplicadas por bytes.
- [x] Limitar entrada, contagens, argumentos, strings e saída serializada;
  usar aritmética checked em somas, multiplicações, alinhamento e conversões.
- [x] Implementar em Rust um modelo interno separado de `AppEntry`, sem
  filesystem, ponteiros retidos ou tipos Rust atravessando a ABI.
- [x] Preservar o schema atual: versão `1`, campos de identidade, caminhos,
  `args`, `cpu_limit_seconds`, `memory_limit_mib`, SHA-256 e versão opcional,
  incluindo as regras atuais de escapes e Unicode.
- [x] Manter `AppEntry`, `save_to_file`, CLI, GUI, loader, Proton e execução
  inalterados nesta etapa; Rust ficará restrito à ABI e aos testes.

Implementação concluída para R24.1:

- `rust_app_catalog_parser.h` congela a ABI C, os status, erros, limites e o
  wire TLAC v1.0; os contratos C e C++ confirmam largura, alinhamento, magic,
  strides e constantes.
- `src/rust/catalog_parser.rs` implementa o parser JSON byte-oriented estrito,
  o modelo interno, validação de schema/IDs, serializer determinístico e as
  funções `size`/`fill` com panics capturados e buffers caller-owned.
- O C++ de produção não foi alterado: não há decoder, adaptador ou promoção
  de `AppCatalog` nesta etapa. R24.2 continua responsável pelo diferencial.
- A documentação do contrato, da ABI, do diagnóstico e da compatibilidade foi
  atualizada sem alterar `AppEntry`, `Cargo.lock` ou o formato persistido.

Evidência reproduzível em 2026-09-06:

- `cargo test --locked --offline`: 38/38 testes passaram; Clippy com
  `--locked --offline --all-targets -- -D warnings` passou.
- Rust Debug: build do static library, contratos C/C++ e Cargo/Clippy via
  CTest passaram.
- Baseline Debug `TL_BUILD_RUST=OFF`: contratos C/C++ passaram sem link ou
  referência ao parser Rust.
- `git diff --check` deve ser executado antes do commit final deste marco.

### R24.2 — Decoder e diferencial (concluída)

- [x] Criar `parse_app_catalog_rust` e decoder C++ reutilizável para validar
  magic, versão, cabeçalho, descritores, offsets, contagens, strides,
  alinhamento, referências, reservados e limites antes de construir o
  `AppCatalog`.
- [x] Comparar semanticamente Rust e `AppCatalog::load_from_file` em catálogos
  válidos, Unicode, escapes, argumentos, SHA-256, versões e múltiplas entradas.
- [x] Cobrir JSON truncado, campos desconhecidos ou repetidos, trailing comma,
  conteúdo extra, overflow numérico, IDs inválidos, entradas parciais e
  limites de quantidade/tamanho.
- [x] Cobrir `size`/`fill`, buffers insuficientes, sentinelas de memória,
  chamadas repetidas e concorrentes, sem alterar a saída quando a capacidade
  for insuficiente.
- [x] Confirmar que o build `TL_BUILD_RUST=OFF` não liga nem referencia a
  biblioteca ou símbolos do parser Rust.

Implementação entregue para R24.2:

- `rust_app_catalog_parser.hpp/.cpp` expõe o adaptador interno e o decoder
  TLAC sem alterar `AppCatalog::load_from_file` ou qualquer caminho de
  produção. O decoder usa leitores little-endian, valida ranges checked,
  descritores vazios canônicos, padding, reservas, strings, referências,
  contagens, intervalos de argumentos e IDs antes de publicar o vetor.
- `test_rust_app_catalog_parser.cpp` cobre equivalência semântica, strings
  binárias, JSON inválido, mensagens caller-owned, `size`/`fill`, sentinelas,
  mutações de wire, limite de entrada e concorrência. O CMake só adiciona o
  adaptador/teste quando `TL_BUILD_RUST=ON`.

**Status: concluída em 2026-09-06.**

Evidência reproduzível:

- `cargo test --locked --offline`: 38/38; Clippy offline com `-D warnings`:
  aprovado.
- Rust Debug e Rust Release: CTest completo passou, 783/783 testes, com
  quatro skips ambientais permitidos (Iphlpapi, X11/GUI e HTTPS local).
- Rust Sanitize: 781/781 testes passaram; `x11_popup_smoke` e
  `runtime_gui_smoke` foram excluídos explicitamente porque o sandbox não
  fornece X11 e o LeakSanitizer não pode iniciar nesse ambiente. Restaram
  dois skips ambientais permitidos (Iphlpapi e HTTPS local).
- Baseline Debug e Release com `TL_BUILD_RUST=OFF`: CTest serial completo
  passou, 737/737 testes em cada configuração, com os quatro skips ambientais
  permitidos. Ambos foram configurados com `TL_PROTON_ROOT` vazio e
  `TL_BUILD_RUST=OFF`.
- `nm` não encontrou os símbolos Rust do parser/adaptador TLAC nas bibliotecas
  OFF; `git diff --check` passou.
- As matrizes usaram `TL_PREFIX` e `XDG_CONFIG_HOME` temporários e graváveis;
  a execução anterior que registrava falhas de preparação dependia do HOME
  somente leitura do sandbox e não é evidência válida contra R24.2.

O gate diferencial e de build de R24.2 está fechado. A promoção de R24.3 foi
implementada e validada abaixo.

### R24.3 — Promoção do leitor (concluída)

- [x] Somente após o diferencial passar, centralizar a seleção dentro de
  `AppCatalog::load_from_file`, mantendo sua assinatura pública e o formato
  persistido.
- [x] Com `TL_BUILD_RUST=ON`, usar Rust como fonte canônica para catálogos
  existentes; arquivo ausente preserva o comportamento atual e conteúdo
  inválido não pode deixar entradas parciais no catálogo.
- [x] Converter falhas internas de ABI, transporte ou decoder em diagnóstico
  estruturado no trace, sem substituir silenciosamente o resultado Rust por
  C++.
- [x] Manter em C++ a escrita do catálogo, filesystem, permissões, validações
  físicas, criação de desktop entries, seleção de backend e execução.
- [x] Preservar `TL_BUILD_RUST=OFF` como variante C++ explícita e padrão, sem
  alterar stdout, stderr normal, exit codes ou o comportamento dos caminhos de
  execução.

Critérios de saída da R24:

- [x] Cargo test e Clippy com `--locked --offline`.
- [x] Contratos C/C++, diferencial completo e testes de robustez do catálogo.
- [x] CTest nos presets Rust Debug, Sanitize e Release, além dos baselines
  Debug e Release com `TL_BUILD_RUST=OFF`.
- [x] `nm` sem símbolos Rust nas bibliotecas OFF, `git diff --check` limpo e
  evidência reproduzível antes de marcar qualquer subetapa como concluída.
- [x] Nenhuma alteração em `AppEntry`, no schema persistido, no `Cargo.lock`,
  no loader, no backend Proton ou na matriz de compatibilidade de aplicativos.

Implementação concluída para R24.3:

- `AppCatalog::load_from_file` seleciona o parser Rust somente no build
  `TL_BUILD_RUST=ON`, depois de limpar o estado, confirmar abertura e aplicar o
  limite de 4 MiB. O adaptador `parse_app_catalog_rust` e o decoder TLAC são a
  única ponte de produção; o vetor só é publicado após a validação completa.
  Rejeições e falhas internas retornam `false`, deixam o catálogo vazio e não
  acionam fallback para o parser C++.
- O arquivo ausente ou indisponível não chama Rust. O C++ continua responsável
  pela escrita, filesystem e demais operações físicas. O build
  `TL_BUILD_RUST=OFF` mantém o leitor C++ original sem símbolos Rust.
- A tentativa Rust emite `catalog-parse` no componente `runtime` apenas com
  trace solicitado. Sucessos usam `Info`; rejeições de conteúdo/limite,
  `Warning`; falhas internas, `Error`. Rejeições incluem `code`, `phase`,
  `input-offset` e `detail-value`, sem alterar stdout/stderr normal sem trace.

Evidência reproduzível em 2026-09-06:

- Rust Debug: CTest completo passou, `788/788`, com quatro skips ambientais
  permitidos (Iphlpapi, X11/GUI e HTTPS local). A matriz incluiu contratos,
  diferencial TLAC e `integration_catalog_parser`.
- Rust Release: CTest completo passou, `788/788`, com os mesmos quatro skips
  ambientais permitidos.
- Rust Sanitize: `786/786` passou com `x11_popup_smoke` e
  `runtime_gui_smoke` excluídos explicitamente pela limitação de X11/LeakSanitizer
  do ambiente; permaneceram apenas os skips ambientais de Iphlpapi e HTTPS
  local.
- Baseline C++ Debug e Release (`TL_BUILD_RUST=OFF`): CTest completo passou,
  `738/738` em cada configuração, com quatro skips ambientais permitidos.
  Os testes de integração confirmaram que o caminho OFF continua aceitando o
  comportamento C++ e que o ON publica o mesmo catálogo válido sem fallback.
- `cargo test --locked --offline` e Clippy offline com `-D warnings` passaram
  via CTest Rust; contratos C/C++, diferencial, concorrência, buffers,
  sentinelas e trace passaram. `nm` não encontrou símbolos do parser/adaptador
  Rust nas bibliotecas OFF, `Cargo.lock` permaneceu inalterado e
  `git diff --check` passou.

R24.3 está concluída. A promoção não altera `AppEntry`, o schema persistido,
o loader, Proton, execução ou a matriz de compatibilidade de aplicativos.

## Regras para todas as migrações

- [ ] Rust não deve atravessar a ABI com exceções, panics ou ponteiros próprios.
- [ ] Nenhuma crate externa será adicionada sem versão fixada, lockfile,
  justificativa, revisão de licença e validação offline.
- [ ] Cada parser terá corpus diferencial e teste de regressão antes de ser
  usado no fluxo de produção.
- [ ] O resultado Rust será convertido para tipos C++ estáveis antes de entrar
  no loader ou no runtime.
- [ ] Rust deve ser promovido por componente, não por reescrita global.

## Componentes que não entram neste roadmap

O loader de execução, `GuestContext`, grafo de módulos, `image_mapper`, ABI
Microsoft x64, APIs Win32, SEH/unwind, assembly, memória convidada, isolamento
de processos, materialização física, GUI e backend Proton permanecem em C++.
Eles dependem de endereços reais, pilhas, sinais, chamadas Windows, `mmap`,
assembly ou bibliotecas do host; uma reescrita em Rust acrescentaria uma
fronteira `unsafe` sem benefício proporcional.

Rust também não será usado para substituir código estável apenas por ser
menor ou mais moderno. A migração só avança quando reduzir risco real ou
melhorar a validação de uma entrada compartilhada por várias aplicações.
