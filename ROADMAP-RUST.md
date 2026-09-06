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

- [ ] Escolher Rust como implementação canônica do parsing PE.
- [ ] Manter o parser C++ somente como oráculo diferencial ou fallback de
  compatibilidade durante um período definido.
- [ ] Não permitir fallback silencioso quando os resultados divergirem.
- [ ] Decidir explicitamente se o binário de produção passará a exigir a
  biblioteca Rust; `TL_BUILD_RUST=OFF` não pode fingir usar Rust.
- [ ] Remover o parser C++ da execução principal somente após a matriz inteira
  passar e o corpus diferencial permanecer estável.

## Prioridade 2 — Parser de pacotes MSIX/AppX

O parser em `src/package/msix.cpp` também recebe entrada externa e combina
ZIP, XML, caminhos e seleção de executável. A migração deve ficar limitada à
análise dos bytes e do manifesto.

### R22.1 — Análise segura do pacote

- [ ] Migrar a leitura limitada da estrutura ZIP e do manifesto XML.
- [ ] Validar tamanhos, contagens, compressão, entidades e caminhos no Rust.
- [ ] Rejeitar traversal, links e entradas ambíguas de forma determinística.
- [ ] Preservar a seleção exclusiva de PE32+ AMD64 nativo.
- [ ] Comparar todos os resultados com o parser C++ e fixtures existentes.

### R22.2 — Integração

- [ ] Integrar primeiro no inspection/report do pacote.
- [ ] Integrar depois no fluxo de instalação, mantendo em C++ a criação do
  prefixo, extração física, permissões e cadastro no catálogo.
- [ ] Não adicionar suporte a .NET, Mono, bundles ou assinatura Authenticode.

## Prioridade 3 — Parser de perfis

O parser em `src/compat/profile.cpp` é um candidato válido, mas posterior ao
PE e ao MSIX. O ganho principal é eliminar outra rotina manual de parsing de
entrada, não melhorar o runtime em si.

### R23.1 — Schema e validação

- [ ] Migrar a leitura dos schemas de perfil já documentados.
- [ ] Preservar rejeição de campos desconhecidos, duplicidades, fontes ausentes,
  traversal, identidades incompatíveis e backend inválido.
- [ ] Manter a materialização, o acesso ao filesystem e a seleção final do
  backend em C++.
- [ ] Comparar perfis válidos, ausentes, inválidos e incompatíveis com o
  parser C++ atual.

### R23.2 — Promoção

- [ ] Usar Rust como parser canônico somente após a matriz de perfis passar em
  Debug, Sanitize e Release.
- [ ] Preservar o fallback genérico para perfil ausente ou lexicalmente
  inválido.
- [ ] Não alterar o schema apenas por causa da migração de linguagem.

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
