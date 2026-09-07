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

### A5 concluído — GUI e ambiente X11

Evidência reproduzível de 2026-09-07:

- [x] Em `Xvfb :99 -screen 0 1280x800x24`, o TradutorLinux iniciou
  `7zFM_x64.exe`, criou a janela X11 `"7-Zip"` de `800x600` e registrou as
  classes `7-Zip::FM` e `7-Zip::Panel`.
- [x] O trace confirmou `CreateWindowExA ... status="success"`, processamento
  de menus, toolbar e pintura dos painéis. O processo terminou por timeout
  controlado (`124`) após 8 segundos, sem classificar isso como falha funcional.
- [x] O teste foi encerrado com limpeza do Xvfb e sem alterar prefixo persistente;
  a ausência de X11 continua sendo skip ambiental fora desse cenário.

Conclusão: a GUI mínima experimental funciona em X11 virtualizado. Isso não
promove cobertura geral de Win32, operações completas do 7-Zip ou suporte a
outros aplicativos gráficos.

Objetivo: distinguir falha do aplicativo de ausência de ambiente gráfico.

Tarefas:

- [x] Repetir `7zFM_x64.exe` em X11/Xvfb funcional.
- [x] Registrar timeout, memória, exit code, stdout, stderr, trace e limpeza.
- [x] Separar skips ambientais de falhas funcionais.

Aceitação:

- [x] O resultado reproduzível identifica explicitamente que o cenário sem
  X11 é ambiental e que o cenário Xvfb cria a GUI e chega ao loop.
- [x] Nenhuma conclusão de compatibilidade depende de uma sessão gráfica ausente.

### A6 concluído — instaladores e fluxo de instalação

Evidência reproduzível de 2026-09-07:

- [x] O `--report` analisou oito candidatos. `7-Zip_x64_Installer.exe`,
  `CapCut_..._installer.exe`, `Creative_Cloud_Set-Up_7474.exe`,
  `EpicInstaller-...exe`, `Notepad++_x64_Installer.exe` e `RTSSSetup737.exe`
  retornaram exit `5`, `unsupported-architecture`, máquina `0x14c`.
  `RobloxPlayerInstaller.exe` e `lghub_installer.exe` passaram a análise PE32+
  x86-64.
- [x] O comando `install` testou Roblox e G HUB em prefixos próprios, com
  `--cpu 3`, `--memory 512` e timeout externo de 15 segundos. Roblox encerrou
  com exit `3` após `RBXCRASH`; G HUB encerrou com exit `1` por `ExitProcess`.
  Nenhum dos dois deixou arquivos do prefixo ou catálogo fora do diretório
  temporário.
- [x] O `install` do Affinity foi testado separadamente no prefixo temporário
  e terminou com exit `4` antes da extração, pela rejeição ZIP64/limite já
  documentada em A4.
- [x] Não foram executados indiscriminadamente os instaladores x86 ou setups
  que já falham na arquitetura; cada falha permaneceu controlada e sem
  cadastro de aplicação.

Conclusão: o fluxo de instalação mantém análise, execução e materialização
separadas. Nenhum instalador testado nesta rodada foi promovido como suporte
funcional; os próximos ganhos exigem primeiro PE32/x86, APIs faltantes ou
fluxos específicos dos instaladores.

Objetivo: validar instalação de forma isolada, sem executar indiscriminadamente
instaladores obtidos da internet.

Tarefas:

- [x] Selecionar instaladores por classe e analisar primeiro com `--report`.
- [x] Executar apenas com prefixo temporário, timeout externo, limite de
  memória e limpeza garantida.
- [x] Verificar catálogo, permissões, extração, PE interno e `app run` quando
  aplicável; as falhas ocorreram antes de cadastrar uma aplicação.
- [x] Cobrir o caminho MSIX com parse, extração e validação separados.

Aceitação:

- [x] Cada instalador testado tem comando, resultado e diagnóstico registrados;
  fixtures do parser permanecem cobrindo as decisões estruturais.
- [x] Falhas não deixaram arquivos, catálogo ou prefixo parcial nos diretórios
  temporários observados.

### A7 concluído — proveniência e integridade dos downloads

Objetivo: tornar o corpus reproduzível e distinguir arquivos oficiais de
artefatos apenas disponíveis localmente.

Tarefas:

- [x] Registrar origem e SHA-256 do CPU-Z, HWMonitor e GPU-Z, incluindo a
  fonte do hash quando o fornecedor não o exibe na página de download.
- [x] Reproduzir o `HWiNFO64.exe` local a partir do pacote portátil HWiNFO
  8.52 e confirmar a igualdade byte a byte.
- [x] Preservar versão, arquitetura e data de cada arquivo no inventário.

Auditoria realizada em 2026-09-07:

| Arquivo | Versão/arquitetura | Data local | Fonte declarada | SHA-256 local | Evidência |
| --- | --- | --- | --- | --- | --- |
| `CPU-Z_2.18_en.exe` | 2.18 / PE32 x86 | 2026-09-07 | [CPUID](https://download.cpuid.com/cpu-z/cpu-z_2.18-en.exe) | `3999dad2516dbc9afdd51defc2447d940aa78a88bcf93655186c856b326e3821` | download oficial reproduzido byte a byte; também coincide com o manifesto `CPUID.CPU-Z/2.18` do [WinGet](https://github.com/microsoft/winget-pkgs/tree/master/manifests/c/CPUID/CPU-Z/2.18) |
| `GPU-Z_2.70.0.exe` | 2.70.0 / PE32 x86 | 2026-09-07 | [TechPowerUp](https://www.techpowerup.com/gpuz/) | `6cb0ef29682452de81a9576808881685161411a1fad00938ba04131159979c29` | coincide com o hash publicado no manifesto `TechPowerUp.GPU-Z/2.70.0` do [WinGet](https://github.com/microsoft/winget-pkgs/tree/master/manifests/t/TechPowerUp/GPU-Z/2.70.0) |
| `HWMonitor_1.67.exe` | 1.67 / PE32 x86 | 2026-09-07 | [CPUID](https://download.cpuid.com/hwmonitor/hwmonitor_1.67.exe) e [SAC](https://www.sac.sk/download/utildiag/hwm167.exe) | `8c6799f8ece4ab5846cc8beddcf52bd887a5ae896279652960ef8d943d453473` | `hwm167.exe` do SAC tem 3.631.600 bytes e é byte-a-byte igual ao local; o hash também coincide com o manifesto `CPUID.HWMonitor/1.67` do [WinGet](https://github.com/microsoft/winget-pkgs/tree/master/manifests/c/CPUID/HWMonitor/1.67). O endpoint CPUID consultado em 2026-09-07 entregou outro PE32 de 2.555.904 bytes, hash `e7cb1f99f0fb4758161335075762c142c98955faa22e8dba358de8549cfa89e0` |
| `HWiNFO64.exe` | 8.52 / PE32+ x86-64 | 2026-08-25 | [HWiNFO](https://www.hwinfo.com/download/) e pacote portátil [SAC](https://www.sac.sk/download/utildiag/hwi_852.zip) | `39292da56747eaed8b6025ba5e231e096e5792708bf4671308c2e700ed059cc0` | o ZIP de 20.828.120 bytes tem SHA-256 `0ce80064422e128a0f4257733c41e02970b5997a04361accc1f7f2f4b059f1ed`, coincide com o manifesto [Scoop](https://github.com/ScoopInstaller/Extras/blob/master/bucket/hwinfo.json), passa `unzip -t` e produz um executável byte-a-byte igual ao local |

Os arquivos locais foram baixados originalmente de um artefato identificado
apenas por `downloaded_from_www.guru3d.com.txt`; esse marcador não contém uma
URL individual por arquivo. A reconstrução foi feita pelos URLs pinados acima:
CPU-Z, GPU-Z, HWMonitor e o ZIP portátil HWiNFO. O endpoint CPUID atual do
HWMonitor entregou um binário diferente, mas o artefato SAC reproduziu o
arquivo local e preservou o hash publicado no manifesto. O inventário mantém
essa divergência registrada em vez de ocultá-la.

Aceitação:

- [x] O corpus usado nos testes pode ser reconstruído sem depender de nomes
  ambíguos ou downloads não documentados.

### A8 — Decisão independente sobre PE32/x86

Objetivo: decidir o futuro dos 12 executáveis PE32 sem ampliar o escopo atual
por acidente.

Tarefas:

- [x] Manter a rejeição `Unsupported` para PE32/x86 enquanto não houver fase
  própria ou autorização explícita.
- [x] Avaliar custo, arquitetura e impacto no loader em
  `docs/arquitetura/pe32-x86-decision.md`.
- [x] Registrar que nenhuma implementação será iniciada antes de atualizar
  `ROADMAP.md`, contratos, compatibilidade e testes, com autorização explícita.

Decisão registrada em 2026-09-07:

- PE32/x86 continua fora do escopo operacional;
- o parser deve rejeitar a arquitetura antes de mapear ou executar;
- a abertura de uma fase x86 exige desenho de execução, ABI, fixtures e
  autorização própria;
- PE32+ AMD64, Proton e os caminhos já suportados não mudam.

Aceitação:

- [x] Existe uma decisão registrada para manter PE32/x86 fora do escopo, com
  critérios técnicos de reabertura.

## Rodada B — matriz completa do corpus após A1–A8

Esta rodada usa o corpus atual de `/home/tonho/Área de trabalho/Aplicativos_Windows_Populares/`,
com 24 executáveis PE, 2 DLLs PE e 1 pacote MSIX. Cada cenário será executado
com prefixo temporário, timeout externo, limite de memória e limpeza verificada.
Arquivos que já falham por arquitetura, formato ou análise estrutural serão
registrados pelo `--report` e não serão executados indiscriminadamente.

### B1 — Análise completa Rust/C++

Objetivo: produzir uma matriz reproduzível de `--report` para todos os PE e
para o MSIX, comparando Rust ON com o baseline C++ OFF.

Tarefas:

- [ ] Inventariar novamente nome, SHA-256, tipo PE/MSIX e arquitetura.
- [ ] Executar `--report` em Rust ON para os 24 executáveis e as 2 DLLs.
- [ ] Executar o mesmo corpus em Rust OFF e comparar status, categoria,
  diagnóstico, imports, seções e ausência de fallback.
- [ ] Executar `--report` do MSIX com trace, registrando o limite ou formato
  que determinar o resultado.

Aceitação:

- [ ] Cada arquivo tem exit code, status, trace e diagnóstico salvos fora do
  repositório e resumidos no roadmap.
- [ ] Nenhum arquivo é classificado como suportado sem equivalência ON/OFF e
  sem registro em `docs/compatibilidade.md`.

### B2 — Execução controlada de PE32+ nativo

Objetivo: separar análise aprovada de execução efetivamente verificada.

Tarefas:

- [ ] Selecionar somente executáveis PE32+ não-DLL que passaram no report.
- [ ] Executar cada candidato com prefixo temporário, timeout externo,
  `--cpu`, `--memory` e captura separada de stdout/stderr.
- [ ] Repetir a matriz com Rust ON e Rust OFF quando o caminho existir.
- [ ] Registrar `supported`, `execution-failed`, `timeout`, `signal` ou
  `map-failed` sem promover compatibilidade por semelhança.

Aceitação:

- [ ] Toda execução termina de forma controlada ou é interrompida pelo limite
  externo, sem deixar prefixos ou catálogos persistentes.
- [ ] O trace demonstra que falhas de parsing ocorrem antes de mapeamento e
  execução.

### B3 — Instalação seletiva e segura

Objetivo: testar instalação somente depois da análise e sem executar
indiscriminadamente instaladores obtidos da internet.

Tarefas:

- [ ] Classificar instaladores por arquitetura, tipo e risco antes de chamar
  `install`.
- [ ] Testar somente candidatos PE32+ ou pacotes MSIX aprovados, sempre em
  prefixo temporário com timeout, memória limitada e limpeza.
- [ ] Confirmar extração, catálogo, permissões, executável principal e
  ausência de cadastro parcial após erro.
- [ ] Manter instaladores PE32/x86, .NET/Mono e pacotes rejeitados como
  `unsupported`/`malformed`, sem fallback implícito.

Aceitação:

- [ ] Cada instalação tem resultado de análise, extração, catálogo, limpeza,
  stdout, stderr e exit code documentados.
- [ ] Nenhuma falha de instalação é promovida a suporte sem execução do
  aplicativo instalado.

### B4 — Matriz de solicitações e compatibilidade

Objetivo: garantir que o TradutorLinux identifique cada solicitação do corpus
como análise, execução, instalação ou arquivo não executável, com diagnóstico
consistente.

Tarefas:

- [ ] Criar uma tabela por arquivo com ação solicitada, backend, arquitetura,
  status, exit code, limitação e próxima ação.
- [ ] Atualizar `docs/compatibilidade.md` somente com evidência reproduzível.
- [ ] Registrar no trace as diferenças Rust/C++ e os skips ambientais
  permitidos, sem reclassificar falha funcional como skip.
- [ ] Criar fixtures ou testes de regressão para cada correção que surgir da
  matriz, antes de alterar o runtime.

Aceitação:

- [ ] A matriz completa pode ser repetida sem depender de estado persistente,
  nomes ambíguos ou execução manual não registrada.
- [ ] O roadmap contém um resumo verificável para todos os arquivos do corpus.

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
