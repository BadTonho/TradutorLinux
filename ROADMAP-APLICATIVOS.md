# Roadmap do corpus de aplicativos Windows

Este documento transforma os resultados da primeira rodada de testes do diretório
`Aplicativos_Windows_Populares/` em uma fila de trabalho verificável. Ele
complementa os roadmaps de produto e Rust ([ROADMAP.md](feitos/ROADMAP.md) e
[ROADMAP-RUST.md](feitos/ROADMAP-RUST.md)); não substitui suas fases nem
autoriza declarar compatibilidade sem testes de integração e registro em
[`docs/compatibilidade.md`](docs/compatibilidade.md).

## Estado inicial — 2026-09-07

- [x] Inventariados 24 executáveis e 1 pacote `Affinity x64.msix`.
- [x] Analisados os executáveis com `--report` no build Rust Debug.
- [x] Executados os executáveis com limite externo de tempo, limite de memória
  e prefixo temporário.
- [x] Revalidados os downloads de CPU-Z, GPU-Z e HWMonitor como PE; o HWiNFO64
  existente foi preservado.
- [x] Analisado o pacote MSIX, incluindo EOCD/central directory ZIP64 e extras
  de entrada; ele é rejeitado de forma controlada pelo limite agregado de
  512 MiB, não por formato ZIP desconhecido.

Os logs detalhados dessa rodada foram temporários e não fazem parte do contrato
de evidência do projeto. Cada marco abaixo deve produzir sua própria fixture,
teste e registro reproduzível.

## Resultado de referência

| Grupo | Resultado atual | Próxima ação |
|---|---|---|
| `--report` | `7zFM_x64.exe`, `7z_x64.exe`, `Rufus_x64.exe`, `WinRAR_x64.exe` e `winrar-x64-723.exe` passaram | Separar análise aprovada de execução aprovada |
| Execução | `7z_x64.exe`, `WinRAR_x64.exe` e `winrar-x64-723.exe` terminaram com código 0 | Regressão em matriz ON/OFF |
| GUI | `7zFM_x64.exe` chegou à execução, mas não havia X11 funcional | Repetir em ambiente gráfico controlado |
| PE32/x86 | 12 arquivos rejeitados por arquitetura não suportada | Manter fora do escopo até decisão própria |
| Unwind x64 | 6 executáveis agora passam no `--report`; o trace registra V1/V2, cadeias e `extended-set-fpreg` | Avaliar as limitações de execução de cada aplicativo |
| HWiNFO64 | Imagem empacotada: diretório de exports em região sem dados crus | Manter rejeição segura e registrar UPX0/UPX1 |
| Rufus | Report passou; execução parou por entry point fora de página executável | Investigar imagem empacotada e política de execução |
| MSIX Affinity | ZIP64 de disco único válido com 1.284 entradas; rejeitado pelo limite agregado descompactado de 512 MiB | Manter limite seguro e registrar a limitação do pacote |

## Ordem de execução

Os marcos são independentes, mas devem ser tratados nesta ordem: unwind x64,
MSIX, HWiNFO, Rufus, GUI e instaladores. A decisão sobre PE32/x86 fica
separada e não deve ser introduzida como efeito colateral de outra correção.

### A1 concluído — evidência de 2026-09-07

O parser agora aceita as duas formas estendidas observadas no corpus: um GPR
válido em `OpInfo` e a repetição de `FrameOffset`, inclusive o valor `4`.
Qualquer valor que não seja GPR válido nem `FrameOffset` continua sendo
`unsupported-mechanism`. C++ e Rust compartilham essa regra e o diferencial
continua sem fallback.

Evidência reproduzível:

- [x] Fixture C++ e fixture diferencial Rust cobrem `FrameOffset=4`, GPR
  estendido e valor inválido.
- [x] Suíte unitária Rust Debug: 493 testes aprovados e 1 skip ambiental
  previamente definido, usando prefixo temporário.
- [x] `--report` Rust e C++ OFF passaram para Logitech G HUB, G HUB Installer,
  Roblox, Rockstar Games Launcher, Notepad++ e PuTTY.
- [x] Execução Rust controlada: Logitech/G HUB terminaram com código 1;
  Roblox/Rockstar com código 3; Notepad++ terminou por `SIGABRT` controlado
  (71); PuTTY terminou por timeout controlado (72).
- [x] Nenhuma dessas execuções falhou no parsing de unwind; o trace confirma
  mapeamento posterior e os limites de execução permaneceram ativos.

### A1 — Unwind x64 estendido

Objetivo: ampliar a análise somente se a semântica dos encadeamentos reais for
compatível com o modelo do loader.

Tarefas:

- [x] Localizar a validação que rejeita `UWOP_SET_FPREG` com `OpInfo` estendido.
- [x] Comparar os casos de Logitech G HUB, Roblox, Rockstar Games Launcher,
  Notepad++ e PuTTY com fixtures mínimas.
- [x] Definir a semântica suportada para unwind e encadeamentos, sem relaxar
  ranges, offsets ou permissões.
- [x] Preservar diagnóstico estruturado e ausência de mapeamento/execução nas
  rejeições.

Aceitação:

- [x] Testes diferenciais C++/Rust passam para todos os casos cobertos.
- [x] `--report` e execução têm resultado controlado e documentado.
- [x] A matriz ON/OFF não apresenta fallback silencioso nem regressão.

### A2 concluído — estrutura de exports do HWiNFO64

Evidência reproduzível de 2026-09-07:

- [x] O `--report` Rust retornou exit `4`, `status="malformed"`,
  `code="24"`, `phase="6"`, `input-offset="10007328"` e
  `detail-value="4492"`; o build C++ OFF retornou a mesma categoria com
  `diretório de exports fora da imagem`.
- [x] A inspeção PE confirmou `UPX0` com `VirtualSize=0x1496000` e
  `SizeOfRawData=0`, `UPX1` como região empacotada com os dados crus, e o
  diretório de exports em RVA `0x98b320`, dentro da região sem representação
  no arquivo. O entry point fica em `UPX1`; a imagem depende do desempacotamento
  em runtime para materializar esse layout.
- [x] Foi adicionada a fixture `PeReaderTest.RejectsExportDirectoryWithoutFileBackedSection`,
  que protege a rejeição quando uma diretiva aponta para uma seção sem dados
  crus. Nenhum range RVA foi relaxado.
- [x] Não houve mapeamento ou execução após a rejeição; a matriz ON/OFF mantém
  exit `4` e não há fallback Rust→C++.

Conclusão: o arquivo é um PE empacotado legítimo para execução Windows, mas seu
diretório de exports não existe como bytes no arquivo estático. O parser deve
rejeitá-lo como entrada estruturalmente não analisável sem um desempacotador,
que permanece fora do escopo.

### A2 — Estrutura de exports do HWiNFO64

Objetivo: distinguir uma imagem PE legítima de uma imagem malformada sem
enfraquecer a validação de RVA.

Tarefas:

- [x] Reproduzir o erro de diretório de exports fora da imagem.
- [x] Inspecionar seções, diretório de dados, ranges e conversões RVA/offset.
- [x] Confirmar a menor decisão segura: não relaxar o mapeamento RVA e criar
  uma fixture de regressão para seção sem dados crus.
- [x] Manter a rejeição e documentar que a imagem exige desempacotamento.

Aceitação:

- [x] A decisão é sustentada por fixture, trace estruturado e inspeção das
  seções reais.
- [x] Não há acesso fora da entrada nem relaxamento genérico de limites.

### A3 concluído — Rufus e imagens PE empacotadas

Evidência reproduzível de 2026-09-07:

- [x] `--report` Rust e C++ OFF passaram com exit `0`, descrevendo as três
  seções `UPX0`, `UPX1` e `.rsrc`, relocations e imports.
- [x] A execução Rust e C++ OFF terminou antes do convidado com exit `4` e
  `map-failed status="invalid-image" detail="entry point fora de uma página
  executável"`.
- [x] A causa é a política W^X existente: `UPX1` é marcada como
  `READ|WRITE|EXECUTE`, o mapper a reduz a `ReadWrite`, e o processo não
  executa uma página sem permissão de execução. O entry point está em `UPX1`.
- [x] Os testes existentes de `DowngradesWritableExecutableSectionToReadWrite`
  e permissões efetivas protegem essa decisão; nenhuma permissão `RWX` foi
  reintroduzida e não há execução após a falha.

Conclusão: Rufus é analisável como PE, mas a execução exige desempacotamento
ou transição controlada de permissões que não faz parte do loader atual. A
limitação é intencional e não é promovida como suporte funcional.

Objetivo: separar suporte de análise de suporte de execução para imagens com
seções como `UPX0`/`UPX1`.

Tarefas:

- [x] Inspecionar entry point, seções, permissões e relocations.
- [x] Determinar que o erro ocorre no mapeamento/política W^X, após o parser.
- [x] Manter a política segura para imagens empacotadas e registrar a
  limitação sem desempacotador.
- [x] Usar as regressões existentes de W^X e permissões para proteger a
  análise aprovada sem prometer execução.

Aceitação:

- [x] O report continua distinto da execução.
- [x] A falha ocorre antes da execução quando as pré-condições não são válidas.

### A4 concluído — pacote MSIX do Affinity

Evidência reproduzível de 2026-09-07:

- [x] O parser Rust e o inspector C++ passaram a reconhecer EOCD ZIP64,
  localizador, entradas de disco único e o extra `0x0001` para os campos
  sentinela, com aritmética checked e rejeição de multipartes.
- [x] Fixture mínima com manifesto DEFLATE e executável armazenado passou no
  diferencial Rust/C++, na extração e nos testes de `size`/`fill`; as oito
  verificações `RustMsixParserTest.*` passaram.
- [x] O arquivo real `Affinity x64.msix` foi analisado pelo TradutorLinux no
  build Rust e no build C++ OFF. Ambos retornaram exit `4` e mantiveram o
  stdout normal; Rust emitiu `code="19" phase="3" input-offset="672836042"`
  e `detail-value="18527752"`, identificando o limite de tamanho, enquanto o
  caminho OFF permaneceu sem campos Rust.
- [x] Cargo test (38 testes) e Clippy offline com `-D warnings` passaram.

A rejeição do Affinity é intencional: o pacote possui 1.560.716.440 bytes
descompactados, acima do limite agregado de 512 MiB. O manifesto também aponta
para um executável .NET/Mono, que continua fora do escopo. Portanto não há
instalação ou execução declarada para esse pacote.

Objetivo: identificar por que um ZIP estruturalmente válido é rejeitado pelo
parser MSIX Rust.

Tarefas:

- [x] Reproduzir o diagnóstico inicial de EOCD clássico com sentinela ZIP64 e
  confirmar que o valor `65535` não representava corrupção.
- [x] Comparar central directory, EOCD, flags, extra fields, timestamps,
  compressão, atributos e estruturas Zip64 no arquivo real.
- [x] Verificar disco único, encryption, links e convenções de bundle sem
  presumir que todo ZIP é MSIX simples.
- [x] Criar uma fixture mínima representativa e um caso de rejeição por limite.

Aceitação:

- [x] O formato é aceito com validações completas quando ZIP64 é de disco único
  e está dentro dos limites; o pacote real fica rejeitado por limite preciso.
- [x] Extração, PE interno e catálogo continuam fora da análise do parser sem
  integração não planejada.

### A5 — GUI e ambiente X11

Objetivo: distinguir falha do aplicativo de ausência de ambiente gráfico.

Tarefas:

- [ ] Repetir `7zFM_x64.exe` em X11/Xvfb funcional.
- [ ] Registrar timeout, memória, exit code, stdout, stderr, trace e limpeza.
- [ ] Separar skips ambientais de falhas funcionais.

Aceitação:

- [ ] O resultado reproduzível identifica explicitamente se o bloqueio é X11,
  GUI ou runtime.
- [ ] Nenhuma conclusão de compatibilidade depende de uma sessão gráfica ausente.

### A6 — Instaladores e fluxo de instalação

Objetivo: validar instalação de forma isolada, sem executar indiscriminadamente
instaladores obtidos da internet.

Tarefas:

- [ ] Selecionar instaladores por classe e analisar primeiro com `--report`.
- [ ] Executar apenas com prefixo temporário, timeout externo, limite de
  memória e limpeza garantida.
- [ ] Verificar catálogo, permissões, extração, PE interno e `app run` quando
  aplicável.
- [ ] Cobrir o caminho MSIX com parse, extração e validação separados.

Aceitação:

- [ ] Cada instalador testado tem fixture, comando, resultado e trace
  registrados.
- [ ] Falhas não deixam arquivos, catálogo ou prefixo parcial.

### A7 — Proveniência e integridade dos downloads

Objetivo: tornar o corpus reproduzível e distinguir arquivos oficiais de
artefatos apenas disponíveis localmente.

Tarefas:

- [ ] Registrar origem e SHA-256 oficial do CPU-Z, HWMonitor e HWiNFO64.
- [ ] Registrar a origem e o SHA-256 do GPU-Z já verificado:
  `6cb0ef29682452de81a9576808881685161411a1fad00938ba04131159979c29`.
- [ ] Preservar versão, arquitetura e data de cada arquivo no inventário.

Aceitação:

- [ ] O corpus usado nos testes pode ser reconstruído sem depender de nomes
  ambíguos ou downloads não documentados.

### A8 — Decisão independente sobre PE32/x86

Objetivo: decidir o futuro dos 12 executáveis PE32 sem ampliar o escopo atual
por acidente.

Tarefas:

- [ ] Manter a rejeição `Unsupported` para PE32/x86 enquanto não houver decisão.
- [ ] Avaliar custo, arquitetura e impacto no loader em documento separado.
- [ ] Só iniciar implementação após atualizar `ROADMAP.md`, contratos,
  compatibilidade e testes, com autorização explícita.

Aceitação:

- [ ] Existe uma decisão registrada: manter fora do escopo ou abrir uma fase
  própria com critérios técnicos.

## Regras de validação

- Cada correção começa com uma fixture mínima e termina com testes automatizados.
- Validar análise e execução separadamente; passar em `--report` não significa
  que o aplicativo seja executável.
- Comparar builds Rust ON e C++ OFF quando o caminho tiver os dois backends.
- Usar prefixo temporário, timeout externo, limite de memória e limpeza após
  cada execução.
- Não executar todos os instaladores diretamente nem tratar o runtime como
  sandbox de segurança.
- Não declarar suporte de aplicativo antes de atualizar a matriz de
  compatibilidade com evidência reproduzível.

## Definição de concluído por marco

Um marco só pode ser encerrado quando houver fixture mínima, implementação
revisada, testes CTest/Rust relevantes, report e execução avaliados, comparação
ON/OFF quando aplicável, trace/exit code documentados e `git diff --check`
aprovado. Skips ambientais devem ser identificados separadamente; falhas
funcionais não podem ser reclassificadas como skips.
