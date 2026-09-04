# Próximas etapas — continuidade no Linux real

Atualizado em 2026-09-04.

Este arquivo registra a sequência de trabalho e o resultado da retomada no Linux
real. As etapas executáveis foram validadas no build Debug; as limitações e os
itens que dependem de amostra comercial ou decisão de escopo permanecem
explícitos abaixo.

## Estado atual

- Commits desta retomada: `ce66739`, `4f95c65`, `2fa9ebd`, `d9aeb9f`,
  `956aadc`, `17f4305`, `45cc3f9`, `990b960`, `6d7c855`, `3df05e4`,
  `ee1fec9`, `5fe3e6d`, `a1fe77f`, `144be0d` e `03085a7`.
- O alvo continua sendo PE32+ x86-64 em Linux x86-64.
- Já foram implementados e testados no build Debug Linux os subconjuntos de `IPHLPAPI`, `WTSAPI32` e parte de `CRYPT32`, além das correções de forwarders, TLS genérico e parser de manifests MSIX.
- A validação executada até aqui cobriu fixtures próprias, testes unitários direcionados e traces de `tl_worker_rsl` e `tl_powr`.
- Isso ainda não autoriza declarar suporte completo a Roblox, Worker/RSL ou qualquer aplicativo comercial. A matriz de compatibilidade deve continuar refletindo o resultado real de execução ponta a ponta.
- Dependências que foram necessárias no ambiente Linux anterior: `zlib1g-dev` e `xvfb`. No Linux real, confirme também CMake, compilador C++20, ferramentas PE/MinGW e Qt/X11 exigidos pelo projeto.

## Resultado desta execução (2026-09-04)

- [x] Baseline: 10 testes unitários direcionados passaram e 1 foi skip
  controlado por ausência de interface IPv4; os quatro testes CTest de
  metadata/execução passaram, aceitando exit `77` como skip documentado para
  `tl_powr`/`tl_worker_rsl` sem IPv4.
- [x] X11: `x11_popup_smoke` passou os caminhos de Escape, clique externo,
  destruição externa e timeout; `runtime_gui_smoke` passou as fixtures de
  janela, teclado, timer, GDI, pintura e diálogo sob Xvfb.
- [x] Correção de diagnóstico GUI no 7-Zip: `GetClassInfoW` deixou de
  retornar sucesso falso para classes ausentes; classes próprias filhas agora
  entram no ciclo básico de `WM_CREATE`/`WM_PAINT`.
- [x] Primeiro shell visual do `7zFM_x64.exe`: o renderer específico de
  `7-Zip::FM` desenha menu, toolbar, endereço, navegação lateral, lista de
  arquivos e status em X11. Também normaliza a geometria impossível entregue
  pelo aplicativo (`22731,-1163111472,7029x272`) para `800x600`, registrando a
  ocorrência no trace. A lista agora mostra, em modo somente leitura, as
  entradas imediatas do diretório que contém o executável, sem recursão ou
  seguimento de links simbólicos, com limite de 128 linhas. A interface é
  navegável apenas visualmente; comandos, menus reais, ícones e navegação do
  painel ainda não estão ligados ao convidado.
  O aplicativo continua explicitamente fora de suporte como fluxo GUI concluído.
- [x] MSIX: 8 testes `MsixParserTest.*` e o teste de afinidade passaram no
  unitário e no CTest. A validação é estrutural; não houve instalação ou
  execução de .NET/MSIX.
- [x] Forwarders, delay-import e carregamento dinâmico: 29 testes unitários e
  14 testes CTest direcionados passaram.
- [x] TLS genérico: o teste unitário e os quatro testes CTest passaram. A
  fixture emite explicitamente os registros PE de TLS porque o toolchain
  MinGW usado sem CRT não gerou esse diretório automaticamente.
- [x] Manutenção P2.4: helper de permissões de memória centralizado e
  constantes Win32 nomeadas; 41 testes direcionados passaram.
- [x] Manutenção da suíte Win32: `tests/test_win32.cpp` foi separado por
  domínio em arquivos de GUI, segurança, APIs externas e aplicativos, com
  helpers compartilhados e os mesmos 356 testes/79 suítes preservados. A
  validação também corrigiu o relatório `--report` para exibir `support=` em
  cada import resolvida.
- [x] Auditoria de `ExportSupport::Stub`: retornos, `LastError`, buffers e
  traces dos stubs de processo, impressão, WTS, SetupAPI, SensApi, DirectX,
  WINMM e USER32 foram registrados e protegidos por 5 testes direcionados;
  o unitário completo ficou em 361 testes/80 suítes, com 360 aprovados e 1
  skip ambiental; DirectX, GPU, áudio e impressão continuam fora de suporte
  funcional.
- [x] Listagem controlada do shell 7-Zip: a suíte unitária passou com 366 testes
  (365 aprovados e 1 skip ambiental); três testes protegem diretório imediato,
  ordenação, limite de 128 linhas e caminho ausente. O renderer usa somente o
  diretório absoluto do executável aberto e não conecta comandos de arquivo.
- [x] Ponte GDI para controles lógicos: `TextOut`, `FillRect` e `Rectangle`
  agora desenham na superfície X11 da janela principal com os offsets
  acumulados dos pais; dois testes protegem projeção aninhada e rejeição de
  handles órfãos. Isso prepara a tradução de controles reais sem criar uma
  janela X11 separada para cada filho Win32.
- [x] Controles comuns mínimos: `CreateStatusWindowW` e `CreateToolbarEx`
  agora criam filhos lógicos reais, validam o parent e o vetor `TBBUTTON`,
  renderizam status/toolbar na superfície compartilhada e encaminham cliques
  da toolbar por `WM_COMMAND`; a suíte ficou em 369 testes (368 aprovados e
  1 skip ambiental), sem declarar suporte amplo ao `COMCTL32`.
- [x] Medições e catálogo: o catálogo registra os `298/298` imports do
  `7zFM_x64.exe`, a janela X11 real `800x600`, a normalização de geometria e
  os dois smokes GUI aprovados, sem transformar essa evidência visual em
  suporte funcional.
- [ ] Worker/RSL comercial: bloqueado nesta cópia do checkout; a busca não
  encontrou um executável comercial `Worker`/`RSL` em
  `Aplicativos_Windows_Populares/`. Só existe a fixture própria
  `tl_worker_rsl.exe`, que retorna `77` de forma controlada neste host sem
  IPv4. Nenhum estado comercial foi promovido.
- [ ] Manutenção restante: limites configuráveis de recursos e a camada
  genérica de tradução continuam backlog. Os limites de recursos e a camada de
  tradução exigem decisão de escopo antes de implementação.

## Ordem de execução

### 1. Reproduzir o baseline no Linux real

No diretório do projeto:

```bash
git status --short
git log -5 --oneline
```

Reutilize o build Debug existente, se ele já estiver configurado. Se não estiver, configure-o conforme os presets e instruções do projeto. Não crie um preset novo sem necessidade.

Compile somente os alvos inicialmente afetados, com baixo paralelismo:

```bash
cmake --build build/debug --target tradutorlinux_unit_tests tradutorlinux_samples --parallel 2
```

Rode os testes da etapa atual:

```bash
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='IphlpapiTest.*:WtsApiTest.*:Crypt32Test.CertContextAndStoreManagement:MsixParserTest.*'
ctest --test-dir build/debug -R '^(fixture_tl_powr_metadata|runtime_tl_powr_matches_readobj|fixture_tl_worker_rsl_metadata|runtime_tl_worker_rsl_matches_readobj)$' --output-on-failure
```

Resultado: passou no Debug; sem interface IPv4, `tl_powr` e
`tl_worker_rsl` terminam com `77` e o CTest os aceita como skip controlado.

### 2. Validar definitivamente o caminho X11 e os popups

Depois de confirmar que `xvfb` está instalado, reconfigure o build para que o CMake detecte o executável:

```bash
cmake -S . -B build/debug
cmake --build build/debug --target x11_popup_smoke --parallel 2
ctest --test-dir build/debug -R '^x11_popup_smoke$' --output-on-failure
ctest --test-dir build/debug -N | grep -E 'runtime_gui_smoke|simple_todo_gui_smoke|x11_popup_smoke'
```

`x11_popup_smoke` e `runtime_gui_smoke` apareceram e passaram sob Xvfb. O alvo
`simple_todo_gui_smoke` não está registrado como teste neste build, portanto
não é declarado como validado nesta etapa. Só rode a variante `sanitize` se ela
já existir configurada; não a configure automaticamente em uma máquina
limitada.

Após a validação, atualizar `ANALISE-CRITICA.md`, `ROADMAP.md` e
`docs/compatibilidade.md`; esta retomada já registrou essas evidências.

### 3. Fechar a validação do parser MSIX/DEFLATE

```bash
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='MsixParserTest.*:Kernel32SystemTest.TimeZoneProcessAndAffinity'
ctest --test-dir build/debug -N | grep -Ei 'msix|affinity|package'
ctest --test-dir build/debug -R 'MsixParserTest|Kernel32SystemTest.TimeZoneProcessAndAffinity|affinity' --output-on-failure
```

O objetivo desta etapa é inspecionar manifests e dependências com segurança. Não instalar nem executar .NET/MSIX como se isso fosse suporte de execução.

Resultado: os 9 testes direcionados passaram no unitário e no CTest; a
instalação e a execução de .NET/MSIX continuam fora do escopo.

### 4. Validar forwarders, imports dinâmicos e delay-import

```bash
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='ModuleTest.*:ImportResolverTest.*'
ctest --test-dir build/debug -N | grep -Ei 'delay|dynload|report'
ctest --test-dir build/debug -R '^(ImportResolverTest\.(ResolvesDelayImportsAndWritesIat|ReportsEveryUnresolvedDelayImport|RestoresDelayIatPagePermissionsAfterPatch)|ModuleTest\.(ResolvesMultiHopForwarderByNameAndOrdinal|RejectsForwarderCyclesAndMissingTargets)|PeReaderTest\.(ParsesDelayImportsByNameAndOrdinal|RejectsDelayImport.*)|fixture_tl_delay_import_metadata|fixture_tl_dynload_metadata|runtime_tl_dynload_matches_readobj|report_tl_dynload_support)$' --output-on-failure
```

Resultado: 29 testes unitários e 14 testes CTest direcionados passaram.

Confirmar especialmente:

- forwarder encadeado, forwarder ausente e ciclo terminam com diagnóstico controlado;
- a política atual de delay-import continua explícita e testada;
- `GetProcAddress` não seja tratado como busca global correta quando a identidade da DLL for necessária;
- o trace informe módulo, símbolo, mecanismo e limitação conhecida.

### 5. Validar TLS genérico

```bash
ctest --test-dir build/debug -R '^(fixture_tl_tls_generic_metadata|runtime_tl_tls_generic_matches_readobj|report_tl_tls_generic_support)$' --output-on-failure
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='Win32ConcurrencyTest.PointerBackedTlsSlotIsZeroInitializedAndReleased'
```

Resultado: o unitário de TLS e os quatro testes CTest passaram no Debug. A
fixture contém registros PE TLS explícitos para manter o contrato verificável
sem depender de CRT; a variante `sanitize` não foi rerodada nesta retomada.

### 6. Reexecutar o Worker/RSL real com trace

Primeiro localizar o executável disponível no checkout:

```bash
find Aplicativos_Windows_Populares -type f \( -iname '*worker*.exe' -o -iname '*rsl*.exe' \)
```

Então executar somente após as validações anteriores:

```bash
./build/debug/src/tradutorlinux --trace=pe,loader,imports,runtime,process CAMINHO_DO_EXECUTAVEL.exe
```

Os canais válidos do CLI devem ser conferidos em `--help`; referências antigas a `ws2` ou `crypt` não devem ser copiadas sem verificar o parser atual. Registrar o caminho exato, stdout, exit code e o primeiro bloqueio reproduzível.

Não mudar o estado de compatibilidade de um aplicativo comercial apenas por resolver imports ou iniciar o processo. Para cada nova API necessária, seguir o ciclo: aplicativo-alvo ou fixture, implementação mínima, teste de regressão, trace, atualização da matriz e documentação.

Resultado: nenhuma amostra comercial correspondente foi encontrada no
checkout, então a execução não foi tentada. A única ocorrência relevante é a
fixture gerada `build/debug/tests/samples/generated/tl_worker_rsl.exe`.

### 7. Manutenção após fechar os bloqueios funcionais

As validações acima foram concluídas, mas a manutenção foi separada por risco:

- [x] eliminar o helper de permissões duplicado e nomear as constantes Win32;
- [x] dividir `tests/test_win32.cpp` em suítes por domínio, preservando nomes e cobertura;
- [x] auditar APIs marcadas como `ExportSupport::Stub`, adicionando testes de retorno, `LastError`, buffers de saída e trace;
- [ ] revisar limites de recursos do processo convidado, se a decisão de escopo for aprovada;
- [x] corrigir contradições históricas em `docs/compatibilidade.md`, especialmente linhas que diziam “suportado” enquanto a matriz atual registra `execution-failed`;
- [x] atualizar medições e o catálogo de aplicativos somente com evidência reproduzível.

## Regra para cada etapa concluída

1. Alterar somente o código, testes e documentos diretamente relacionados.
2. Validar no Linux real com paralelismo máximo 2.
3. Rodar `git diff --check` e revisar `git status`.
4. Criar um commit separado por etapa concluída, sem incluir artefatos gerados.
5. Não fazer push sem solicitação explícita.
