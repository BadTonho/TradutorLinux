# Roadmaps legados do TradutorLinux

Este arquivo preserva integralmente os três roadmaps anteriores. Ele é somente histórico; o novo roadmap será definido em documento separado.

---

## `ROADMAP-APLICATIVOS.md` (legado)

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

## Resultado de referência — 2026-09-12

| Grupo | Resultado atual | Próxima ação |
|---|---|---|
| `--report` | Matriz do corpus: 27/27 casos passaram com paridade Rust ON/C++ OFF | Preservar a separação entre análise e execução |
| Execução | 7-Zip CLI, smoke do 7zFM, uma execução bem-sucedida da operação `7zG`, extração/SFX do WinRAR e smokes controlados de PuTTY/Notepad++ passaram; repetições de `7zG` ainda terminam intermitentemente em timeout | Avançar somente com novo cenário reproduzível |
| GUI | 7zFM, WinRAR SFX, PuTTY e Notepad++ foram sondados sob Xvfb próprio; os smokes de PuTTY/Notepad++ validam diagnósticos controlados, não suporte funcional | Ampliar interação somente após observar um contrato genérico real |
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

### B1 concluído — análise completa Rust/C++

Objetivo: produzir uma matriz reproduzível de `--report` para todos os PE e
para o MSIX, comparando Rust ON com o baseline C++ OFF.

Tarefas:

- [x] Inventariar novamente nome, SHA-256, tipo PE/MSIX e arquitetura.
- [x] Executar `--report` em Rust ON para os 24 executáveis e as 2 DLLs.
- [x] Executar o mesmo corpus em Rust OFF e comparar status, categoria,
  diagnóstico, imports, seções e ausência de fallback.
- [x] Executar `--report` do MSIX com trace, registrando o limite ou formato
  que determinar o resultado.

Evidência reproduzível de 2026-09-07:

- [x] Foram analisados 26 arquivos PE e 1 MSIX em
  `/tmp/tl-matrix-b1/results.tsv`, com Rust ON em `build/debug-rust` e
  C++ OFF em `build/debug`.
- [x] Os dois backends produziram a mesma distribuição: 13 entradas com
  exit `0`, 12 com `Unsupported`/exit `5` e 2 com `Malformed`/exit `4`.
- [x] O stdout foi byte-a-byte igual nas 27 entradas. Nos sucessos, os
  diagnósticos foram semanticamente iguais após remover somente
  `backend="rust"`; as mensagens de erro continuam podendo diferir sem
  alterar categoria ou exit code.
- [x] Os 12 PE32/x86 foram rejeitados por arquitetura, HWiNFO64 foi rejeitado
  por export empacotado sem bytes crus e o MSIX Affinity foi rejeitado pelo
  limite/estrutura já documentado.
- [x] O trace Rust registrou `backend="rust"` em 891 eventos e os quatro
  casos de rejeição verificados não registraram mapeamento ou execução.

Aceitação:

- [x] Cada arquivo tem exit code, status, trace e diagnóstico salvos fora do
  repositório e resumidos no roadmap.
- [x] Nenhum arquivo é classificado como suportado sem equivalência ON/OFF e
  sem registro em `docs/compatibilidade.md`.

### B2 concluído — execução controlada de PE32+ nativo

Objetivo: separar análise aprovada de execução efetivamente verificada.

Tarefas:

- [x] Selecionar somente executáveis PE32+ não-DLL que passaram no report.
- [x] Executar cada candidato com prefixo temporário, timeout externo,
  `--cpu`, `--memory` e captura separada de stdout/stderr.
- [x] Repetir a matriz com Rust ON e Rust OFF quando o caminho existir.
- [x] Registrar `supported`, `execution-failed`, `timeout`, `signal` ou
  `map-failed` sem promover compatibilidade por semelhança.

Evidência reproduzível de 2026-09-07:

- [x] Foram tentados em Rust ON e C++ OFF `7zFM_x64.exe`,
  `7z_x64.exe`, `Rockstar-Games-Launcher.exe`, `Rufus_x64.exe`,
  `WinRAR_x64.exe`, `winrar-x64-723.exe`, `notepad++.exe` e `putty_x64.exe`.
- [x] Rust e C++ produziram stdout e exit code iguais nos casos válidos:
  7-Zip CLI/WinRAR `0`, Rockstar `3` e Rufus `4` (`map-failed` por entry
  point não executável). A tentativa inicial de GUI foi invalidada porque o
  Xvfb não iniciou; ela foi repetida em C2 com `xdpyinfo` confirmando Xvfb
  funcional. Nessa rodada, 7-Zip File Manager e PuTTY terminaram por timeout
  controlado (`72`), e Notepad++ por SIGSEGV controlado (`71`), com resultados
  iguais nos dois backends. Esses resultados são evidência de execução sob
  X11, mas não de compatibilidade concluída.
- [x] As execuções usaram timeout externo de 15 s, `--timeout 3`,
  `--cpu 3`, `--memory 512` e `APPDATA` isolado por caso. Nenhum diretório
  temporário recebeu arquivos persistentes e nenhum processo Xvfb ficou ativo.
- [x] Os instaladores PE32+ `RobloxPlayerInstaller.exe`,
  `Logitech_GHUB_x64.exe` e `lghub_installer.exe` não foram executados como
  aplicativos; foram reservados para a validação de instalação B3.
- [x] A evidência funcional anterior de GUI continua sendo somente a A5,
  que validou `7zFM_x64.exe` sob Xvfb iniciado com sucesso; a rodada B não
  substitui esse resultado por uma execução em display indisponível.

Aceitação:

- [x] Toda execução termina de forma controlada ou é interrompida pelo limite
  externo, sem deixar prefixos ou catálogos persistentes.
- [x] O trace demonstra que falhas de parsing ocorrem antes de mapeamento e
  execução.

### B3 concluído — instalação seletiva e segura

Objetivo: testar instalação somente depois da análise e sem executar
indiscriminadamente instaladores obtidos da internet.

Tarefas:

- [x] Classificar instaladores por arquitetura, tipo e risco antes de chamar
  `install`.
- [x] Testar somente candidatos PE32+ ou pacotes MSIX aprovados, sempre em
  prefixo temporário com timeout, memória limitada e limpeza.
- [x] Confirmar extração, catálogo, permissões, executável principal e
  ausência de cadastro parcial após erro.
- [x] Manter instaladores PE32/x86, .NET/Mono e pacotes rejeitados como
  `unsupported`/`malformed`, sem fallback implícito.

Evidência reproduzível de 2026-09-07:

- [x] Foram executados em Rust ON e C++ OFF `RobloxPlayerInstaller.exe`,
  `Logitech_GHUB_x64.exe`, `lghub_installer.exe` e `Affinity x64.msix`, com
  `install`, timeout externo de 20 s, `--cpu 3`, `--memory 512`, prefixo e
  `APPDATA` isolados.
- [x] Os resultados foram iguais nos dois backends: Roblox exit `3` após
  `RBXCRASH`, G HUB exit `1` por término do convidado e Affinity exit `4`
  antes da extração por rejeição MSIX/limite documentado.
- [x] Cada caso registrou `prefix_files=0` e `appdata_files=0` antes da
  limpeza; não houve catálogo, executável principal ou cadastro parcial.
- [x] `Logitech_GHUB_x64.exe` e `lghub_installer.exe` têm o mesmo SHA-256
  `4b2f9903b27c8434afcd52fe65845632fcae47cc50432fb6b3b1637144e811e1` e são
  byte-a-byte iguais.
- [x] Os 12 instaladores PE32/x86 foram mantidos fora da execução e continuam
  rejeitados por `Unsupported`/exit `5` no B1; nenhum instalador .NET/Mono ou
  PE32 foi usado como atalho para ampliar o escopo.

Aceitação:

- [x] Cada instalação tem resultado de análise, extração, catálogo, limpeza,
  stdout, stderr e exit code documentados.
- [x] Nenhuma falha de instalação é promovida a suporte sem execução do
  aplicativo instalado.

### B4 concluído — matriz de solicitações e compatibilidade

Objetivo: garantir que o TradutorLinux identifique cada solicitação do corpus
como análise, execução, instalação ou arquivo não executável, com diagnóstico
consistente.

Tarefas:

- [x] Criar uma tabela por arquivo com ação solicitada, backend, arquitetura,
  status, exit code, limitação e próxima ação.
- [x] Atualizar `docs/compatibilidade.md` somente com evidência reproduzível.
- [x] Registrar no trace as diferenças Rust/C++ e os skips ambientais
  permitidos, sem reclassificar falha funcional como skip.
- [x] Criar fixtures ou testes de regressão para cada correção que surgir da
  matriz, antes de alterar o runtime.

Matriz consolidada de 2026-09-07:

`ON/OFF` indica Rust ON e C++ OFF; `—` indica que a ação não se aplica ou foi
deliberadamente não executada.

| Arquivo | Tipo/arquitetura | `--report` ON/OFF | Execução ON/OFF | `install` ON/OFF | Classificação |
| --- | --- | --- | --- | --- | --- |
| `7-Zip_x64_Installer.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `7z.dll` | PE32+ DLL | 0/0 | — | — | DLL analisada; não executada como aplicativo |
| `7zFM_x64.exe` | PE32+ GUI | 0/0 | 72/72 sob Xvfb válido | — | GUI iniciou; timeout controlado sem cenário de interação; A5 mantém smoke funcional |
| `7z_x64.exe` | PE32+ CLI | 0/0 | 0/0 | — | execução controlada |
| `CPU-Z_2.18_en.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `CapCut_7677236283084898320_installer.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `Creative_Cloud_Set-Up_7474.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `EpicInstaller-20.1.4-831cc1564f92442abc51fdb4a9854359.exe` | PE32 x86/.NET | 5/5 | — | — | análise; arquitetura/formato fora do escopo |
| `Everything_Search_x64.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `GPU-Z_2.70.0.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `HWMonitor_1.67.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `HWiNFO64.exe` | PE32+ x86-64 empacotado | 4/4 | — | — | malformado para análise estática; sem desempacotador |
| `Logitech_GHUB_x64.exe` | PE32+ instalador | 0/0 | — | 1/1 | instalação controlada; convidado encerrou com 1 |
| `Notepad++_x64_Installer.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `RTSS.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `RTSSHooks64.dll` | PE32+ DLL | 0/0 | — | — | DLL analisada; não executada como aplicativo |
| `RTSSSetup737.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `RobloxPlayerInstaller.exe` | PE32+ instalador | 0/0 | — | 3/3 | instalação controlada; `RBXCRASH` |
| `Rockstar-Games-Launcher.exe` | PE32+ aplicativo | 0/0 | 3/3 | — | execução controlada; `ExitProcess 3` |
| `Rufus_x64.exe` | PE32+ aplicativo empacotado | 0/0 | 4/4 | — | `map-failed` por W^X/entry point |
| `WinRAR_x64.exe` | PE32+ aplicativo | 0/0 | 0/0 | — | execução controlada |
| `lghub_installer.exe` | PE32+ instalador | 0/0 | — | 1/1 | alias byte-a-byte do G HUB |
| `notepad++.exe` | PE32+ GUI | 0/0 | 124/124 sob Xvfb válido após D1 | — | APIs `lstr*W` corrigidas para UTF-16 guest; janela criada e timeout controlado, ainda sem cenário de interação |
| `officedeploymenttool_20228-20124.exe` | PE32 x86 | 5/5 | — | — | análise; arquitetura não suportada |
| `putty_x64.exe` | PE32+ GUI | 0/0 | 72/72 sob Xvfb válido | — | `PuTTYTimerWindow` foi criada; timeout controlado sem cenário de interação |
| `winrar-x64-723.exe` | PE32+ aplicativo | 0/0 | 0/0 | — | execução controlada; duplicata do WinRAR |
| `Affinity x64.msix` | MSIX/ZIP64 | 4/4 | — | 4/4 | pacote rejeitado antes da extração |

Os resultados de B1–B3 foram gravados em `/tmp/tl-matrix-b1`,
`/tmp/tl-matrix-b2` e `/tmp/tl-matrix-b3`. Eles não são dependências do
runtime e podem ser regenerados a partir do corpus pinado e dos comandos do
roadmap.

Aceitação:

- [x] A matriz completa pode ser repetida sem depender de estado persistente,
  nomes ambíguos ou execução manual não registrada.
- [x] O roadmap contém um resumo verificável para todos os arquivos do corpus.

## Rodada C — redução das limitações observadas

A rodada C começa somente depois da matriz completa B1–B4. O objetivo é
transformar falhas reproduzíveis em trabalho técnico pequeno, com fixture e
regressão antes de qualquer alteração no runtime. PE32/x86, .NET/Mono,
desempacotamento de HWiNFO e compatibilidade universal continuam fora do
escopo.

### C1 — Triagem fixture-driven dos bloqueios nativos

Objetivo: distinguir limitação conhecida, falha de API e falha do aplicativo
sem alterar o loader por tentativa.

Tarefas:

- [x] Preservar fixtures ou logs mínimos para `Rufus`/W^X,
  `Notepad++`/SIGSEGV, `PuTTY`/timeout e `Rockstar`/exit `3`.
- [x] Comparar traces Rust ON/C++ OFF por etapa: parsing, mapeamento,
  imports, TLS, GUI, processo e término.
- [x] Mapear cada bloqueio a uma API, mecanismo ou política já existente;
  não adicionar uma API sem alvo e teste de regressão.
- [x] Confirmar se o bloqueio ocorre antes ou depois do entry point e se há
  risco de relaxar W^X, limites ou isolamento.

Evidência: [triagem dos bloqueios do corpus](docs/arquitetura/aplicativos-bloqueios.md).

Aceitação:

- [x] Cada bloqueio tem uma hipótese testável, uma fixture ou log mínimo e
  uma decisão explícita: corrigir, manter rejeitado ou pedir autorização de
  escopo.
- [x] Nenhum aplicativo é promovido a suportado durante a triagem.

### C2 concluído — lacunas de execução e GUI

Objetivo: corrigir no máximo uma lacuna comprovada por vez, preservando a
matriz ON/OFF e a execução controlada.

Tarefas:

- [x] Reproduzir `Notepad++` e `PuTTY` com Xvfb e traces reduzidos, isolando
  o primeiro resultado relevante: PuTTY cria `PuTTYTimerWindow` e aguarda;
  Notepad++ alcança `startup-info` wide e termina com sinal fatal controlado
  durante a corrupção de heap (SIGSEGV ou SIGABRT, exit `71`).
- [x] Reproduzir `Rockstar` com stdout/stderr e imports já resolvidos, sem
  transformar `ExitProcess 3` em sucesso artificial.
- [x] Manter `Rufus` bloqueado por W^X até existir um modelo seguro para a
  imagem empacotada; não permitir página `RWX` como atalho.
- [x] Avaliar a necessidade de correção com fixture unitária antes de alterar o
  runtime; não surgiu ainda uma correção segura autorizada, portanto B2 não foi
  alterada artificialmente.

Aceitação:

- [x] Como não houve correção nesta etapa, não houve mudança de loader ou API;
  a exigência de Debug/Sanitize fica pendente para a etapa que isolará uma
  correção concreta.
- [x] Falhas restantes continuam com exit code e diagnóstico controlados.

Evidência C2 de 2026-09-07:

- [x] `xdpyinfo` confirmou um Xvfb próprio em `:99` antes das execuções; os
  resultados foram salvos em `/tmp/tl-matrix-c2-x11`.
- [x] `7zFM_x64.exe`, `putty_x64.exe` e `notepad++.exe` foram executados em
  Rust ON e C++ OFF com `--trace --timeout 3 --cpu 3 --memory 512` e timeout
  externo de 15 segundos. stdout e exit code coincidiram: `72`, `72` e `71`,
  respectivamente.
- [x] PuTTY não registrou `x11/connect-failed`: registrou
  `RegisterClassExA`/`CreateWindowExA` de `PuTTYTimerWindow` e permaneceu até
  `guest-timeout`. Notepad++ reproduziu `guest-signal` após `startup-info`
  wide; a falha de heap foi observada como SIGSEGV ou SIGABRT, sempre exit
  `71`.
- [x] Uma captura GDB em `/tmp/tl-c2-gdb-notepad.log` observou a corrupção de
  heap na alocação seguinte dentro de `tl_SHGetFolderPathW` (`src/runtime/shell32.cpp:226`),
  sem demonstrar ainda a escrita causadora; a fixture `tl_shell` continua
  passando, então não é seguro generalizar a correção para todos os chamadores.
- [x] Não há correção autorizada para o bloqueio do Notepad++, nem um
  cenário automatizado de interação para promover PuTTY ou 7-Zip GUI.

Conclusão C2: os bloqueios restantes são controlados e reproduzíveis, mas não
há alteração de runtime justificada sem uma fixture mínima para a corrupção de
heap. C3 pode tratar os instaladores separadamente sem reclassificar esses
casos como suporte.

### C3 concluído — diagnóstico dos instaladores aprovados

Objetivo: entender por que Roblox, G HUB e Affinity não concluem instalação,
sem executar instaladores x86 nem substituir metadados Rust por C++.

Tarefas:

- [x] Separar falha do setup convidado, extração ZIP/MSIX, catálogo,
  executável principal e validação PE interno: Roblox/G HUB terminam no setup;
  Affinity falha no parsing do pacote antes da extração.
- [x] Capturar trace de instalação quando o CLI permitir e preservar os
  resultados ON/OFF sem cadastrar entradas parciais.
- [x] Avaliar Affinity somente como pacote MSIX/.NET fora do escopo; não
  aumentar limites de segurança sem evidência de necessidade e autorização.
- [x] Repetir instalação somente em prefixos temporários e remover artefatos
  após cada caso.

Aceitação:

- [x] Cada instalador tem uma causa de término ou uma limitação ambiental
  reproduzível.
- [x] Nenhuma instalação falha silenciosamente nem deixa catálogo/prefixo
  residual.

Evidência C3 de 2026-09-07:

- [x] Rust ON (`build/debug-rust`) e C++ OFF (`build/debug`) foram executados
  com `install --trace --timeout 4 --cpu 3 --memory 512` em prefixos isolados;
  resultados completos estão em `/tmp/tl-matrix-c3`.
- [x] `RobloxPlayerInstaller.exe` terminou com `ExitProcess(3)` e os dois
  builds retornaram `3`, com `failed stage="setup"`; não houve extração,
  validação de PE interno ou cadastro.
- [x] `Logitech_GHUB_x64.exe` e `lghub_installer.exe` terminaram com
  `ExitProcess(1)` e os dois builds retornaram `1`, com
  `failed stage="setup"`; o alias teve o mesmo comportamento e nenhum
  catálogo foi alterado.
- [x] `Affinity x64.msix` retornou `4` nos dois builds antes da extração.
  Rust emitiu `package-parse status="malformed" code="19" phase="3"`
  com o limite agregado excedido; C++ registrou a mesma etapa sem fallback.
  O conteúdo interno .NET permanece fora do escopo.
- [x] stdout foi vazio e idêntico nos oito casos; nenhum trace registrou
  `extracted`, `registered` ou cadastro parcial, e os oito prefixos
  temporários ficaram sem arquivos.

Conclusão C3: os instaladores PE32+ não falharam em parsing, extração ou
validação do PE interno; os setups convidados encerraram explicitamente antes
de materializar uma instalação. O pacote Affinity continua rejeitado com
segurança por limite de análise, sem ampliar limites nem prometer suporte
.NET/MSIX.

### C4 concluído — regressão da matriz completa

Objetivo: repetir B1–B4 depois de cada correção da rodada C e manter o corpus
como gate de compatibilidade.

Tarefas:

- [x] Reexecutar análise Rust ON/C++ OFF nos 27 arquivos.
- [x] Reexecutar execução nativa e instalação seletiva com os mesmos limites.
- [x] Comparar stdout, stderr normal, exit codes, trace, limpeza e backend.
- [x] Atualizar `docs/compatibilidade.md` e este roadmap somente com
  evidência reproduzível.

Aceitação:

- [x] Nenhuma correção altera silenciosamente o comportamento dos caminhos
  não promovidos ou do build Rust OFF.
- [x] Os resultados podem ser repetidos em um ambiente limpo e cada commit
  identifica exatamente o bloco validado.

Evidência C4 de 2026-09-07:

- [x] O `--report` foi repetido nos 27 arquivos em Rust ON e C++ OFF. As
  distribuições coincidiram: `13` sucessos, `12` rejeições PE32/x86 com exit
  `5` e `3` rejeições estruturais com exit `4`; stdout foi byte-a-byte igual.
  Diferenças de stderr ficaram limitadas aos campos estruturados do backend
  Rust e a textos humanos equivalentes (HWiNFO/Affinity).
- [x] A execução foi repetida em Xvfb válido para os oito PE32+ não-DLL
  selecionados. Rust/C++ coincidiram em stdout e exit: 7-Zip GUI `72`, 7-Zip
  CLI `0`, Rockstar `3`, Rufus `4`, WinRAR `0`, Notepad++ `71`, PuTTY `72` e
  WinRAR SFX `0`. As GUIs que chegaram ao X11 registraram conexão e janela.
- [x] A instalação seletiva ON/OFF foi repetida em C3 com prefixos temporários:
  Roblox `3`, G HUB/alias `1` e Affinity `4`, sem eventos de extração/registro
  e sem arquivos nos prefixos.
- [x] Os resultados estão em `/tmp/tl-matrix-c4/report`,
  `/tmp/tl-matrix-c4/run` e `/tmp/tl-matrix-c3`; `git diff --check` passa e
  nenhum artefato desses diretórios foi adicionado ao repositório.

Conclusão C4: a matriz do corpus está reproduzível e ON/OFF permanece
equivalente nos caminhos testados. Nenhum aplicativo adicional é promovido a
suportado; as limitações de PE32/x86, empacotamento, interação GUI, heap do
Notepad++, término dos setups e MSIX/.NET continuam explícitas.

## Rodada D — aprofundamento controlado dos bloqueios x64

Esta rodada reinicia o ciclo depois da matriz completa. Cada marco deve começar
com uma hipótese específica, uma fixture ou reprodução mínima e uma comparação
Rust ON/C++ OFF. Nenhum resultado negativo será convertido em suporte apenas
porque o parsing ou a resolução de imports passou.

### D1 — Isolamento da corrupção de heap do Notepad++

Objetivo: descobrir se a corrupção observada depois de `GetStartupInfoW` e no
primeiro `SHGetFolderPathW(CSIDL_APPDATA)` vinha de uma API do runtime, de uma
convenção de memória Win32 ainda incompleta ou do próprio aplicativo.

Tarefas:

- [x] Criar a fixture PE32+ mínima `tl_shell_heap_probe`, sem CRT, cobrindo
  startup wide, mutação/cópia do bloco de ambiente, liberação do bloco
  original, `SHGetFolderPathW`, `HeapSize`, `HeapReAlloc` de 512 para 1024
  bytes e alocações posteriores, sem copiar código do Notepad++.
- [x] Criar a fixture agregada `tl_notepad_startup_probe`, sem CRT, com a
  sequência observada de startup, handles, locale, SList, FLS, ambiente,
  realloc e `SHGetFolderPathW`, mantendo o probe independente do aplicativo.
- [x] Executar a fixture em Debug Rust ON e C++ OFF; os testes de metadados,
  report, runtime e `app run` passaram em ambos, com exit `0` e stdout igual.
- [x] Executar a fixture no preset Sanitizer compatível com o parser
  atual, com sentinelas e backtrace; separar corrupção do host de falha guest.
- [x] Comparar a fixture com `tl_shell` e com o Notepad++ sob Xvfb válido;
  registrar a primeira operação divergente antes de mudar o runtime.
- [x] A causa foi isolada no runtime: as APIs `lstrcpyW`, `lstrcpynW`,
  `lstrcmpW` e `lstrcmpiW` usavam `wchar_t` host de 4 bytes para buffers
  guest UTF-16 de 2 bytes. A correção usa `std::uint16_t`, tem teste unitário,
  fixture guest e matriz ON/OFF repetida.

Aceitação:

- [x] Há uma fixture reproduzível, um diagnóstico ASan da primeira escrita e
  uma correção mínima da convenção de memória guest/host.
- [x] Nenhuma correção relaxa isolamento, W^X, validação de memória ou limites.
- [x] O Notepad++ foi reclassificado somente quanto ao bloqueio de heap: a
  execução agora termina em timeout controlado, com stdout/exit/trace
  comparados nos dois backends; ele continua sem suporte funcional GUI.

Evidência inicial D1 de 2026-09-07:

- [x] `tests/samples/src/tl_shell_heap_probe.c` reproduz a sequência mínima;
  `fixture_tl_shell_heap_probe_metadata`,
  `runtime_tl_shell_heap_probe_matches_readobj`,
  `app_run_tl_shell_heap_probe` e `report_tl_shell_heap_probe_support`
  passaram nos builds `build/debug` e `build/debug-rust`.
- [x] A execução direta com `--trace` está em `/tmp/tl-d1-shell-heap`:
  Rust ON e C++ OFF retornaram `0`, stdout byte-a-byte igual e registraram
  startup wide, ambiente, startup wide e `ExitProcess(0)` sem corrupção.
- [x] A fixture existente `tl_shell` continua passando; o probe com cópia do
  ambiente também passa. Antes da correção, o Notepad++ reproduzia corrupção
  de heap após `startup-info` wide; isso foi reduzido à ABI incorreta das APIs
  `lstr*W`, e não a uma necessidade de relaxar o heap ou o isolamento.
- [x] A fixture foi executada diretamente com `build/sanitize/src/tradutorlinux`
  e `ASAN_OPTIONS=detect_leaks=0`, retornando `0` e sem relatório de memória;
  o ASan emitiu somente o aviso conhecido sobre `__asan_handle_no_return` na
  troca de stack do convidado.
- [x] O probe agregado passou em Debug C++ OFF e Rust ON nos testes de
  metadados, report, runtime e `app run`; a execução direta nos dois builds
  retornou `0`, stdout byte-a-byte igual (`notepad-startup-probe`) e traces
  semanticamente iguais para startup, locale, FLS, ambiente e `ExitProcess`.
- [x] O probe agregado também retornou `0` no binário Sanitizer existente, sem
  erro ASan/LSan além do aviso conhecido de troca de stack. Depois da correção,
  a fixture de regressão exercita explicitamente as quatro APIs wide.
- [x] O Sanitizer reconstruído executou o Notepad++ sob Xvfb válido, chegou à
  janela `Configurator` e terminou em `124` pelo timeout externo, sem
  `AddressSanitizer`, `LeakSanitizer`, `heap-buffer-overflow`, `SIGABRT` ou
  `SIGSEGV` em `/tmp/tl-d1-notepad-sanitize-xvfb-fixed.err`. A descoberta
  automática do CTest continua limitada pelo LSan sob ptrace, então a
  evidência aceita é a execução direta do alvo Sanitizer.
- [x] Um GDB seguindo o processo convidado registrou duas sequências de
  `HeapSize`/`HeapReAlloc` nos PCs convidados `0x14043aaa1`/`0x14043a989`,
  depois um segundo `GetStartupInfoW` e a chamada
  `SHGetFolderPathW(CSIDL_APPDATA)`. A execução instrumentada expirou sem
  reproduzir a corrupção; a escrita causadora foi identificada posteriormente
  pelo ASan em `tl_lstrcpyW`, com `WRITE of size 4` em região guest de 2 bytes.

Evidência final D1 de 2026-09-07:

- [x] O teste unitário `Win32LocaleTest.WideStringApisUseGuestUtf16Units` e a
  fixture `tl_notepad_startup_probe` cobrem cópia, cópia limitada, comparação
  sensível e comparação sem distinção de maiúsculas das quatro APIs wide.
- [x] Debug C++ OFF e Rust ON retornaram `124` sob o mesmo Xvfb, com stdout
  byte-a-byte igual, trace semântico igual, conexão X11, criação/mapeamento da
  janela e nenhuma ocorrência de corrupção, ASan ou sinal fatal.

### D2 — Cenários interativos para GUIs x64

Objetivo: transformar os timeouts controlados de 7-Zip File Manager e PuTTY em
cenários automatizados de interação e encerramento, sem declarar suporte GUI
amplo.

Tarefas:

- [x] Reutilizar o smoke Xvfb do 7-Zip para validar abertura, ação mínima e
  encerramento em Rust ON/OFF; a ação `Copy` foi confirmada nos dois builds
  com exit `0`, arquivo copiado e trace de operação bem-sucedida.
- [x] Criar um cenário PuTTY que abra a janela configurável, envie somente
  eventos seguros e encerre por comando/fechamento controlado.
- [x] Comparar janelas, eventos X11, stdout, exit code, limpeza e trace; manter
  timeout como resultado quando a interação não for determinística.

Aceitação:

- [x] Cada cenário possui ação e critério de encerramento reproduzíveis.
- [x] Nenhuma GUI foi promovida além da operação `Copy` já exercitada no
  7-Zip; o cenário PuTTY valida apenas a abertura/fechamento da configuração,
  não o fluxo SSH completo.

Evidência D2 de 2026-09-07:

- [x] `tests/gui/seven_zip_smoke.cpp` passou em `build/debug` e
  `build/debug-rust`, com Xvfb escolhendo displays livres por `-displayfd`.
  O harness não fixa `:99`, evitando locks residuais do ambiente.
- [x] `tests/apps/putty/putty_smoke.cpp` inicia PuTTY sob Xvfb próprio, encontra
  `PuTTY Configuration`, envia somente `WM_DELETE_WINDOW` pela conexão X11 e
  exige saída limpa (`exit 0`) em `build/debug` e `build/debug-rust`.
- [x] O trace do cenário confirma `CreateDialogParamA`, as captions `About
  PuTTY`/`PuTTY Configuration` e a criação dos controles dinâmicos; o harness
  não depende da janela temporária de sincronização.
- [x] A correção ficou limitada ao parser de templates de diálogo, suporte ao
  diálogo modeless e invalidação segura do cache de alocações guest; não altera
  loader, rede, prefixo ou a classificação geral de compatibilidade.

### D3 — Diagnóstico dos instaladores e pacote x64

Objetivo: avançar a identificação de Roblox, G HUB e Affinity sem executar
instaladores x86, ampliar limites MSIX ou prometer .NET/Mono.

Tarefas:

- [x] Separar, com fixtures e traces, setup convidado, materialização,
  catálogo, executável principal e falha de validação PE interno.
- [x] Verificar se Roblox/G HUB têm uma etapa controlada que possa ser testada
  sem cadastrar ou deixar arquivos persistentes.
- [x] Registrar Affinity como limite de pacote/.NET e testar somente rejeições,
  limpeza e diagnósticos estruturados.

Aceitação:

- [x] Nenhum instalador deixa prefixo ou catálogo parcial.
- [x] Qualquer mudança passa ON/OFF e preserva os códigos de erro existentes.

Evidência D3 de 2026-09-07:

- [x] `RobloxPlayerInstaller.exe` foi executado com `install --trace
  --timeout 4 --cpu 3 --memory 512` nos builds `build/debug-rust` e
  `build/debug`. Ambos retornaram `3`, registraram `RBXCRASH`/`ExitProcess(3)`
  e `failed stage="setup"`; stdout foi igual e os diretórios temporários não
  receberam arquivos.
- [x] `Logitech_GHUB_x64.exe` foi executado nos dois builds com os mesmos
  limites. Ambos retornaram `1`, registraram `ExitProcess(1)` e
  `failed stage="setup"`; não houve extração, registro ou arquivos persistentes.
- [x] `Affinity x64.msix` foi rejeitado com exit `4` antes da extração nos dois
  builds. Rust registrou `package-parse format="MSIX / AppX"
  status="malformed" code="19" phase="3"`, enquanto o build C++ OFF manteve
  `failed stage="package-parse"`; stdout foi igual e ambos deixaram zero
  arquivos no prefixo e no APPDATA isolados.
- [x] As evidências completas ficaram em `/tmp/tl-d3-*`; nenhum caso exibiu
  `extracted` ou `registered`. A etapa não alterou o runtime, não ampliou o
  limite MSIX e não criou suporte para .NET/Mono.

### D4 — Regressão e fechamento da rodada D

Objetivo: repetir a matriz completa depois de cada alteração de D1–D3.

Tarefas:

- [x] Reexecutar `--report` nos 27 arquivos em Rust ON/C++ OFF.
- [x] Reexecutar a matriz nativa sob Xvfb válido e a instalação seletiva em
  prefixos temporários.
- [x] Comparar stdout, stderr, exit codes, traces, limpeza e símbolos OFF;
  atualizar a matriz sem promover limitações não resolvidas.

Aceitação:

- [x] Todos os marcos alterados possuem commit separado e evidência em `/tmp`.
- [x] `git diff --check` passa e o worktree fica limpo antes da próxima rodada.

Evidência D4 de 2026-09-07:

- [x] O `--report` foi repetido nos 27 arquivos listados no corpus, usando
  `build/debug-rust` e `build/debug`. Os dois backends produziram 13 exits `0`,
  12 exits `5` e 2 exits `4`; exit code e stdout coincidiram em 27/27. Nenhum
  caso de report registrou eventos de loader, runtime ou processo.
- [x] A matriz nativa foi repetida para `7z_x64.exe`, os dois WinRAR, Rockstar,
  Rufus, Notepad++, PuTTY e 7zFM, sob Xvfb próprio, `--timeout 3`, `--cpu 3`
  e `--memory 512`. Exit code e stdout coincidiram em 8/8; os campos
  semânticos dos traces coincidiram em 8/8 e todos os APPDATA isolados ficaram
  sem arquivos. `7z_x64.exe` retornou `0`, Rockstar `3`, Rufus `4` e os
  cenários sem interação retornaram `72` por timeout controlado.
- [x] A instalação seletiva D3 foi a regressão pós-D1/D2/D3 para Roblox, G HUB
  e Affinity: pares ON/OFF preservaram exits `3/1/4`, stdout vazio e zero
  arquivos, sem `extracted` ou `registered`.
- [x] O `nm` com nomes exatos não encontrou símbolos dos adaptadores Rust no
  executável nem na biblioteca estática C++ OFF. As evidências estão em
  `/tmp/tl-d4-report.qUWazS`, `/tmp/tl-d4-run-valid.7XWPLv` e `/tmp/tl-d3-*`.

### E1 — Encerramento controlado do WinRAR SFX

Objetivo: transformar a limitação observada no cenário sem interação em uma
regressão reproduzível de GUI, usando somente uma ação externa segura e sem
introduzir comportamento específico do WinRAR no runtime.

Tarefas:

- [x] Criar `tests/apps/winrar/winrar_sfx_smoke.cpp` para iniciar o SFX sob
  Xvfb próprio, localizar `WinRAR self-extracting archive` e enviar
  `WM_DELETE_WINDOW` como cancelamento externo seguro.
- [x] Registrar o smoke como alvo separado no CMake, sem ligá-lo ao runtime ou
  criar DLL específica do aplicativo.
- [x] Executar o smoke nos builds Rust ON e C++ OFF e comparar saída, exit code,
  trace, limpeza e ausência de timeout.
- [x] Repetir a regressão D4 relevante depois do novo cenário.

Aceitação:

- [x] O WinRAR SFX cria e mapeia a janela, encerra com exit `0` após o
  cancelamento controlado e não registra `guest-timeout`.
- [x] Rust ON e C++ OFF produzem o mesmo comportamento no smoke.
- [x] A matriz de compatibilidade distingue o cenário SFX controlado da
  execução sem interação e não declara suporte geral ao WinRAR.

Evidência E1 de 2026-09-07:

- [x] `build/debug-rust/tests/winrar_sfx_smoke` e
  `build/debug/tests/winrar_sfx_smoke`, usando `WinRAR_x64.exe`, passaram sob
  Xvfb próprio. Ambos localizaram e mapearam `WinRAR self-extracting archive`,
  enviaram `WM_DELETE_WINDOW`, observaram `EndDialog(result=2)` e terminaram
  com `ExitProcess(0)` sem `guest-timeout`.
- [x] A regressão pós-E5/E6/E7, repetida em 2026-09-07, confirmou o mesmo
  `ExitProcess(0)` nos dois builds. O critério anterior `exit 3` era uma
  expectativa obsoleta do harness; o cancelamento agora é validado pelo
  `EndDialog(result=2)`, pela saída limpa e pela ausência de timeout, sem
  alterar o runtime para forçar um código de retorno.
- [x] O `--report` de `WinRAR_x64.exe` continuou equivalente nos builds ON/OFF;
  o cenário sem interação permanece `guest-timeout 72` e não foi promovido.
- [x] Uma tentativa separada de `Return` confirmou `IsDialogMessageW`/`IDOK`
  e o início de `environment expand`/`filesystem enumerate`, mas não concluiu
  a extração dentro dos limites externos; esse fluxo continua explicitamente
  não suportado e não altera o runtime.

### E2 — Bloqueio controlado de exceções C++ no Notepad++

Objetivo: transformar o bloqueio observado depois da correção da ABI UTF-16
em uma regressão negativa reproduzível, sem declarar suporte geral ao
Notepad++ e sem criar código específico no runtime.

Tarefas:

- [x] Criar `tests/apps/notepadpp/notepadpp_smoke.cpp` para iniciar o programa
  sob Xvfb próprio, fechar `Configurator` e o diálogo de `stylers.xml`, e
  verificar o bloqueio de exceção C++ como falha controlada.
- [x] Registrar o smoke como alvo separado no CMake, sem DLL, shim ou regra
  específica do aplicativo.
- [x] Executar o smoke nos builds Rust ON e C++ OFF e comparar saída, exit code,
  trace, limpeza e ausência de timeout.
- [x] Repetir a regressão relevante de `--report` e da matriz nativa.

Aceitação:

- [x] Notepad++ cria e mapeia `Configurator`, reproduz o diálogo de
  `stylers.xml` e termina com `guest-signal`/exit `71`, sem `guest-timeout`.
- [x] Rust ON e C++ OFF produzem o mesmo comportamento.
- [x] A matriz mantém o Notepad++ fora da declaração de suporte geral enquanto
  o fluxo principal e a interação de edição não forem validados.

Evidência E2 de 2026-09-07:

- [x] Os smokes ON/OFF reproduziram `Configurator`, `Load stylers.xml failed`,
  cinco eventos `cxx-throw ignored` do worker e `guest-signal`, com exit `71`;
  não houve `guest-timeout` nem alteração no runtime comum.
- [x] O `--report` continuou equivalente nos dois builds: 584/584 imports e
  stdout/exit idênticos. A execução sem interação continua no timeout já
  registrado; o smoke E2 apenas torna o bloqueio pós-interação determinístico.

### E3 — Backing store HGLOBAL genérico para streams OLE

Objetivo: corrigir a rejeição genérica de `CreateStreamOnHGlobal` quando o
convidado fornece um bloco válido de `GlobalAlloc`, sem adicionar qualquer
tratamento específico para WinRAR ou outro aplicativo.

Tarefas:

- [x] Aceitar somente blocos registrados por `GlobalAlloc` e manter a rejeição
  de HGLOBALs externos/desconhecidos.
- [x] Preservar a política `delete-on-release`: o bloco permanece vivo quando
  zero e é liberado pelo `Release` final quando diferente de zero.
- [x] Manter a capacidade inicial de blocos `GlobalAlloc`; expansões além dela
  falham de forma controlada, enquanto streams anônimos mantêm o crescimento
  existente.
- [x] Adicionar testes unitários ON/OFF para leitura do backing store,
  propriedade do bloco e rejeição de handles desconhecidos.
- [x] Repetir o smoke SFX do WinRAR após a alteração, mantendo o cenário de
  cancelamento como a única evidência de execução GUI aprovada.

Aceitação:

- [x] Os testes `OleStreamTest.*` passaram em Rust ON e C++ OFF, quatro casos
  em cada build.
- [x] O trace do WinRAR SFX passou de `ole-stream ... invalid` para criação e
  liberação bem-sucedidas sob Rust ON; a tentativa de `IDOK`/extração ainda
  não concluiu dentro dos limites e não foi promovida.
- [x] O smoke original de cancelamento continua sendo a regressão de execução;
  nenhum DLL, shim, regra ou caminho específico de WinRAR foi adicionado ao
  runtime.

### E4 — Rejeição segura de consultas de certificado

Objetivo: remover um stub permissivo de `CryptQueryObject` que fabricava
handles de loja/mensagem e um contexto nulo, mantendo a fronteira de
Authenticode/CMS explicitamente fora do escopo.

Tarefas:

- [x] Validar ponteiros de saída antes de escrever qualquer resultado.
- [x] Zerar saídas válidas e retornar `ERROR_NOT_SUPPORTED` para formatos ainda
  não implementados; nunca devolver tokens que o runtime não rastreia.
- [x] Adicionar regressão unitária para rejeição, erro estruturado e ausência
  de handles fictícios.
- [x] Repetir o smoke do Notepad++ e confirmar que o bloqueio controlado não
  regrediu.

Aceitação:

- [x] O teste `Crypt32Test.CryptQueryObjectRejectsUnsupportedInputWithoutFabricatedHandles`
  passa em Rust ON e C++ OFF.
- [x] Notepad++ continua com a mesma falha controlada pós-interação; nenhum
  caminho de certificado passa a acessar ponteiros inválidos.
- [x] Authenticode, CMS, loja do sistema e suporte específico de aplicativo
  continuam fora do contrato.

### E5 — Posição de arquivo consistente após I/O

Objetivo: corrigir uma divergência genérica entre o deslocamento mantido pelo
runtime e o deslocamento real do descritor Linux, observada durante a
investigação do fluxo de arquivos do WinRAR, sem introduzir qualquer regra
específica de aplicativo.

Tarefas:

- [x] Sincronizar `FileSlot::position` depois de leituras e escritas bem-
  sucedidas, mantendo handles especiais e descritores host sem estado global.
- [x] Adicionar regressões para `ReadFile`/`WriteFile` seguidos de
  `SetFilePointer(FILE_CURRENT)` e `SetFilePointerEx(FILE_CURRENT)`.
- [x] Repetir os testes relevantes nos builds Rust ON e C++ OFF.
- [x] Repetir a sondagem do WinRAR SFX sem transformar a exploração em suporte
  declarado nem adicionar DLL, shim ou regra específica.

Aceitação:

- [x] As posições observadas após I/O coincidem com o deslocamento host nos
  dois builds, e os testes existentes de seek e metadados continuam passando.
- [x] A sondagem do WinRAR deixa de repetir leituras no mesmo deslocamento e
  avança para chamadas posteriores; o bloqueio seguinte permanece controlado
  e não é tratado nesta etapa.
- [x] O runtime continua genérico e a matriz não promove o WinRAR para uso
  diário ou extração interativa.

Evidência E5 de 2026-09-07:

- [x] O filtro unitário com cinco casos passou sequencialmente em
  `build/debug-rust` e `build/debug`: posição após leitura, posição após
  escrita, seeks existentes e metadados wide.
- [x] A sondagem de `IDOK` do WinRAR avançou de leituras repetidas para
  enumeração, criação de `RarHtmlClassName` e chamadas posteriores. O cenário
  ainda terminou em falha controlada após `set-attributes` não suportado e
  exceção C++ ignorada no worker (`guest-signal`/`SIGTRAP`); não houve
  alteração específica do aplicativo.

### E6 — Atributos consultivos de arquivo

Objetivo: aceitar o subconjunto de atributos que aplicações Win32 usam como
metadados consultivos, sem fingir uma representação Linux inexistente e sem
afrouxar a validação dos atributos que alteram permissões ou tipo.

Tarefas:

- [x] Tratar `SetFileAttributesW(..., 0)` como o estado normal do arquivo.
- [x] Aceitar `FILE_ATTRIBUTE_NOT_CONTENT_INDEXED` como atributo consultivo,
  removendo-o somente antes da aplicação dos bits POSIX suportados.
- [x] Manter bits desconhecidos, `HIDDEN`, `SYSTEM` e combinações inválidas
  como falha controlada.
- [x] Adicionar regressões unitárias e repetir os builds ON/OFF.

Aceitação:

- [x] O contrato de `SetFileAttributesW` cobre as chamadas genéricas `0` e
  `ARCHIVE | NOT_CONTENT_INDEXED` sem alterar a política dos demais bits.
- [x] O comportamento foi validado sem código, DLL ou shim específico de
  aplicativo; a matriz do WinRAR continua sem promoção de suporte.

Evidência E6 de 2026-09-07:

- [x] O teste `Win32FileMetadataTest.SetFileAttributesWControlsReadonlyAndValidatesBits`
  passou nos builds Rust ON e C++ OFF após incluir os casos `0` e
  `ARCHIVE | NOT_CONTENT_INDEXED`.
- [x] A política está documentada em `docs/compatibilidade.md`; o bit
  consultivo não é falsamente exposto por `GetFileAttributesW`.

### E7 — Falha controlada na criação de threads host

Objetivo: impedir que uma falha de recurso do host durante `CreateThread`
escape por uma função `noexcept` e aborte o processo convidado, mantendo a
criação de threads como comportamento genérico compartilhado.

Tarefas:

- [x] Capturar falhas de alocação e de construção de `std::thread` dentro de
  `tl_CreateThread`.
- [x] Liberar TEB, stack, referências FLS e slot reservado quando a thread não
  puder ser iniciada; não publicar `thread_id` nem handle parcial.
- [x] Retornar `ERROR_NOT_ENOUGH_MEMORY` e emitir diagnóstico `api-failure`
  para a falha de recurso do host.
- [x] Adicionar regressão de argumentos inválidos e repetir a sondagem real que
  reproduzia `std::system_error`/`SIGABRT`.

Aceitação:

- [x] A fixture unitária continua sem handle parcial para uma criação inválida.
- [x] O cenário WinRAR deixa de terminar em `std::system_error`/`SIGABRT` e
  passa a terminar no bloqueio convidado posterior, com `guest-signal`/
  `SIGTRAP` controlado.
- [x] Nenhum tratamento específico, DLL ou shim do WinRAR foi adicionado.

Evidência E7 de 2026-09-07:

- [x] A compilação do runtime Rust e o teste de concorrência passaram após a
  captura da exceção.
- [x] A sondagem `IDOK` sob Xvfb não contém mais `terminate called`,
  `std::system_error` ou `SIGABRT`; o trace termina no `cxx-throw`/`SIGTRAP`
  posterior do convidado.

### E8 — DLLs lado a lado em execuções nativas

Objetivo: corrigir a descoberta genérica de DLLs PE32+ AMD64 colocadas no
diretório do executável, inclusive quando o aplicativo é executado diretamente
fora de `drive_c`. Essa é a forma usada por aplicativos portáteis e pelo
`7z_x64.exe`/`7z.dll`; não há regra específica do 7-Zip no runtime.

Tarefas:

- [x] Procurar primeiro o diretório do executável solicitante antes das pastas
  do prefixo e dos módulos internos.
- [x] Manter validação de arquivo regular sem symlink, parsing PE32+ AMD64,
  mapeamento, relocations, imports, attach/detach, refcount e W^X.
- [x] Adicionar regressão unitária do `GuestModuleGraph` para um executável
  externo e uma DLL irmã.
- [x] Repetir a fixture `tl_dynload` e a matriz relevante em Rust ON e C++ OFF.
- [x] Executar o 7-Zip real com `7z.dll` ao lado, listar um ZIP `stored` por
  `Z:\...` e comparar stdout, exit code e trace de carregamento nos dois builds.

Aceitação:

- [x] DLLs PE ao lado do executável são encontradas sem entrar em
  `compat/apps/` e sem alterar módulos compartilhados por aplicativo.
- [x] A fixture unitária passa em Rust ON e C++ OFF; `tl_dynload` continua
  passando nos dois builds.
- [x] O 7-Zip real mapeia/anexa/descarrega `7z.dll`, lista o ZIP e termina com
  exit `0`; stdout Rust/C++ é byte a byte igual.
- [x] Operações não exercitadas além da listagem e o suporte a DLL como
  aplicativo independente permanecem fora da declaração de compatibilidade.

Evidência E8 de 2026-09-07:

- [x] `ModuleGraphTest.LoadsDriveDllFromExternalApplicationDirectory` passou
  em `build/debug-rust` e `build/debug`.
- [x] `runtime_tl_dynload_matches_readobj` e `app_run_tl_dynload` passaram nos
  dois builds.
- [x] Com `7z_x64.exe` e `7z.dll` no corpus original, um ZIP temporário foi
  listado por `Z:\tmp\...\payload.zip`: ambos os builds retornaram `0`,
  produziram SHA-256 de stdout
  `52efd5e3a6b52adc449a1f7702b9cafb4bf53784b19104aa0fd250bc433c0f19` e
  registraram onze descobertas de `7z.dll`.

### E9 — Smoke automatizado de operações do 7-Zip CLI

Objetivo: proteger as operações de arquivo que já foram observadas no 7-Zip
real sem colocar o aplicativo, a DLL ou regras de seleção dentro do runtime.

Tarefas:

- [x] Criar `tests/apps/7zip/7z_cli_smoke.cpp` como alvo separado, sem DLL
  específica e sem dependência do corpus no build padrão.
- [x] Isolar cada execução em staging e prefixo temporários, copiar o
  `7z_x64.exe` e `7z.dll` irmãos e aplicar timeout externo de 15 segundos.
- [x] Exercitar criação, listagem e extração de ZIP `stored`, verificando
  `Everything is Ok`, ciclo `dll-mapped`/`dll-attach`/`dll-unload` e bytes do
  arquivo extraído.
- [x] Registrar o smoke no CTest quando `TL_POPULAR_APPS_DIR` for fornecido,
  mantendo o build padrão independente dos downloads externos.
- [x] Executar a matriz em Rust ON e C++ OFF.

Aceitação:

- [x] `seven_zip_cli_smoke` passa em Rust ON e C++ OFF com exit `0`.
- [x] O smoke não modifica o corpus original, não cria DLLs específicas e
  remove o staging ao terminar.
- [x] A tabela de compatibilidade declara somente o subconjunto de criação,
  listagem e extração coberto; compressão, formatos adicionais, senha,
  volumes e GUI continuam não validados.

Evidência E9 de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust -R '^seven_zip_cli_smoke$'` passou.
- [x] `ctest --test-dir build/debug -R '^seven_zip_cli_smoke$'` passou.
- [x] O resultado do smoke foi `7-Zip CLI create/list/extract: ok` nos dois
  builds, com `7z.dll` carregada do diretório do executável.

### E10 — Round-trip comprimido do 7-Zip CLI

Objetivo: ampliar a evidência do fluxo genérico de arquivos sem confundir o
ZIP `stored` com o caminho de compressão usado pelo 7-Zip em arquivos reais.

Tarefas:

- [x] Manter um caso `stored` explícito e adicionar criação de ZIP com
  `DEFLATE`, listagem técnica que confirma `Method = Deflate` e extração dos
  dois arquivos em diretórios separados.
- [x] Reutilizar o mesmo staging, prefixo temporário, timeout externo e
  verificação de ciclo da `7z.dll`, sem alterar o corpus original.
- [x] Executar a matriz Rust ON/C++ OFF e exigir bytes idênticos após as duas
  extrações.

Aceitação:

- [x] O smoke confirma seis comandos reais do `7z_x64.exe`: criação, listagem
  e extração para `stored` e `DEFLATE`, todos com exit `0`.
- [x] A listagem técnica confirma `Method = Deflate`, e os dois arquivos
  extraídos coincidem byte a byte com a entrada repetitiva.
- [x] Nenhuma DLL, shim, regra de seleção ou mudança específica do aplicativo
  foi adicionada ao runtime.

Evidência E10 de 2026-09-07:

- [x] O executável `build/debug-rust/tests/seven_zip_cli_smoke` passou com
  `7-Zip CLI stored/deflate create/list/extract: ok` e carregou `7z.dll` em
  cada operação.
- [x] `ctest --test-dir build/debug-rust -R '^seven_zip_cli_smoke$'` e o mesmo
  filtro em `build/debug` passaram separadamente; o staging único evita colisão
  quando os processos de teste recebem o mesmo PID lógico no ambiente.
- [x] A expansão do payload tornou o método `Deflate` observável; a execução
  anterior que produzia `Method = Store` para um arquivo pequeno foi corrigida
  no teste, não mascarada no critério.

### E12 — Round-trip do formato 7z com LZMA2

Objetivo: validar o formato nativo mais comum do 7-Zip, mantendo a mesma
fronteira genérica de processo, arquivo e DLL lado a lado usada pelos casos
ZIP.

Tarefas:

- [x] Adicionar criação de `payload.7z` com `LZMA2`, listagem técnica que
  confirma `Method = LZMA2` e extração em staging separado.
- [x] Preservar os casos ZIP `stored` e `DEFLATE`, com payload repetitivo,
  verificação byte a byte e ciclo `7z.dll` em cada operação.
- [x] Executar o smoke manual e o filtro CTest nos builds Rust ON e C++ OFF,
  sem alterar o corpus original.

Aceitação:

- [x] O smoke confirma nove comandos reais do `7z_x64.exe`: criação, listagem
  e extração para ZIP `stored`, ZIP `DEFLATE` e `7z/LZMA2`, todos com exit `0`.
- [x] A listagem técnica confirma `Method = LZMA2`, e os três arquivos
  extraídos coincidem byte a byte com a entrada.
- [x] Nenhuma DLL, shim, regra de seleção ou mudança específica do aplicativo
  foi adicionada ao runtime.

Evidência E12 de 2026-09-07:

- [x] `build/debug-rust/tests/seven_zip_cli_smoke` e o binário equivalente
  de `build/debug` passaram com
  `7-Zip CLI stored/deflate/7z create/list/extract: ok`.
- [x] `ctest --test-dir build/debug-rust -R '^seven_zip_cli_smoke$'` passou
  em 3,47 s, e o mesmo filtro em `build/debug` passou em 4,40 s.

### E13 — Múltiplos arquivos e caminhos Unicode no 7-Zip CLI

Objetivo: cobrir a combinação de entradas múltiplas, compressão `DEFLATE`,
diretórios aninhados e nomes Unicode sem introduzir suporte específico ao
7-Zip no runtime.

Tarefas:

- [x] Corrigir `_beginthreadex` para encaminhar a função convidada com a
  convenção Microsoft x64, a pilha convidada e o contexto de thread do
  runtime, reutilizando `tl_CreateThread` em vez de chamar o endereço PE por
  uma thread POSIX crua.
- [x] Ampliar o smoke para criar, listar e extrair `input-data/café-日本.txt`
  junto com `input.txt` nos ZIP `stored`, ZIP `DEFLATE` e `7z/LZMA2`.
- [x] Solicitar `UTF-8` explicitamente nas listagens técnicas, para que a
  verificação do nome seja independente da página OEM do terminal.
- [x] Executar a matriz Rust ON/C++ OFF e os testes de CRT/concurrency
  associados ao caminho de threads.

Aceitação:

- [x] Os três formatos criam, listam e extraem os dois arquivos, preservando
  byte a byte o conteúdo e o caminho aninhado Unicode.
- [x] O caso DEFLATE com múltiplas entradas não termina mais em
  `guest-signal`/`SIGSEGV`; ambos os builds terminam com `Everything is Ok` e
  exit `0`.
- [x] Nenhuma DLL, shim, regra de seleção ou mudança específica do aplicativo
  foi adicionada ao runtime.

Evidência E13 de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust --output-on-failure -R
  '(^seven_zip_cli_smoke$|Msvcrt|Win32Concurrency|runtime_tl_7z_cli|app_run_tl_7z_cli)'`
  passou: 55/55 testes.
- [x] O mesmo filtro em `build/debug` passou: 55/55 testes.
- [x] `seven_zip_cli_smoke` passou manualmente nos dois builds com
  `7-Zip CLI stored/deflate/7z create/list/extract: ok`.

### E14 — Ciclo de manutenção de arquivos no 7-Zip CLI

Objetivo: cobrir operações de manutenção de um arquivo existente, além do
round-trip inicial, mantendo o cenário isolado e o runtime genérico.

Tarefas:

- [x] Testar integridade do ZIP `stored` com o comando `t` e exigir
  `Everything is Ok`, exit `0` e o ciclo completo da `7z.dll`.
- [x] Remover `input.txt` com o comando `d`, atualizar o mesmo ZIP com
  `updated.txt` usando `DEFLATE` e validar o arquivo resultante com listagem
  técnica UTF-8 e extração em staging separado.
- [x] Confirmar que o caminho Unicode permanece no arquivo, que `input.txt`
  não reaparece e que `updated.txt` preserva os bytes esperados.
- [x] Reexecutar a matriz CTest Rust ON/C++ OFF, sem alterar o corpus
  original nem criar DLL, shim ou regra específica do aplicativo.

Aceitação:

- [x] O smoke cobre criação, integridade, remoção, atualização, listagem e
  extração para ZIP `stored`, ZIP `DEFLATE` e `7z/LZMA2`.
- [x] A operação de manutenção termina com exit `0` nos dois backends e não
  altera a declaração além do subconjunto testado.
- [x] A saída continua determinística, o staging é removido e os bytes do
  caminho Unicode e de `updated.txt` são verificados após a extração.

Evidência E14 de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust --output-on-failure -R
  '^seven_zip_cli_smoke$'` passou em 5,61 s.
- [x] O mesmo filtro em `build/debug` passou em 7,11 s.
- [x] Os binários manuais ON/OFF emitiram
  `7-Zip CLI stored test/delete/update/deflate/7z lifecycle: ok`.

### E15 — Caminhos com espaços no 7-Zip CLI

Objetivo: validar a passagem de caminhos com espaços pela linha de comando
Windows, sem reduzir a cobertura de Unicode, manutenção de arquivos ou
formatos já exercitados.

Tarefas:

- [x] Executar o ciclo completo do smoke em um staging cujo nome contém
  espaço, incluindo o prefixo, os arquivos, os três arquivos compactados e os
  diretórios de extração.
- [x] Confirmar que o CRT convidado preserva cada argumento `Z:\...` e a
  opção `-o...` como uma unidade, sem truncar no espaço.
- [x] Reexecutar Rust ON/C++ OFF e verificar os mesmos bytes, exit codes,
  ciclo da `7z.dll` e limpeza do staging.

Aceitação:

- [x] Criação, listagem, teste, remoção, atualização e extração de ZIP
  `stored`, ZIP `DEFLATE` e `7z/LZMA2` continuam terminando com exit `0` nos
  dois backends quando todos os caminhos contêm espaço.
- [x] O nome Unicode `input-data/café-日本.txt`, `updated.txt` e a ausência
  de `input.txt` no arquivo atualizado continuam verificados byte a byte.
- [x] Nenhuma DLL, shim, regra de seleção ou comportamento específico do
  aplicativo foi adicionado ao runtime.

Evidência reproduzível de 2026-09-07:

- [x] `cmake --build build/debug-rust --target seven_zip_cli_smoke --parallel 2`
  e o binário do smoke passaram com staging contendo espaço.
- [x] `cmake --build build/debug --target seven_zip_cli_smoke --parallel 2`
  e o binário equivalente passaram com o mesmo resultado.
- [x] Os filtros CTest ON/OFF do `seven_zip_cli_smoke` passaram; a saída foi
  `7-Zip CLI stored test/delete/update/deflate/7z lifecycle: ok` e os stagings
  foram removidos.

### E16 — Diretório de trabalho e caminhos relativos no 7-Zip CLI

Objetivo: validar que o 7-Zip CLI resolve arquivos e diretórios relativos a
partir do diretório de trabalho do processo convidado, preservando a cobertura
anterior de staging com espaços, Unicode e formatos de arquivo.

Tarefas:

- [x] Manter a criação dos arquivos compactados com caminhos absolutos em um
  staging cujo nome contém espaço, para conservar a cobertura de argumentos
  Windows com espaços.
- [x] Executar listagem, teste, remoção, atualização e extração usando nomes de
  arquivo e opções `-o` relativos ao diretório de trabalho do `7z_x64.exe`.
- [x] Confirmar que o ciclo continua usando a `7z.dll` do diretório do
  aplicativo, preserva bytes Unicode e termina com o mesmo resultado em Rust
  ON e C++ OFF.

Aceitação:

- [x] ZIP `stored`, ZIP `DEFLATE` e arquivo `7z/LZMA2` passam pelo ciclo de
  listagem/extração com caminhos relativos e exit `0` nos dois backends.
- [x] A atualização relativa mantém `updated.txt`, remove `input.txt` e
  preserva `input-data/café-日本.txt` byte a byte.
- [x] Nenhuma regra, shim ou DLL específica foi adicionada ao runtime; o
  comportamento é exercitado somente pelo smoke isolado do aplicativo.

Evidência reproduzível de 2026-09-07:

- [x] `cmake --build build/debug-rust --target seven_zip_cli_smoke --parallel 2`
  seguido de `ctest --test-dir build/debug-rust --output-on-failure -R
  '^seven_zip_cli_smoke$'` passou (1/1).
- [x] `cmake --build build/debug --target seven_zip_cli_smoke --parallel 2`
  seguido do mesmo filtro em `build/debug` passou (1/1).
- [x] Ambos emitiram `7-Zip CLI stored test/delete/update/deflate/7z
  lifecycle: ok` e removeram o staging temporário.

### E17 — I/O padrão do 7-Zip CLI

Objetivo: cobrir o uso do 7-Zip como filtro de linha de comando, alimentando
um arquivo por stdin e extraindo o conteúdo por stdout, sem depender de um
arquivo intermediário para a segunda metade do ciclo.

Tarefas:

- [x] Criar um ZIP `stored` com `7z a -si...`, encaminhando um payload
  controlado pelo smoke para o stdin do processo convidado.
- [x] Extrair o item com `7z e -so` e comparar stdout byte a byte com o
  payload original, mantendo os limites externos e a captura separada de
  stderr/trace.
- [x] Reexecutar o ciclo completo existente e o cenário de I/O padrão em
  Rust ON e C++ OFF, sem adicionar lógica específica ao runtime.

Aceitação:

- [x] A criação via stdin e a extração via stdout terminam com exit `0`,
  carregam/descartam `7z.dll` corretamente e não contaminam o payload com
  diagnóstico do runtime.
- [x] O ciclo anterior de ZIP `stored`, ZIP `DEFLATE`, `7z/LZMA2`, Unicode,
  espaços, manutenção e caminhos relativos continua passando nos dois
  backends.
- [x] A cobertura permanece em `tests/apps/7zip/`, sem DLL, shim ou regra de
  compatibilidade específica do aplicativo.

Evidência reproduzível de 2026-09-07:

- [x] `cmake --build build/debug-rust --target seven_zip_cli_smoke --parallel 2`
  seguido de `ctest --test-dir build/debug-rust --output-on-failure -R
  '^seven_zip_cli_smoke$'` passou (1/1) em 6,51 s.
- [x] `cmake --build build/debug --target seven_zip_cli_smoke --parallel 2`
  seguido do mesmo filtro em `build/debug` passou (1/1) em 7,97 s.
- [x] Ambos emitiram `7-Zip CLI stored test/delete/update/deflate/7z/stdin
  lifecycle: ok` e removeram o staging temporário.

### E18 — Renomeação de entradas no 7-Zip CLI

Objetivo: ampliar o ciclo de manutenção de arquivos existentes com a operação
`rn`, verificando que a alteração do nome permanece consistente entre a
listagem técnica e a extração.

Tarefas:

- [x] Renomear `updated.txt` para `renamed.txt` dentro do ZIP `stored` depois
  da remoção e atualização já cobertas pelo smoke.
- [x] Exigir `Everything is Ok`, exit `0`, ciclo completo da `7z.dll` e
  ausência do nome antigo na listagem e no diretório extraído.
- [x] Reexecutar todo o ciclo em Rust ON e C++ OFF, preservando os cenários de
  Unicode, espaços, caminhos relativos e stdin/stdout.

Aceitação:

- [x] `renamed.txt` aparece no `-slt`, `updated.txt` e `input.txt` não
  aparecem, e os bytes extraídos de `renamed.txt` permanecem iguais ao
  payload atualizado.
- [x] A operação não introduz DLL, shim, regra de aplicativo ou estado
  persistente no runtime.

Evidência reproduzível de 2026-09-07:

- [x] O filtro `^seven_zip_cli_smoke$` passou após compilar o alvo em
  `build/debug-rust` (1/1, 6,90 s de CTest).
- [x] O mesmo filtro passou após compilar o alvo em `build/debug` (1/1,
  8,42 s de CTest).
- [x] Ambos emitiram `7-Zip CLI stored test/delete/update/rename/deflate/7z/stdin
  lifecycle: ok` e removeram o staging temporário.

### E19 — Arquivo 7z protegido por senha

Objetivo: validar o caminho de criptografia de arquivo do 7‑Zip CLI em um
cenário isolado, sem armazenar senha ou artefato fora do staging temporário.

Tarefas:

- [x] Criar um arquivo `7z/LZMA2` com senha explícita e entradas binárias e
  Unicode já usadas pelos ciclos anteriores.
- [x] Extrair o arquivo com a senha correta usando um nome de arquivo relativo
  e verificar os dois payloads byte a byte.
- [x] Reexecutar o cenário completo em Rust ON e C++ OFF, mantendo timeout,
  limites de recurso, ciclo da `7z.dll` e limpeza do staging.

Aceitação:

- [x] Criação e extração protegidas terminam com exit `0` e `Everything is Ok`;
  o nome Unicode e os bytes do payload são preservados.
- [x] A senha existe somente como dado do smoke, não é persistida no catálogo,
  no runtime ou em uma DLL específica do aplicativo.
- [x] Os ciclos anteriores de manutenção, stdin/stdout e caminhos relativos
  continuam passando nos dois backends.

Evidência reproduzível de 2026-09-07:

- [x] O filtro `^seven_zip_cli_smoke$` passou em `build/debug-rust` após
  compilar o alvo (1/1, 7,92 s de CTest).
- [x] O mesmo filtro passou em `build/debug` após compilar o alvo (1/1,
  9,59 s de CTest).
- [x] Ambos emitiram `7-Zip CLI stored test/delete/update/rename/deflate/7z/stdin/password
  lifecycle: ok` e removeram o staging temporário.

### E20 — Rejeição controlada de senha incorreta no 7-Zip CLI

Objetivo: verificar o erro do 7-Zip ao abrir um arquivo protegido com senha
incorreta, sem aceitar dados parciais como uma extração válida e sem adicionar
tratamento específico ao runtime.

Tarefas:

- [x] Tentar extrair o arquivo `7z/LZMA2` protegido usando uma senha incorreta
  e exigir término não-zero, sem timeout.
- [x] Confirmar que a tentativa ainda carrega e descarrega `7z.dll`, mas não
  produz os payloads binário e Unicode esperados.
- [x] Reexecutar o cenário completo em Rust ON e C++ OFF, mantendo os checks
  de sucesso com a senha correta e a limpeza do staging.

Aceitação:

- [x] A senha correta continua terminando com exit `0`, `Everything is Ok` e
  os dois payloads byte a byte.
- [x] A senha incorreta termina com exit diferente de `0`, sem timeout e sem
  payload válido; não há fallback nem regra específica no runtime.
- [x] O mesmo comportamento é reproduzido nos builds Rust ON e C++ OFF.

Evidência reproduzível de 2026-09-07:

- [x] O filtro `^seven_zip_cli_smoke$` passou em `build/debug-rust` após
  compilar o alvo (1/1, 8,17 s de CTest).
- [x] O mesmo filtro passou em `build/debug` após compilar o alvo (1/1,
  10,11 s de CTest).
- [x] Ambos emitiram `7-Zip CLI stored test/delete/update/rename/deflate/7z/stdin/password/wrong-password
  lifecycle: ok` e removeram o staging temporário.

### E21 — Matriz automatizada de análise do corpus

Objetivo: transformar a identificação estrutural de cada solicitação do corpus
em um teste CTest repetível, sem confundir `--report` com execução.

Tarefas:

- [x] Registrar um verificador CMake que execute `--report` nos 26 PE
  selecionados e no `Affinity x64.msix`, cobrindo executáveis, DLLs,
  instaladores, aplicativos x64 e candidatos PE32/x86.
- [x] Fixar os exit codes esperados: `0` para análises concluídas, `4` para
  rejeições estruturais e `5` para arquitetura/formato não suportado; casos
  ausentes, timeout ou código inesperado fazem o teste falhar.
- [x] Registrar o caso somente quando `TL_POPULAR_APPS_DIR` for fornecido,
  mantendo o build padrão sem depender do corpus e sem executar entry points.

Aceitação:

- [x] CPU-Z, GPU-Z e HWMonitor são identificados automaticamente como PE32/x86
  não suportados, em vez de serem executados indiscriminadamente.
- [x] DLLs, instaladores, aplicativos PE32+ e MSIX recebem o resultado esperado
  da matriz B4, com timeout individual e diagnóstico de caso em falha.
- [x] A matriz passa nos builds Rust ON e C++ OFF sem alterar o runtime, o
  loader ou o comportamento de execução.

Evidência reproduzível de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust --output-on-failure -R
  '^popular_apps_report_matrix$'` passou com 27/27 casos em 23,73 s.
- [x] O mesmo filtro em `build/debug` passou com 27/27 casos em 20,10 s.
- [x] O teste foi registrado com os labels `runtime;apps;report;corpus` e
  permanece condicional ao diretório externo do corpus.

### E22 — Matriz automatizada de execução direta do corpus

Objetivo: repetir automaticamente os cenários nativos controlados já
observados no B2, sem iniciar instaladores, DLLs ou fluxos GUI interativos.

Tarefas:

- [x] Registrar `popular_apps_native_matrix` para 7-Zip, os dois binários
  WinRAR, Rockstar e Rufus, que possuem resultados diretos documentados.
- [x] Executar cada caso com prefixo e `APPDATA` temporários, `--timeout 3`,
  limite de três CPUs, memória de 512 MiB e timeout externo de 30 s.
- [x] Verificar o exit code e o marcador de trace esperado, incluindo
  `ExitProcess(0)`, `ExitProcess(3)` e `map-failed`, repetindo ON/OFF.

Aceitação:

- [x] 7-Zip e os dois WinRAR terminam com `0`; Rockstar termina com `3`; Rufus
  termina com `4` por `map-failed`, sem timeout ou sinal inesperado.
- [x] Instaladores, DLLs e GUIs com interação continuam fora desta matriz e
  permanecem cobertos somente por seus cenários específicos.
- [x] Nenhuma regra, shim ou DLL específica de aplicativo foi adicionada ao
  runtime; o teste apenas executa o caminho geral já existente.

Evidência reproduzível de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust --output-on-failure -R
  '^popular_apps_native_matrix$'` passou com 5/5 casos em 1,09 s.
- [x] O mesmo filtro em `build/debug` passou com 5/5 casos em 1,07 s.
- [x] O staging criado no build para prefixos temporários foi removido pelo
  próprio verificador após cada matriz.

### E23 — Matriz automatizada de instalação controlada

Objetivo: repetir os cenários de instalação real já autorizados sem executar
indiscriminadamente os instaladores do corpus e sem permitir cadastro parcial.

Tarefas:

- [x] Registrar `popular_apps_install_matrix` para Roblox, Logitech G HUB,
  seu alias byte-a-byte e o pacote Affinity já analisados.
- [x] Isolar `HOME`, `XDG_CONFIG_HOME`, `APPDATA`, prefixo e staging por caso,
  com `--cpu 3`, `--memory 512` e timeout externo de 30 s.
- [x] Verificar exit code, estágio e marcador do diagnóstico; exigir ausência
  de stdout, arquivos residuais, extração e registro após cada rejeição.
- [x] Manter instaladores PE32/x86, .NET/Mono e demais setups fora da execução.

Aceitação:

- [x] Roblox termina com `3`/`RBXCRASH`; G HUB e o alias terminam com `1`;
  Affinity termina com `4` antes da extração.
- [x] Rust ON registra o evento estruturado do parser MSIX; C++ OFF mantém o
  estágio C++ sem campos Rust; ambos deixam o ambiente sem cadastro parcial.
- [x] Nenhuma DLL específica, regra de aplicativo ou alteração do runtime foi
  adicionada; a matriz apenas repete o fluxo geral de instalação.

Evidência reproduzível de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust --output-on-failure -R
  '^popular_apps_install_matrix$'` passou com 4/4 casos em 12,24 s.
- [x] O mesmo filtro em `build/debug` passou com 4/4 casos em 15,59 s.
- [x] O build OFF não emitiu `backend="rust"`; ambos os stagings foram
  removidos depois da verificação de ausência de arquivos e cadastro.

### E24 — Sobrescrita controlada no 7-Zip CLI

Objetivo: validar a semântica genérica de extração sobre arquivos existentes,
sem aceitar que o staging antigo mascare bytes incorretos ou nomes Unicode.

Tarefas:

- [x] Preparar `input.txt` e `input-data/café-日本.txt` com conteúdo antigo
  antes de extrair o arquivo `7z/LZMA2` protegido.
- [x] Executar `7z x -aoa` com a senha correta e comparar os dois payloads
  byte a byte após a substituição.
- [x] Manter a extração normal, a senha incorreta, stdin/stdout e os ciclos de
  manutenção anteriores, repetindo tudo em Rust ON e C++ OFF.

Aceitação:

- [x] A extração com `-aoa` termina com `0` e `Everything is Ok`, substitui o
  conteúdo binário e Unicode e mantém o ciclo da `7z.dll` completo.
- [x] A rejeição de senha incorreta continua não produzindo payload válido; o
  caso novo não altera stdout nem os resultados anteriores.
- [x] Nenhuma regra, DLL ou tratamento específico de 7-Zip foi adicionado ao
  runtime; a cobertura permanece no smoke de aplicativo.

Evidência reproduzível de 2026-09-07:

- [x] O filtro `^seven_zip_cli_smoke$` passou em `build/debug-rust` após
  compilar o alvo (1/1, 8,72 s de CTest).
- [x] O mesmo filtro passou em `build/debug` após compilar o alvo (1/1,
  10,58 s de CTest).
- [x] Ambos emitiram `7-Zip CLI stored test/delete/update/rename/deflate/7z/stdin/password/overwrite/wrong-password
  lifecycle: ok` e removeram o staging temporário.

### E25 — Eventos WSA para clientes de rede genéricos

Objetivo: cobrir o modelo de eventos usado por clientes Win32 como o PuTTY,
sem criar código, DLL ou regra específica de aplicativo.

Tarefas:

- [x] Substituir os stubs de `WSACreateEvent`, `WSACloseEvent`,
  `WSASetEvent`, `WSAResetEvent`, `WSAEventSelect`,
  `WSAWaitForMultipleEvents` e `WSAEnumNetworkEvents` por objetos opacos
  manuais limitados, associação de sockets e tradução de prontidão Linux.
- [x] Preservar limites e falhas controladas: no máximo 64 eventos ativos,
  handles inválidos retornam `WSA_WAIT_FAILED`/`WSAEINVAL`, espera sem sinal
  retorna `WSA_WAIT_TIMEOUT` e a enumeração escreve somente o registro de
  44 bytes validado.
- [x] Expandir `tl_network_loopback` com TCP orientado a evento, `FD_ACCEPT`,
  timeout e `WSAEnumNetworkEvents`, mantendo UDP/`WSAPoll` e o cenário somente
  loopback.

Aceitação:

- [x] O teste unitário confirma sinalização manual, reset, espera imediata e
  timeout sem sinal.
- [x] `app_run_tl_network_loopback` passou em `build/debug-rust` depois da
  alteração, demonstrando associação real com socket, `FD_ACCEPT` e leitura
  do registro de eventos.
- [x] O smoke de configuração do PuTTY continua separado; uma sondagem real
  com servidor TCP local terminou com `exit 1` e zero bytes recebidos, portanto
  o SSH completo não foi declarado suportado nem mascarado por fallback.

Evidência reproduzível de 2026-09-07:

- [x] `build/debug-rust/tests/tradutorlinux_unit_tests
  --gtest_filter='PuttyCoverageTest.AllApisAndModules'` passou (1/1).
- [x] `ctest --test-dir build/debug-rust --output-on-failure -R
  '^app_run_tl_network_loopback$'` passou (1/1, 0,13 s).
- [x] O runtime foi recompilado somente como alvo `tradutorlinux`, com
  paralelismo 2; a sondagem manual sob Xvfb e listener em `127.0.0.1` foi
  encerrada e deixou apenas artefatos temporários em `/tmp`.

### E26 — Formatação segura de endpoints IPv4 no Winsock

Objetivo: completar uma operação genérica de `WS2_32.dll` usada por clientes
de rede Win32, mantendo o runtime sem regras específicas de aplicativo.

Tarefas:

- [x] Expor `WSAAddressToStringA` com validação de `sockaddr_in`, memória do
  convidado, capacidade de saída e comprimento retornado, sem sobrescrever
  buffers curtos.
- [x] Exercitar a API na fixture `tl_network_loopback`, que agora verifica a
  conversão de um endpoint após `getsockname`, além do teste unitário de
  formatação e sentinelas.
- [x] Confirmar o registro pelo nome no módulo `WS2_32.dll` e preservar o
  comportamento no build Rust ON e no baseline C++ OFF.

Aceitação:

- [x] O teste unitário valida `127.0.0.1:22`, o tamanho incluindo NUL,
  `WSAEFAULT` em capacidade insuficiente e a preservação do buffer-sentinela.
- [x] A matriz `tl_network_loopback` passou nos quatro cenários em
  `build/debug-rust` e `build/debug`; nenhum acesso externo foi usado.
- [x] A sondagem do `putty_x64.exe` sob Xvfb com listener local continua
  terminando com `exit 1` e zero bytes recebidos. A API genérica não é tratada
  como suporte ao PuTTY nem altera sua classificação na matriz.

Evidência reproduzível de 2026-09-07:

- [x] `WinSockTest.AddressToStringValidatesCapacityAndFormatsIpv4`,
  `PuttyCoverageTest.AllApisAndModules` e
  `ModuleTest.RegistersBuiltinKernel32Exports` passaram nos presets Rust ON e
  C++ OFF.
- [x] `fixture_tl_network_loopback_metadata`,
  `runtime_tl_network_loopback_matches_readobj`, `app_run_tl_network_loopback`
  e `report_tl_network_loopback_support` passaram nos dois presets.
- [x] A execução manual com listener local recebeu `0` bytes e retornou `1`;
  o resultado permanece uma limitação de integração GUI/SSH, não um skip
  funcional.

### E11 — Matriz CTest dos smokes GUI do corpus

Objetivo: tornar os cenários GUI reais já validados em D2/E1/E2 descobríveis e
repetíveis por CTest, sem executar instaladores automaticamente e sem colocar
regras de aplicativo no runtime.

Tarefas:

- [x] Registrar, somente com `TL_POPULAR_APPS_DIR` e Xvfb disponível,
  `7zFM_x64.exe`, `putty_x64.exe`, `WinRAR_x64.exe` e
  `Notepad++/notepad++.exe` nos alvos separados já existentes.
- [x] Manter um staging/Xvfb por cenário, timeout e `SKIP_RETURN_CODE 77` para
  a ausência real do servidor gráfico; falhas depois da inicialização do Xvfb
  continuam sendo falhas de teste.
- [x] Registrar labels por aplicativo e manter o build padrão sem downloads ou
  dependência do corpus.

Aceitação:

- [x] Os quatro testes são descobertos em Rust ON e C++ OFF quando o diretório
  do corpus é fornecido.
- [x] Cada smoke mantém o contrato funcional já documentado: Copy do 7-Zip,
  configuração do PuTTY, cancelamento do SFX do WinRAR e bloqueio controlado
  do Notepad++.
- [x] Nenhuma DLL, shim, regra de seleção ou mudança específica do aplicativo
  foi adicionada ao runtime.

Evidência E11 de 2026-09-07:

- [x] Após a configuração com
  `-DTL_POPULAR_APPS_DIR='/home/tonho/Área de trabalho/Aplicativos_Windows_Populares'`,
  `ctest -N` listou os quatro smokes em `build/debug-rust` e `build/debug`.
- [x] Os quatro testes foram executados nos dois builds e terminaram como
  `Skipped`, exit `77`, porque `/tmp/.X11-unix/X0` estava órfão e o diretório
  de sockets tinha proprietário incompatível; `xdpyinfo :0` não conectou e não
  havia processo Xvfb vivo. O CTest não classificou isso como sucesso funcional.
- [x] O mesmo ambiente já havia produzido evidência funcional dos quatro
  cenários em Xvfb válido; a repetição funcional foi concluída abaixo com
  Xvfb próprio e acesso gráfico permitido.
- [x] A repetição funcional de 2026-09-07 passou os quatro cenários em
  `build/debug-rust`: `seven_zip_gui_smoke` (2,67 s),
  `putty_real_gui_smoke` (11,40 s), `winrar_sfx_real_smoke` (0,65 s) e
  `notepadpp_real_gui_smoke` (2,86 s), sem skips.
- [x] A mesma matriz passou em `build/debug`: `seven_zip_gui_smoke` (2,70 s),
  `putty_real_gui_smoke` (11,41 s), `winrar_sfx_real_smoke` (0,52 s) e
  `notepadpp_real_gui_smoke` (2,29 s), sem skips.
- [x] Os smokes mantiveram seus contratos individuais de interação e
  encerramento nos dois backends; isso não promove compatibilidade geral dos
  aplicativos nem autoriza DLLs específicas.

### E27 — Rodada geral do tradutor no corpus

Objetivo: usar o TradutorLinux para identificar todo o corpus e repetir, de
forma controlada, os únicos fluxos de execução e instalação autorizados pelos
marcos anteriores.

Tarefas:

- [x] Analisar todos os 26 arquivos PE e o pacote MSIX com `--report`,
  preservando as rejeições esperadas de arquitetura, formato e estrutura.
- [x] Executar a matriz nativa autorizada para 7-Zip, os dois binários
  WinRAR, Rockstar Games Launcher e Rufus, com prefixo temporário, limites de
  CPU/memória e timeout.
- [x] Executar a matriz de instalação autorizada para Roblox, Logitech G HUB,
  o alias byte-a-byte e Affinity, verificando limpeza, ausência de cadastro
  parcial e diagnóstico Rust/C++.
- [x] Manter os instaladores PE32/x86, .NET/Mono, DLLs e demais aplicativos
  sem cenário autorizado fora da execução; eles continuam analisados e
  classificados, não iniciados indiscriminadamente.

Aceitação:

- [x] A matriz `popular_apps_report_matrix` passou 27/27 em Rust ON e C++
  OFF, usando respectivamente `build/debug-rust` e `build/debug`.
- [x] A matriz `popular_apps_native_matrix` passou 5/5 nos dois builds, sem
  timeout inesperado, sinal não controlado ou staging persistente.
- [x] A matriz `popular_apps_install_matrix` passou 4/4 nos dois builds; o
  MSIX inválido foi rejeitado antes da extração e o build OFF não emitiu
  campos de backend Rust.
- [x] A matriz GUI E11 passou separadamente nos dois builds com Xvfb próprio;
  nenhuma etapa alterou o corpus original ou criou DLL específica.

Evidência reproduzível de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust -R '^popular_apps_report_matrix$'`
  passou (25,27 s), `popular_apps_native_matrix` passou (3,76 s) e
  `popular_apps_install_matrix` passou (16,93 s).
- [x] `ctest --test-dir build/debug -R '^popular_apps_report_matrix$'` passou
  (23,08 s), `popular_apps_native_matrix` passou (1,10 s) e
  `popular_apps_install_matrix` passou (15,50 s).
- [x] As configurações confirmam `TL_BUILD_RUST=ON` em `build/debug-rust` e
  `TL_BUILD_RUST=OFF` em `build/debug`, ambas apontando para o mesmo corpus.
- [x] A execução completa do rótulo `apps` passou nos dois builds: no Rust ON,
  report 27/27, nativa 5/5 e instalação 4/4; no C++ OFF, os mesmos resultados.
  Os quatro smokes GUI foram `Skipped` com retorno 77 somente porque não havia
  Xvfb disponível nessa execução; a validação funcional com Xvfb próprio está
  registrada na E11.

### E28 — Fluxo SSH local determinístico do PuTTY

Objetivo: transformar o bloqueio restante do PuTTY em um cenário reproduzível
de rede e GUI, exercitando apenas capacidades genéricas do runtime. Esta etapa
não cria DLL, shim ou regra específica para o aplicativo.

Escopo:

- manter PE32/x86, HWiNFO empacotado, Rufus com entry point `RWX`, instaladores
  e serviços externos fora da execução;
- iniciar um servidor TCP local controlado pelo teste, sem Internet, para
  separar conexão, troca inicial de versão, prompt e encerramento;
- usar somente `tests/apps/putty/` para o harness e fixtures do cenário;
- investigar o trace ON/OFF antes de qualquer API nova e só implementar uma
  capacidade genérica quando houver contrato, fixture mínima e regressão;
- preservar o smoke atual de abertura/fechamento da configuração como
  regressão independente.

Tarefas:

- [x] Definir o protocolo mínimo do servidor local e os critérios observáveis
  de sucesso, rejeição e encerramento, com timeout externo e prefixo isolado.
  O probe abre um listener TCP em `127.0.0.1` numa porta efêmera, aceita no
  máximo uma conexão, lê uma linha limitada a 256 bytes e só considera válido
  um banner `SSH-*` terminado em `\r\n`; o servidor encerra em 10 s.
- [x] Criar o smoke interativo que configure o PuTTY sem depender de cliques
  por coordenadas frágeis, valide a criação da configuração, a seleção do
  provedor `WS2_32.dll`, o timeout controlado e compare Rust ON com C++ OFF.
  A entrada é enviada por `KeyPress`/`KeyRelease` X11 direcionados à janela,
  usando foco inicial, Tab e Return; o cenário não acessa a Internet.
- [x] Capturar a primeira falha genérica após a conexão e criar uma fixture
  mínima antes de alterar `src/runtime/` ou módulos compartilhados. O primeiro
  bloqueio continua anterior ao handshake: o servidor recebeu zero bytes e o
  trace não registrou chamada de socket/conexão; portanto nenhuma API nova foi
  justificada nesta rodada.
- [x] Repetir `--report`, execução, limpeza e ausência de processos residuais;
  não promover o PuTTY a suporte geral por um handshake parcial. O novo
  `putty_ssh_local_probe` e a matriz anterior de `--report` foram repetidos nos
  builds Rust ON e C++ OFF, com prefixo temporário e Xvfb encerrado ao final.

Critérios de saída:

- [x] O cenário local termina com stdout, exit code e trace determinísticos em
  Rust ON e C++ OFF, ou falha controladamente com a limitação identificada.
  Nos dois builds, a configuração é criada, o listener recebe zero bytes e o
  processo termina com `guest-timeout 72`; o probe registra esse estado como
  limitação esperada e retorna sucesso ao CTest.
- [x] Não há acesso externo, fallback silencioso, DLL específica ou alteração
  do caminho de outros aplicativos. O código novo está isolado em
  `tests/apps/putty/` e no cadastro CTest; o runtime geral não foi alterado.
- [x] A matriz de compatibilidade registra separadamente conexão parcial,
  configuração GUI e SSH completo: a configuração permanece validada, a
  conexão/handshake permanece não alcançada e o SSH completo segue fora do
  suporte declarado.

Evidência reproduzível de 2026-09-07:

- [x] `cmake --build build/debug-rust --target putty_ssh_smoke --parallel 2`
  e `cmake --build build/debug --target putty_ssh_smoke --parallel 2` passaram.
- [x] `ctest --test-dir build/debug-rust -R '^putty_ssh_local_probe$'
  --output-on-failure` passou em 10,17 s.
- [x] `ctest --test-dir build/debug -R '^putty_ssh_local_probe$'
  --output-on-failure` passou em 10,33 s.
- [x] As execuções manuais ON/OFF emitiram a mesma conclusão:
  `configuration reached, no bytes sent, guest-timeout 72`; o servidor foi
  encerrado com resultado inválido e nenhum processo de teste permaneceu.

### E29 — WinSock carregado dinamicamente

Objetivo: separar a capacidade genérica de carregar `WS2_32.dll` em tempo de
execução da interação ainda bloqueada do PuTTY. Esta etapa não adiciona regra,
DLL ou shim específico para aplicativo.

Tarefas:

- [x] Criar a fixture PE32+ `tl_dynamic_ws2.exe`, que importa somente APIs
  básicas de `KERNEL32.dll`, carrega `ws2_32.dll` com `LoadLibraryA`, resolve
  `WSAStartup`, `WSACleanup`, `socket` e `closesocket` com `GetProcAddress` e
  abre/fecha um socket `AF_INET`/`SOCK_STREAM`.
- [x] Cadastrar metadata, `--report` e execução da fixture na matriz CTest,
  com saída `dynamic-ws2\n` e comparação Rust ON/C++ OFF.
- [x] Repetir o cenário nos dois builds. Rust ON e C++ OFF passaram em
  metadata, report e execução; o trace confirmou quatro seleções do provedor
  builtin de `ws2_32.dll`, sem alterar o runtime.
- [x] Registrar que a primeira execução dentro do sandbox retornou erro 13 ao
  criar o socket por política ambiental. A repetição fora do sandbox passou;
  a diferença é limitação de ambiente, não um skip funcional nem uma falha do
  contrato WinSock.

Critérios de saída:

- [x] A carga dinâmica e a resolução de exports são exercitadas sem imports
  estáticos de `WS2_32.dll` e sem código específico do PuTTY.
- [x] stdout, exit code, metadata e report coincidem em Rust ON e C++ OFF.
- [x] O probe SSH do PuTTY alcança a ação genérica de abertura da sessão e cria
  a janela principal `PuTTY`; o listener ainda recebe zero bytes, então a
  investigação de rede permanece separada e não amplia APIs sem nova evidência.

Evidência reproduzível de 2026-09-07:

- [x] `runtime_tl_dynamic_ws2_matches_readobj`, `app_run_tl_dynamic_ws2` e
  `report_tl_dynamic_ws2_support` passaram em `build/debug-rust`.
- [x] Os mesmos três testes, além de `fixture_tl_dynamic_ws2_metadata`,
  passaram em `build/debug`.
- [x] O arquivo real do PuTTY não possui imports estáticos de `WS2_32.dll`,
  mas contém referências a `ws2_32.dll`/WinSock e usa `LoadLibraryA` e
  `GetProcAddress`; portanto o próximo diagnóstico é a ativação da sessão,
  não uma nova implementação app-specific.

### E30 — Contratos genéricos de controles na configuração GUI

Objetivo: estabilizar os contratos comuns usados pela janela de configuração do
PuTTY sem introduzir código, DLL, shim ou regra específica do aplicativo. O
handshake SSH continua fora do escopo; esta etapa cobre somente a preparação
genérica da interface e a identificação do próximo bloqueio.

Tarefas:

- [x] Manter controles criados dinamicamente por `CreateWindowExA` no índice
  lógico do diálogo, permitindo `GetDlgItem` e o ciclo de vida correto durante
  `WM_DESTROY`.
- [x] Implementar o subconjunto genérico de `SysTreeView32` necessário para
  inserir, excluir, selecionar, expandir, navegar e consultar itens por
  `SendMessageA/W`, com referências lógicas estáveis e notificações básicas.
- [x] Implementar foco inicial, Tab e ativação de botão padrão para janelas
  regulares, sem processar duas vezes as teclas que já pertencem a
  `IsDialogMessageW` em diálogos modais.
- [x] Proteger cada comportamento com testes unitários de controles e repetir
  os smokes de GUI, 7-Zip, PuTTY e WinSock nos caminhos Rust ON/C++ OFF.

Critérios de saída:

- [x] `CommonControls.*` passou com 14 testes, incluindo o modelo de TreeView,
  seleção, foco e navegação por Tab.
- [x] `runtime_gui_smoke`, os fluxos GUI de 7-Zip/PuTTY e os fixtures
  `tl_dynamic_ws2` passaram no build Rust ON; o baseline C++ OFF recompilou os
  alvos afetados sem alteração de seleção de backend.
- [x] O PuTTY alcança a configuração sem o abort de inicialização e termina no
  limite controlado `guest-timeout 72`; o listener local ainda recebe zero
  bytes, portanto o SSH completo não é declarado como suportado.
- [x] Nenhum tratamento específico do PuTTY foi adicionado ao runtime; a
  correção fica em contratos genéricos de janela, diálogo, foco e TreeView.

Evidência reproduzível de 2026-09-07:

- [x] `cmake --build build/debug-rust --target tradutorlinux_unit_tests
  tradutorlinux putty_ssh_smoke --parallel 2` passou, seguido por 14 testes
  `CommonControls` e `runtime_gui_smoke`.
- [x] `ctest --test-dir build/debug-rust -R
  'runtime_tl_7zfm_gui_matches_readobj|app_run_tl_7zfm_gui|runtime_tl_putty_matches_readobj|app_run_tl_putty|report_tl_putty_support'`
  passou; os dois testes `tl_dynamic_ws2` passaram fora do sandbox por
  exigirem socket local.
- [x] `putty_ssh_local_probe` passou fora do sandbox com a limitação esperada
  e stdout `configuration reached, no bytes sent, guest-timeout 72`; o mesmo
  smoke foi mantido no harness sem instrumentação temporária.

### E31 — Matriz recursiva do corpus e ativação da sessão PuTTY

Objetivo: fechar a cobertura de análise do diretório real de aplicativos e
registrar a ação GUI de abertura da sessão do PuTTY, sem iniciar DLLs ou
instaladores rejeitados e sem criar tratamento específico no runtime.

Tarefas:

- [x] Repetir `--report` para os 27 arquivos de primeiro nível já catalogados
  nos builds Rust ON e C++ OFF: 27/27 passaram em cada build, com os mesmos
  códigos para PE válido, arquitetura não suportada e formato rejeitado.
- [x] Criar `popular_apps_recursive_report_matrix`, cobrindo os 64
  PE/DLL/MSIX encontrados recursivamente, inclusive cópias extraídas de
  7-Zip/Notepad++; a matriz permanece somente de análise e fixa o exit
  esperado por arquivo.
- [x] Repetir a matriz recursiva nos dois backends: Rust ON e C++ OFF passaram
  64/64, com a mesma distribuição `success=25`, `malformed=2` e
  `unsupported=37`.
- [x] Alterar somente o smoke em `tests/apps/putty/` para enviar um clique
  controlado ao botão `Open` após preencher host/porta, confirmar a criação
  da janela `PuTTY` e manter o servidor limitado a loopback.
- [x] Repetir a ativação nos dois builds: a configuração e a janela de sessão
  são alcançadas, o servidor recebe zero bytes e o processo termina no
  `guest-timeout 72`; nenhuma syscall de `socket`/`connect` do convidado foi
  observada depois do `Open`.

Critérios de saída:

- [x] O corpus recursivo possui uma análise automatizada reproduzível, sem
  transformar DLLs, pacotes ou instaladores incompatíveis em casos de
  execução.
- [x] O fluxo do PuTTY separa configuração, ativação da janela e conexão:
  somente as duas primeiras etapas foram alcançadas nos builds ON/OFF.
- [x] Nenhum arquivo específico do aplicativo foi adicionado ao runtime; a
  alteração fica no harness de teste e preserva stdout, timeout e limpeza.

Evidência reproduzível de 2026-09-07:

- [x] `ctest --test-dir build/debug-rust -R '^popular_apps_report_matrix$'`
  e `'^popular_apps_recursive_report_matrix$'` passaram; a matriz recursiva
  terminou em 64/64 casos.
- [x] Os mesmos dois testes passaram em `build/debug` (C++ OFF), com saída e
  classificação equivalentes.
- [x] `popular_apps_native_matrix` e `popular_apps_install_matrix` passaram
  em Rust ON e C++ OFF: 6/6 execuções controladas e 8/8 casos de instalação
  controlada por build.
- [x] `putty_ssh_local_probe` passou em ambos os builds após o clique em
  `Open`, sem bytes no listener e sem processo residual.

Limites mantidos:

- CPU-Z, GPU-Z, HWMonitor, RTSS e os demais PE32/x86 continuam rejeitados
  antes da execução; HWiNFO continua rejeitado por imagem empacotada e
  Affinity por pacote fora do limite estrutural.
- DLLs e instaladores não são iniciados indiscriminadamente. Instalação só é
  exercitada pelos oito casos com staging, timeout, memória e limpeza
  controlados já aprovados pela matriz.
- A criação da janela de sessão não é handshake SSH nem suporte funcional do
  PuTTY; o próximo bloqueio exige evidência nova do fluxo interno antes de
  qualquer API adicional.

### E32 — Conversão OEM com `MB_USEGLYPHCHARS`

Objetivo: corrigir um contrato Win32 genérico usado pelo PuTTY durante a
inicialização da sessão, sem introduzir código, DLL, shim ou regra específica
do aplicativo.

Evidência inicial:

- [x] O trace do PuTTY mostrou 256 chamadas a
  `MultiByteToWideChar(437, 0xC, ..., 1, ..., 1)`; o runtime rejeitava
  `0x4` (`MB_USEGLYPHCHARS`) como parâmetro inválido.
- [x] A chamada era repetida durante a construção da tabela de conversão OEM,
  antes do fluxo de conexão, e por isso foi registrada como limitação real do
  runtime, não como comportamento específico do harness.

Correção genérica:

- [x] Adicionar `kMbUseGlyphChars` ao contrato Win32 compartilhado e aceitar a
  combinação com `MB_ERR_INVALID_CHARS`.
- [x] Mapear os caracteres de controle OEM CP437 para seus glyphs quando a
  flag estiver presente, mantendo a conversão anterior para as demais flags e
  páginas suportadas.
- [x] Adicionar teste unitário para os glyphs CP437 e preservar os testes de
  validação de buffers, UTF-8, CP1252 e páginas não suportadas.
- [x] Manter a alteração em `src/runtime/` genérico; nenhum tratamento do
  PuTTY foi adicionado ao runtime ou ao loader.

Critérios de saída:

- [x] Os 17 testes `Win32CodePageTest.*`/`Win32LocaleTest.*` relevantes
  passaram no build Rust ON.
- [x] O smoke GUI real do PuTTY foi repetido após a correção. A etapa de
  conversão OEM deixou de falhar, a configuração e a janela da sessão ainda
  são alcançadas, mas o listener continua recebendo zero bytes e o processo
  termina no `guest-timeout 72`.
- [x] O resultado não promove suporte SSH: a correção removeu uma limitação
  genérica, enquanto o próximo bloqueio continua sendo a ativação da conexão.

Evidência reproduzível de 2026-09-08:

- [x] `cmake --build build/debug-rust --target tradutorlinux
  tradutorlinux_unit_tests putty_ssh_smoke --parallel 2` passou.
- [x] `build/debug-rust/tests/tradutorlinux_unit_tests
  --gtest_filter='Win32CodePageTest.*:Win32LocaleTest.*'` passou com 17/17.
- [x] `build/debug-rust/tests/putty_ssh_smoke
  build/debug-rust/src/tradutorlinux
  '/home/tonho/Área de trabalho/Aplicativos_Windows_Populares/putty_x64.exe'`
  reproduziu `configuration reached, no bytes sent, guest-timeout 72` sem
  processo residual.
- [x] No baseline C++ OFF, os mesmos 17 testes de locale, a matriz de report
  do corpus e as fixtures WinSock passaram fora do sandbox; o probe PuTTY
  reproduziu exatamente `configuration reached, no bytes sent,
  guest-timeout 72`.

Próximo bloqueio:

- O PuTTY ainda não executa `socket`/`connect` depois de `Open`; qualquer
  próxima correção deve começar por nova evidência do fluxo interno ou por uma
  fixture genérica, sem adicionar APIs especulativas.

### E33 — Isolamento do bloqueio pós-ativação do PuTTY

Objetivo: separar um defeito de contrato Win32 genérico de um loop interno do
binário depois da criação da sessão, sem transformar o harness do PuTTY em
regra de produção.

Evidência coletada:

- [x] Instrumentação temporária confirmou que o `WM_COMMAND` do botão `Open`
  chega ao `DispatchMessageA`, a janela `PuTTY` é criada e seu `WM_CREATE`
  retorna normalmente.
- [x] Depois de `GetOEMCP`, as 256 chamadas de
  `MultiByteToWideChar(CP437, MB_USEGLYPHCHARS | MB_ERR_INVALID_CHARS, ...)`
  completam, inclusive o último byte da tabela OEM; não há loop infinito no
  contrato de conversão corrigido em E32.
- [x] Após a conversão, não foram observadas chamadas convidadas a
  `getaddrinfo`, `socket`, `connect`, eventos WinSock ou outra API de runtime
  antes do `guest-timeout 72`; o resultado continua idêntico nos builds Rust
  ON e C++ OFF.
- [x] A instrumentação de `DispatchMessageA`, da conversão OEM e a retenção
  temporária do staging foram removidas; não houve alteração permanente no
  runtime, no loader ou no teste específico.

Evidência adicional reproduzível de 2026-09-08:

- [x] Uma amostra temporária do contexto no timeout, solicitada pelo handler
  de sinais do processo isolado, capturou `RIP` no hospedeiro e o resolveu
  para `tradutorlinux::set_last_error`. A amostra ocorreu depois do evento
  `GetOEMCP(CP437)` e da criação da janela `PuTTY`, antes de qualquer evento
  de `getaddrinfo`, `socket` ou `connect`; o endereço não era um `RIP` do PE
  em `0x140...`.
- [x] A instrumentação de amostragem e a impressão temporária do trace foram
  removidas, os alvos `tradutorlinux` e `putty_ssh_smoke` foram recompilados e
  o smoke continuou com a mesma limitação controlada. Nenhuma alteração de
  produção ou do contrato Win32 foi mantida.
- [x] Uma sondagem temporária dos chamadores de `set_last_error` mostrou a
  sequência repetida `GetMessageA` → `IsDialogMessageW` → `DispatchMessageA`,
  com consultas de controles entre as iterações. O mesmo trace não registrou
  chamadas a `timeSetEvent` ou `timeKillEvent` no caminho pós-`Open`; portanto
  não há evidência para corrigir WinMM nesse cenário.
- [x] A sondagem foi removida, o alvo Rust foi recompilado e o binário voltou
  ao contrato anterior de `set_last_error`/WinMM. O resultado permanece
  `configuration reached, no bytes sent, guest-timeout 72`.
- [x] A inspeção temporária de `GetProcAddress` confirmou que as APIs WinSock
  requisitadas pelo PuTTY (`WSAStartup`, `socket`, `connect`, `select`,
  `WSAEventSelect`, `WSAIoctl` e demais símbolos do conjunto) são resolvidas.
  As falhas observadas foram apenas consultas opcionais de GUI (`MakeDragList`,
  `LBItemFromPt`, `DrawInsert`, `ToUnicodeEx` e
  `SetCurrentProcessExplicitAppUserModelID`); nenhuma delas é chamada pelo
  fluxo observado antes do bloqueio.
- [x] A inspeção de nomes também foi removida e os alvos `tradutorlinux` e
  `putty_ssh_smoke` foram recompilados. Não foi adicionado fallback, stub ou
  regra específica para o aplicativo.

Conclusão:

- [x] A evidência não justifica alterar WinSock, adicionar uma API especulativa
  ou criar um tratamento específico do PuTTY.
- [x] O bloqueio permanece classificado como investigação pendente do fluxo
  interno pós-conversão; uma próxima mudança só será aceita com backtrace,
  fixture genérica reproduzível ou contrato Win32 documentado que a sustente.

Próximo passo permitido:

- [x] Obter nova evidência do ponto interno em que o convidado permanece após
  `init_ucs`/a criação da sessão: o último ponto observável é o helper
  hospedeiro `set_last_error`, sem identificação ainda da API convidada que o
  chama repetidamente.
- [x] Reproduzir o comportamento em uma fixture genérica ou obter um contrato
  Win32 específico antes de corrigir o runtime; sem isso, não implementar
  APIs especulativas nem tratar o PuTTY como suportado.

### E34 — Determinismo da matriz nativa e do smoke do Notepad++

Objetivo: corrigir falhas do próprio harness que dependiam do ambiente gráfico
ou da carga paralela, sem transformar esses sintomas em mudanças no runtime.

Problemas reproduzidos:

- [x] `popular_apps_native_matrix` falhava quando herdava `DISPLAY`: os dois
  nomes do mesmo WinRAR SFX abriam a janela `WinRAR self-extracting archive` e
  aguardavam interação, terminando em `guest-timeout 72` em vez de `exit 0`.
- [x] Com `DISPLAY` removido, o mesmo binário completou o cenário headless com
  `ExitProcess` código `0`, confirmando que a falha era ambiental e não uma
  regressão do loader ou do aplicativo.
- [x] `notepadpp_real_gui_smoke` passou isoladamente, mas sob carga paralela a
  margem interna de 3 segundos podia terminar em timeout antes do diagnóstico
  C++/SEH esperado.

Correção do harness:

- [x] `verify_popular_apps_native.cmake` agora remove `DISPLAY` e
  `WAYLAND_DISPLAY` antes da matriz nativa, tornando o cenário sem interação
  determinístico.
- [x] O smoke do Notepad++ usa 8 segundos e 8 segundos de CPU, mantendo os
  mesmos requisitos de janela, `cxx-throw`, `guest-signal` e ausência de
  `guest-timeout`; nenhum erro é convertido em skip.
- [x] Nenhuma DLL, shim, regra de aplicativo, loader ou API de produção foi
  alterada.

Evidência reproduzível de 2026-09-08:

- [x] Rust ON: os 10 casos selecionados de análise, execução, instalação,
  MSIX, GUI e catálogo passaram com `ctest -j2`.
- [x] C++ OFF: os mesmos 10 casos passaram com `ctest -j2`, incluindo a
  matriz nativa headless e o smoke do Notepad++.
- [x] `git diff --check` permaneceu limpo após a alteração e as falhas
  originais não reapareceram na repetição completa.

### E35 — Retomada da execução do 7-Zip e limites do corpus

Objetivo: ampliar a evidência de execução real depois da correção dos
harnesses e manter explícitas as classes que ainda não podem ser executadas
no alvo PE32+ AMD64 atual.

Evidência reproduzível de 2026-09-08:

- [x] Rust ON: `seven_zip_cli_smoke` passou cobrindo o ciclo CLI de arquivos
  `stored`, `DEFLATE`, `7z/LZMA2`, senha, sobrescrita e stdin/stdout; o
  `seven_zip_gui_smoke` também passou com a janela do File Manager e a cópia
  controlada.
- [x] C++ OFF: os mesmos dois smokes passaram isoladamente, preservando a
  equivalência do caminho operacional.
- [x] A análise individual confirmou que `Everything_Search_x64.exe`,
  `CPU-Z_2.18_en.exe`, `GPU-Z_2.70.0.exe`, `HWMonitor_1.67.exe` e o instalador
  CapCut são PE32/x86 (`machine=0x14c`) e permanecem rejeitados antes da
  execução, conforme o escopo do runtime.
- [x] `HWiNFO64.exe` é PE32+ mas possui imagem empacotada com `UPX0` sem dados
  crus correspondentes ao RVA de export; Rust e C++ rejeitam a estrutura sem
  mapear ou executar o arquivo (`exit 4`).
- [x] Não foi adicionada tentativa de instalar ou executar indiscriminadamente
  esses casos: x86 exige uma etapa de arquitetura própria e HWiNFO exige
  análise de desempacotamento, ambas fora do marco atual.

### E36 — Diagnóstico do callback multimídia no Roblox

Objetivo: verificar se o `timeSetEvent` stub é a causa imediata do
`RBXCRASH` do instalador Roblox, sem promover callbacks assíncronos nem
adicionar uma regra específica ao runtime.

Evidência reproduzível de 2026-09-08:

- [x] O instalador foi executado em prefixo e `APPDATA` temporários, com
  `--trace`, `--cpu 3`, `--memory 512` e timeout externo de 25 segundos. O
  cenário normal continua terminando com `RBXCRASH: FatalRuntimeError
  (RSL - panic: e374e9c-Worker,28)`, `ExitProcess(3)` e `failed
  stage="setup"`; o prefixo não recebeu arquivos.
- [x] Um experimento temporário, removido imediatamente depois, invocou uma
  única vez o callback convidado recebido por `timeSetEvent`. O callback
  retornou normalmente, mas o processo produziu o mesmo `RBXCRASH`, exit `3`
  e ausência de arquivos. O experimento não foi mantido no código nem no
  binário validado.
- [x] O alvo Rust foi recompilado após a remoção do experimento; `git status`
  e `git diff --check` ficaram limpos. Não houve alteração no build C++, no
  loader, no instalador, no catálogo ou na política de execução.

Conclusão:

- [x] A evidência não sustenta substituir o stub por um agendador síncrono ou
  assíncrono. O bloqueio permanece no fluxo interno `Worker/RSL` do setup,
  não em uma falha demonstrada de retorno do callback.
- [x] `timeSetEvent` continua explicitamente classificado como stub genérico;
  qualquer implementação futura exige contrato de ciclo de vida, fixture
  independente de aplicativo, teste ON/OFF e evidência de que o contrato é
  usado por mais de um cenário.
- [x] Roblox continua como instalação controlada com exit `3`, sem suporte
  funcional declarado e sem cadastro parcial.

### E37 — Fixture genérica de loop GUI com WinSock dinâmico

Objetivo: reproduzir em um executável mínimo o mecanismo genérico de uma
janela que recebe uma mensagem enfileirada, executa trabalho no callback e
encerra pelo loop `GetMessageA`/`DispatchMessageA`, incluindo a resolução
dinâmica de WinSock. A fixture não contém nomes, regras ou comportamento do
PuTTY.

Implementação:

- [x] `tests/samples/src/tl_gui_dynamic_ws2.c` registra uma classe, cria a
  janela, chama `PostMessageA`, despacha uma mensagem privada e, somente no
  callback, resolve `WSAStartup`, `socket`, `closesocket` e `WSACleanup` por
  `LoadLibraryA`/`GetProcAddress`. Depois escreve um marcador e encerra com
  `DestroyWindow`/`PostQuitMessage`.
- [x] O manifesto, o valor esperado e o cadastro CMake ficam em
  `tests/samples/`, como fixture genérica compartilhada; nenhuma DLL, shim,
  API ou regra específica de aplicativo foi adicionada ao runtime.

Evidência reproduzível de 2026-09-08:

- [x] Rust ON: `fixture_tl_gui_dynamic_ws2_metadata`,
  `runtime_tl_gui_dynamic_ws2_matches_readobj`,
  `app_run_tl_gui_dynamic_ws2` e `report_tl_gui_dynamic_ws2_support` passaram
  com `ctest --parallel 2`; os dois testes de execução foram repetidos com
  Xvfb para fornecer o backend gráfico.
- [x] C++ OFF: os mesmos quatro testes passaram com `ctest --parallel 2` e
  Xvfb, confirmando que a execução do mecanismo não depende do parser Rust.
- [x] A fixture confirmou a sequência completa de criação de janela,
  mensagem enfileirada, `DispatchMessageA`, resolução dinâmica de WinSock,
  criação/fechamento de socket e saída `0`; report e validação estrutural
  também passaram.
- [x] Os testes não alteraram o diagnóstico do PuTTY: o smoke continua sem
  chamadas convidadas a `socket`/`connect` depois de `Open` e termina no
  `guest-timeout 72`. Portanto, a fixture não autoriza uma correção
  especulativa nem uma declaração de suporte SSH.

Conclusão:

- [x] O contrato genérico de loop de mensagens e WinSock dinâmico usado pela
  fixture funciona nos builds Rust ON e C++ OFF.
- [x] O bloqueio do PuTTY permanece específico do fluxo interno observado
  após a configuração; qualquer nova mudança precisa de outra evidência
  reproduzível ou de um contrato Win32, sem código específico do aplicativo.

### E38 — Localização do loop convidado após o comando Open do PuTTY

Objetivo: localizar no PE o ponto que continua ativo depois da criação da
janela principal e verificar se o bloqueio corresponde a uma API genérica
ausente no runtime.

Evidência reproduzível de 2026-09-08:

- [x] Uma sondagem temporária de `DispatchMessageA` registrou o retorno do
  convidado `0x140052d78`, equivalente ao RVA `0x52d78` no `.text` do PuTTY.
  A desmontagem do PE mostra o laço esperado: `GetMessageA`, teste do retorno,
  `IsDialogMessageW`, `DispatchMessageA` e salto de volta ao início.
- [x] O cenário de interação entregou os eventos de teclado, o clique e o
  `WM_COMMAND` final com `wparam=1009`; esse comando criou a janela principal
  `PuTTY` e o `WM_CREATE` retornou `0`.
- [x] Depois da criação da janela principal, não foram observadas chamadas
  convidadas a `CreateThread`, `PostMessageA`, `SetTimer`, `timeSetEvent`,
  `socket` ou `connect`. O processo permaneceu no laço de mensagens até o
  `guest-timeout 72` e o servidor local recebeu zero bytes.
- [x] As sondagens de `DispatchMessageA`, `PostMessageA`, `SetTimer` e
  `CreateThread`, bem como a preservação temporária do staging, foram
  removidas. O código e o teste voltaram ao comportamento anterior, sem
  diagnóstico permanente ou regra específica de aplicativo.

Conclusão:

- [x] O novo ponto observado é código convidado do próprio PuTTY que retorna
  ao laço de mensagens; não há evidência de uma API genérica rejeitada que
  justifique uma correção no runtime.
- [x] O problema permanece uma limitação de ativação interna da sessão, não
  uma falha demonstrada do transporte WinSock. PuTTY continua sem suporte
  SSH declarado e sem fallback ou tratamento específico.

### E39 — Rejeição segura de handles CryptMsg desconhecidos

Objetivo: corrigir uma inconsistência genérica de `CRYPT32.dll` encontrada
durante a investigação dos aplicativos que consultam certificados, sem
fabricar estado CMS/Authenticode e sem criar tratamento específico para o
Notepad++.

Problema reproduzido:

- [x] `CryptMsgGetParam` retornava sucesso e tamanho zero mesmo quando recebia
  handle nulo ou desconhecido. Isso permitia que o convidado continuasse com
  dados de mensagem inexistentes e produzisse exceções C++ com mensagens como
  `The handle is invalid`.
- [x] A tentativa experimental de deixar `0xE06D7363` seguir o dispatcher SEH
  confirmou que os handlers convidados eram alcançados, mas terminou em
  `SIGSEGV` durante o caminho de certificado. A alteração experimental foi
  removida; exceções C++ gerais continuam fora do contrato.

Correção genérica:

- [x] `CryptMsgGetParam` agora rejeita todo handle que não foi emitido pelo
  runtime com `FALSE`, `ERROR_INVALID_HANDLE` e `pcbData=0`; `pcbData` nulo
  retorna `ERROR_INVALID_PARAMETER`.
- [x] Nenhum handle, buffer, CMS ou certificado fictício é criado. O caminho
  `CryptQueryObject` continua retornando `ERROR_NOT_SUPPORTED` para formatos
  não implementados.
- [x] Foi adicionada a regressão
  `Crypt32Test.CryptMsgGetParamRejectsUnknownHandles`; a mudança permanece em
  `src/runtime/dlls/crypto/crypt32.cpp` e não contém regra de aplicativo.

Evidência reproduzível de 2026-09-08:

- [x] Rust ON: os quatro testes `Crypt32Test.*` passaram, incluindo a nova
  rejeição de handles.
- [x] C++ OFF: os mesmos quatro testes passaram, sem campos ou símbolos Rust.
- [x] O smoke do Notepad++ passou nos dois builds mantendo o bloqueio
  controlado esperado (`exit 71`); a correção não foi promovida como suporte
  GUI nem como implementação de Authenticode.
- [x] A instalação do G HUB permaneceu controlada nos dois builds (`exit 1`),
  sem arquivos residuais, extração ou cadastro.
- [x] `git diff --check` passou antes do registro desta etapa.

Conclusão:

- [x] A semântica de erro para handles CMS desconhecidos agora é fechada e
  determinística, evitando sucesso falso e dados parciais.
- [x] Notepad++ continua limitado por Authenticode/CMS e exceções C++; G HUB
  continua sendo setup encerrado pelo próprio convidado. O próximo avanço
  funcional exigirá uma fixture e contrato próprios para CMS/Authenticode ou
  outra decisão explícita de escopo.

### E40 — Fechamento seguro de CryptMsgClose

Objetivo: completar a rejeição fechada do subconjunto `CRYPT32.dll` enquanto
CMS/Authenticode não possuem implementação, sem devolver sucesso para um
handle que o runtime não controla.

Problema e correção:

- [x] `CryptMsgClose` aceitava `NULL` e ponteiros arbitrários como se fossem
  mensagens válidas, retornando sucesso mesmo sem existir estado CMS emitido.
- [x] A API agora retorna `FALSE` + `ERROR_INVALID_HANDLE` para handles nulos
  ou desconhecidos. Nenhum estado, memória ou certificado é fabricado.
- [x] Foi adicionada a regressão
  `Crypt32Test.CryptMsgCloseRejectsUnknownHandles`, mantendo a regra genérica
  em `src/runtime/dlls/crypto/crypt32.cpp`.

Evidência reproduzível de 2026-09-08:

- [x] Rust ON e C++ OFF: os cinco testes `Crypt32Test.*` passaram, cobrindo
  certificados, stores, `CryptQueryObject`, `CryptMsgGetParam` e
  `CryptMsgClose`.
- [x] A matriz de análise, execução e instalação do corpus continua passando
  nos dois backends; nenhum instalador deixa arquivos ou cadastro parcial.
- [x] `CryptMsgClose` e `CryptMsgGetParam` permanecem rejeições controladas,
  enquanto Authenticode, CMS e exceções C++ continuam explicitamente fora do
  suporte funcional.

### E41 — Reabertura controlada da extração do WinRAR SFX

Objetivo: reavaliar o fluxo de extração depois das correções genéricas de OLE,
posição de arquivo, atributos, threads e handles `CRYPT32`, sem transformar a
sondagem em suporte declarado ao WinRAR.

Evidência reproduzível de 2026-09-08:

- [x] Uma sondagem temporária enviou `Return` à janela do SFX sob Xvfb próprio.
  O convidado passou por `environment expand`, criou a classe
  `RarHtmlClassName`, enumerou `WinRAR_x64.exe`, `Descript.ion`, `ReadMe.txt`,
  `License.txt` e `Rar.txt`, e executou todas as chamadas observadas de
  `SetFileAttributesW` com `status="success"`.
- [x] Com o comportamento seguro atual, a sequência termina no
  `cxx-throw ignored` e em `guest-signal`/`SIGTRAP`; não foi introduzido um
  atalho novo nem uma regra específica de aplicativo.
- [x] Em uma segunda experiência temporária, o dispatcher deixou o código
  `0xE06D7363` alcançar os handlers convidados. O tipo lançado foi
  `.?AW4RAR_EXIT@@`; o unwind encontrou handlers e um alvo, mas o SFX entrou
  em repetição de expansão de ambiente até o timeout, sem concluir a
  extração. A alteração foi revertida.
- [x] A sondagem e as mudanças de teste foram removidas. O smoke oficial de
  cancelamento voltou a passar em Rust ON e C++ OFF, ambos com saída `0`.

Conclusão:

- [x] As correções anteriores eliminaram os bloqueios genéricos de atributos,
  streams OLE e handles inválidos; o próximo bloqueio é a semântica completa
  de exceções C++/destrutores do fluxo de extração.
- [x] Não há correção segura de uma API isolada para aplicar nesta etapa.
  Implementar esse caminho exige fixture PE32+ de exceção C++, contrato de
  `__CxxFrameHandler*`/unwind e testes ON/OFF antes de qualquer promoção do
  WinRAR.

### E42 — Contrato genérico para exceções C++ x64

Objetivo: preparar a próxima frente de compatibilidade sem transformar o
comportamento observado no WinRAR em regra específica. O alvo é um contrato
genérico para imagens PE32+ AMD64 que usam a ABI de exceções C++ da Microsoft,
incluindo `0xE06D7363`, `__CxxFrameHandler*`, mapas de `try/catch` e destrutores.

Evidência reproduzível de 2026-09-08:

- [x] A captura controlada do SFX confirmou que ambiente, enumeração de
  arquivos e `SetFileAttributesW` completam antes do lançamento; a sequência
  de extração ainda termina no caminho de exceção C++ e não conclui a escrita.
- [x] A tentativa temporária de deixar `0xE06D7363` atravessar o dispatcher
  encontrou handlers, o tipo `.?AW4RAR_EXIT@@` e um alvo de unwind, mas entrou
  em repetição até o timeout. O experimento foi removido e não alterou o
  comportamento oficial.
- [x] O compilador MinGW disponível localmente gera `__gxx_personality_seh0`
  para C++ com exceções; isso é a ABI GCC/SEH e não substitui uma fixture com
  metadados MSVC `__CxxFrameHandler*`. Não há `clang-cl` ou toolchain MSVC
  disponível para gerar essa fixture de forma válida neste ambiente.
- [x] O runtime mantém o escopo documentado: o legado
  `__CxxFrameHandler` não interpreta `FuncInfo`, `_CxxThrowException` não
  fabrica estado de exceção e `__CxxFrameHandler3` só aceita o subconjunto
  checked de `catch(...)` e captura tipada exata descrito abaixo. Exceções C++
  fora desse contrato
  seguem para o encerramento controlado; não há mais um `ignored` global para
  `0xE06D7363`.
- [x] Após o registro deste marco, a validação foi reiniciada nos dois
  backends: Rust ON e C++ OFF passaram `report`, análise recursiva, execução
  nativa e instalação, com 4/4 testes em cada build. A repetição não alterou
  os exits nem criou arquivos residuais no corpus/prefixos temporários.
- [x] O diagnóstico genérico `seh` passou a registrar os RVAs do handler, dos
  dados do handler e o índice da função; a fixture `tl_seh` valida esses
  campos nos builds Rust ON e C++ OFF. Nenhum metadado é dereferenciado por
  causa desse diagnóstico.
- [x] Foi criada a fixture genérica `tl_cxx_eh` pelo backend WinEH do LLVM,
  com `__CxxFrameHandler3`, `FuncInfo`, mapas de `try/catch` e funclet de
  captura. Após o primeiro bloco do handler, os testes de metadados e captura
  passam nos dois builds e terminam em `ExitProcess(0)`.
- [x] O contrato mínimo foi documentado: `FuncInfo` v3 relativo à imagem,
  mapas checked e limitados, catch-all sem RTTI e trampoline de `catchret` com
  validação do alvo. A transferência não usa mais o antigo no-op global para
  `0xE06D7363`.
- [x] A fixture genérica `tl_cxx_eh_cleanup` comprova nos builds Rust ON e
  C++ OFF que a ação do `stateUnwindMap` executa o destrutor antes do
  `catchret`, preservando o marcador e terminando com `ExitProcess(0)`. A ponte
  revalida o slot de retorno do catch depois da chamada host para não deixar a
  pilha convidada corromper a continuação.
- [x] A fixture genérica `tl_cxx_eh_cleanup_chain` comprova nos builds Rust ON
  e C++ OFF uma cadeia de dois cleanups do `stateUnwindMap`. O runtime limita a
  cadeia a 64 estados, rejeita ciclos e só entrega o catch depois de ambos os
  funclets retornarem com seus marcadores preservados.
- [x] A fixture genérica `tl_cxx_eh_typed` comprova nos builds Rust ON e C++
  OFF a correspondência exata entre `ThrowInfo`/`CatchableTypeArray` e o
  `type descriptor` do handler. A captura é selecionada sem fallback para
  `catch(...)`; conversões, herança e ajustes de objeto permanecem fora do
  contrato.
- [x] Os testes `fixture_tl_cxx_eh_typed_metadata` e
  `runtime_tl_cxx_eh_typed` passaram 2/2 nos builds Rust ON e C++ OFF; o trace
  registra `cxx-eh detail="catch-typed"` e a continuação termina em
  `ExitProcess(0)`.

Próximo bloco de trabalho, ainda aberto:

- [x] Obter ou gerar uma fixture PE32+ mínima e redistribuível com a ABI MSVC,
  contendo `FuncInfo`, `try/catch`, unwind de término e pelo menos um destrutor;
  uma fixture MinGW não atende este contrato.
- [x] Documentar o layout aceito de `DISPATCHER_CONTEXT`, `FuncInfo`, mapas de
  unwind/try e a regra de catch-all sem informação de tipo.
- [x] Implementar validação checked e limites para esse subconjunto, com busca
  controlada para versões, ponteiros, ranges e disposições desconhecidos.
- [x] Adicionar testes genéricos ON/OFF para o primeiro caso de unwind de
  término e destrutor, com metadata e execução da fixture `tl_cxx_eh_cleanup`.
- [x] Adicionar testes para a rejeição controlada de reentrada durante um
  `catch` (fronteira atual para rethrow), exceção não tratada e ausência de
  mapeamento posterior a uma rejeição. As fixtures `tl_cxx_eh_nested` e
  `tl_cxx_eh_unhandled` passam 4/4 em cada backend; o teste
  `integration_rust_app_run_malformed` passa ON/OFF e confirma ausência de
  `mapped`/execução após a rejeição. O rethrow nativo completo da ABI MSVC
  continua fora do contrato.

Critério de saída:

- [x] A fixture genérica de captura passa nos builds Rust ON e C++ OFF sem
  código ou nomes de aplicativo no runtime.
- [x] O corpus existente não apresentou regressão após a captura tipada: as
  quatro matrizes `popular_apps_*_matrix` passaram nos dois backends, cobrindo
  report, report recursivo, execução nativa e instalação, com timeout, memória,
  prefixos temporários e comparação de trace/exit code preservados.
- [x] A promoção não relaxa W^X, não converte sinais Linux em exceções
  convidadas e não usa o parser C++ como fallback de produção. A validação final
  de 2026-09-10 passou nos dois backends: `ImageMapperTest.DowngradesWritableExecutableSectionToReadWrite`,
  `runtime_tl_crash_guest_signal`, `app_run_tl_crash_guest_signal`,
  `CommandRunTest.DirectRustReportPreservesStructuredParseFailure`,
  `CommandRunTest.AppRunReportRemainsOnCppParserPath`,
  `RustAppCatalogParserTest.PromotedLoadRejectsCppPermissiveInputAtomically` e
  `integration_rust_app_run_malformed`, sem fallback ou mapeamento após rejeição.

### E43 — Rodada controlada de CPU-Z, GPU-Z, HWMonitor e HWiNFO

Objetivo: analisar os novos aplicativos do corpus com o Tradutor, confirmar
que cada solicitação recebe uma classificação explícita e impedir que uma
tentativa de instalação rejeitada deixe staging parcial.

Problema reproduzido em 2026-09-08:

- [x] `--report` Rust ON/C++ OFF identificou CPU-Z, GPU-Z e HWMonitor como
  `PE32`/`machine=0x14c`, retornando `Unsupported`/exit `5`; nenhum deles foi
  executado ou cadastrado. HWiNFO64 foi identificado como PE32+ empacotado,
  retornando `Malformed`/exit `4` por RVA sem faixa crua.
- [x] A primeira tentativa controlada de `install` preservou o exit correto,
  mas o fallback genérico de arquivo `7z` deixava 31, 386, 31 e 1338 arquivos
  no diretório de instalação após CPU-Z, GPU-Z, HWMonitor e HWiNFO,
  respectivamente. Não houve cadastro, mas a rejeição não era transacional.

Correção e evidência:

- [x] O fallback de instaladores continua disponível para encontrar um payload
  PE32+ válido, mas remove o diretório de staging criado pela própria tentativa
  quando nenhum executável é registrado. Prefixos preexistentes não são
  removidos.
- [x] `popular_apps_install_matrix` foi ampliada de 4 para 8 casos e passou
  8/8 em `build/debug-rust` e 8/8 em `build/debug`; os quatro novos casos
  retornaram `5, 5, 5 e 4` e terminaram com zero arquivos residuais.
- [x] Probes diretos ON/OFF confirmaram CPU-Z/GPU-Z/HWMonitor exit `5`, HWiNFO
  exit `4` e `residual-files=0`. As matrizes `--report` simples e recursiva
  também passaram em ambos os backends (27/27 e 64/64, respectivamente).
- [x] Nenhuma DLL, shim, regra por aplicativo ou etapa PE32/x86 foi adicionada;
  a execução permanece não tentada para arquiteturas fora do escopo e o
  desempacotamento genérico de HWiNFO continua uma limitação publicada.

### E44 — Contrato de `RtlUnwindEx` e consolidação SEH x64

Objetivo: fechar a lacuna genérica observada no unwind explícito de imagens
PE32+ AMD64, sem introduzir regras ou shims para o WinRAR. O contrato cobre a
captura do chamador na fronteira Microsoft x64, a chamada dos `UHANDLER` e a
consolidação por callback.

Evidência reproduzível de 2026-09-10:

- [x] `RtlUnwindEx` passou a ter entrada assembly própria: captura o contexto
  do chamador antes da ponte System V, recupera os cinco argumentos Microsoft
  x64 e delega ao mesmo núcleo de unwind; `RtlUnwind` continua usando o caminho
  equivalente já existente.
- [x] O unwind explícito chama cada `UHANDLER` encontrado até o frame-alvo,
  aplica `EXCEPTION_UNWINDING`/`EXCEPTION_TARGET_UNWIND`, preserva o valor de
  retorno em `RAX` e valida os destinos antes do trampoline de restauração.
- [x] O registro `STATUS_UNWIND_CONSOLIDATE` (`0x80000029`) chama o callback
  convidado indicado pelo primeiro parâmetro e usa somente o RIP retornado
  depois da validação de pertencimento à imagem.
- [x] `tl_seh.exe` foi ampliada como fixture genérica para exigir `UHANDLER`,
  `RtlUnwindEx`, `RAX` e o callback de consolidação; os quatro testes SEH
  correspondentes passaram no build Debug, assim como os 468 testes unitários
  (um skip ambiental de interfaces de rede).
- [x] O teste de caminho estendido `\\?\C:\...` agora remove o prefixo sem
  escapar do drive virtual e rejeita UNC estendido; os casos de prefixo
  passaram junto com a suíte unitária.
- [x] O smoke oficial de cancelamento do WinRAR continuou passando com
  `ExitProcess(0)` sob Xvfb. A interação `Return` foi revalidada separadamente:
  chega ao cleanup/unwind convidado, mas ainda termina com `guest-signal`/exit
  `71`; isso não é promovido a suporte de extração.

- [x] A fixture PE32+ genérica `tl_seh.exe` (e sua variante promovida `tl_seh_v2.exe`)
  foi expandida para modelar e validar a caminhada de unwind através de múltiplos
  frames com cleanups intermediários: `seh_unwind_probe` (frame alvo) chama
  `seh_unwind_intermediate` (com handler `@unwind`), que chama `seh_unwind_inner`
  para acionar `RtlUnwindEx`. A suíte CTest valida que o handler intermediário é
  acionado com `EXCEPTION_UNWINDING`, que o handler alvo recebe `EXCEPTION_TARGET_UNWIND`,
  que o callback de consolidação é invocado e que o valor em RAX é preservado na
  restauração do contexto.

### E45 — Rejeição segura de handlers SEH estáticos durante exceções C++

Objetivo: corrigir uma regressão genérica revelada pela repetição do smoke do
Notepad++ após E44, sem transformar o aplicativo em alvo especial nem ampliar
o contrato C++ além do `FuncInfo` v3 validado.

Problema reproduzido em 2026-09-11:

- [x] O smoke sob Xvfb criava `Configurator` e `Load stylers.xml failed`, mas
  o dispatcher tratava o `handler-data` de um handler SEH estático do convidado
  como se fosse metadata `FuncInfo` C++. A transferência de cleanup corrompia
  o contexto e terminava em `guest-signal`/SIGSEGV dentro de `.text`.
- [x] A falha era específica da classificação incorreta de metadata: os
  handlers estáticos usam uma tabela de escopos diferente de `FuncInfo` v3.

Correção e evidência:

- [x] Foi adicionada uma validação genérica de `handler-data` que aceita apenas
  `FuncInfo` v3 com mapas de unwind e IP válidos para a imagem PE ativa.
  Para `0xE06D7363`, metadata estática ou desconhecida é registrada como
  `unsupported-cxx-handler-during-search`/`...-during-unwind` e não passa pela
  ponte C++ de cleanup/catch.
- [x] A regressão unitária
  `UnwindTest.DistinguishesCxxFuncInfoFromStaticSehHandlerData` cobre as duas
  representações; as fixtures C++ existentes continuam exercitando o caminho
  `FuncInfo` validado.
- [x] O smoke do Notepad++ agora termina deterministicamente com
  `ExitProcess(3)`, confirma `Configurator`/`stylers.xml`, não registra
  `guest-signal` nem `guest-timeout`, e passou no Rust ON e no C++ OFF.
- [x] Nenhuma DLL, shim, regra de aplicativo ou tratamento especial foi
  adicionado. O resultado é rejeição controlada, não suporte ao Notepad++.

- [x] O modelo de unwind através de múltiplos frames com cleanups intermediários
  foi validado pela fixture genérica `tl_seh.exe`/`tl_seh_v2.exe`, confirmando a
  execução de handlers intermediários e a consolidação no frame alvo.

### E46 — Limite explícito da stack convidada no unwind

Objetivo: eliminar a leitura fora da alocação observada quando a validação de
sanitizers repetiu o bloqueio SEH do Notepad++, mantendo o unwind incapaz de
tratar memória do hospedeiro como stack Win32.

Problema reproduzido em 2026-09-11:

- [x] O preset `sanitize` encontrou `heap-buffer-overflow` em
  `unwind.cpp:149`: com `TEB.StackLimit=0`, a validação por mapas Linux aceitava
  uma área de heap do hospedeiro durante a caminhada de frames folha.

Correção e evidência:

- [x] `initialize_guest_teb` agora preserva `StackLimit`; a validação interna
  de leituras, escritas e slots de retorno exige a faixa
  `[StackLimit, StackBase)` da thread convidada ativa antes de consultar os
  mapas de memória do host.
- [x] A regressão do TEB verifica os dois limites; quando a busca chega ao fim
  da stack convidada, ela segue para `UnhandledExceptionFilter` sem acessar
  frames do host. O smoke do Notepad++ no preset `sanitize` confirma a
  rejeição controlada sem acesso fora da stack, e os testes unitários e smokes
  Debug ON/OFF permanecem protegidos.
- [x] Nenhuma DLL, shim, regra por aplicativo ou conversão de sinal foi
  adicionada; o limite vale para qualquer imagem PE32+.

Validação pendente do ambiente:

- [x] O aviso preexistente de `-Werror=conversion` em `tests/test_win32.cpp:475` foi
  corrigido com cast explícito sobre o resultado total da expressão inteira,
  mantendo a decodificação UTF-16 válida e a compilação limpa sob avisos estritos.
## Rodada F — aprofundamento dos alvos x64 com maior progresso

Esta rodada começa depois da conclusão de E46 e do ciclo de melhorias de
infraestrutura (CLI JSON, doctor, sandbox S1, B11, B13). O objetivo é avançar
o portfólio real com as lacunas de execução mais próximas de ser fechadas,
começando pelos alvos que já têm todos os imports resolvidos e chegam ao entry
point sem rejeição.

### Estado do corpus — 2026-09-12

| Aplicativo | --report | Execução atual | Próximo bloqueio |
|---|---|---|---|
| `7z_x64.exe` | `supported` (562 KB, 225 imports) | exit `0` (CLI sem args) | – fluxo completo já coberto |
| `7zG.exe` | `supported` (PE32+; 208 imports) | operação `a` cria arquivo 7z e termina com exit `0` sob `--timeout 30 --memory 512` sem limite artificial de CPU; timeout curto/`--cpu 10` ainda é intermitente | trace F14 mostra worker/eventos concluídos no caso que falha; investigar entrega modal de `WM_TIMER` antes de ampliar cenários |
| `7zFM_x64.exe` | `supported` (987 KB, 298 imports) | `seven_zip_smoke` exit `0` | GUI estendida fora do smoke |
| `WinRAR_x64.exe` | `supported` (3,8 MB, ~251 imports) | exit `0` (extração real e cancelamento SFX) | – nos cenários já cobertos |
| `putty_x64.exe` | `supported` (1,7 MB, 348 imports) | `guest-timeout 72` | bloqueio pós-ativação TCP |
| `notepad++.exe` | `supported` (8,4 MB, ~400 imports) | `ExitProcess(3)` após bloqueio C++/SEH controlado | exceção C++/SEH fora do `FuncInfo` v3; `WinVerifyTrust` para arquivo ainda é um gate posterior |
| `Rockstar-Games-Launcher.exe` | `supported` (112 MB) | `ExitProcess(3)` | antidetecção de ambiente |
| `HWiNFO64.exe` | `malformed` (UPX0/UPX1) | não tentada | imagem empacotada UPX |
| `Rufus_x64.exe` | `malformed` (entry W^X) | não tentada | imagem empacotada, W^X |

Aplicativos x86/32-bit (`CPU-Z`, `GPU-Z`, `HWMonitor`, `RTSS`,
`Everything_Search_x64.exe`) continuam fora do escopo.

### F1 concluído — WinRAR: extração real de arquivo (2026-09-11)

Objetivo: avançar o fluxo do WinRAR além do cancelamento controlado, validando
a extração real de arquivos num prefixo temporário. Sem introduzir regras ou
shims específicos do WinRAR no runtime.

Evidência reproduzível:

- [x] O smoke automatizado `tests/apps/winrar/winrar_extract_smoke.cpp` executa
  `WinRAR_x64.exe` com `-s -dZ:\<prefix>\dest\` sob Xvfb próprio e limites
  padronizados (`--timeout 15 --cpu 10 --memory 1024`).
- [x] A extração completa dos 28 arquivos (incluindo `WinRAR.exe`, `Rar.exe`,
  `UnRAR.exe`, `License.txt`, DLLs e documentação) foi verificada no diretório
  de destino isolado, conferindo tamanhos e o cabeçalho textual
  `END USER LICENSE AGREEMENT` em `License.txt`.
- [x] A execução termina com `ExitProcess symbol="ExitProcess" exit-code="0"` e
  `exit exit-code="0"` sem timeout, crash ou sinal.
- [x] O teste `winrar_extract_real_smoke` foi adicionado à suíte CTest com
  suporte a skip controlado (77) quando o binário ou Xvfb não estiver disponível.
- [x] Nenhum código específico de WinRAR foi introduzido no runtime genérico.

### F2 concluído — Notepad++: diagnóstico e correção do stylers.xml (2026-09-11)

Objetivo: isolar a causa do `ExitProcess(3)` após `Load stylers.xml failed`
sem relaxar a política de segurança.

Diagnóstico e Evidência reproduzível:

- [x] **Causa isolada**: `src/runtime/shlwapi.cpp` possuía uma função
  `normalize_win_path` ad-hoc que convertia qualquer caminho com letra de
  unidade `Z:\...` para `./...` (relativo ao diretório corrente) em vez de
  utilizar o resolvedor canônico `translate_windows_path`. Com isso,
  `PathFileExistsW` retornava `0` para `stylers.xml`, gerando o diálogo
  de erro e impedindo o avanço da aplicação.
- [x] **Correção genérica**: `tl_PathFileExistsA/W` e `tl_PathIsDirectoryA/W` em
  `src/runtime/shlwapi.cpp` foram atualizadas para utilizar `translate_windows_path`
  e `normalized_wide_path`.
- [x] **Validação unitária**: `TEST(ShellPathTest, PathFileExistsAndIsDirectoryWithZDrive)`
  adicionado em `tests/test_win32_external.cpp`, cobrindo caminhos `Z:\`, arquivos
  existentes e inexistentes com ANSI e UTF-16.
- [x] **Efeito no Notepad++**: o diálogo `Load stylers.xml failed` foi
  completamente eliminado; Notepad++ carrega `stylers.xml` com sucesso e avança
  para a validação de certificados de plugins e módulos (`WinVerifyTrust`).
- [x] O smoke `notepadpp_smoke` foi atualizado para verificar que
  `Load stylers.xml failed` não ocorre mais.

### F3 concluído — Matriz completa F e regressão (2026-09-11)

Objetivo: repetir a análise dos aplicativos x64 e verificar que os
progressos de E1–E46 e da Rodada F não introduziram regressões.

Evidência reproduzível:

- [x] **Matriz `--report` do corpus**: executada com `verify_popular_apps_report.cmake`
  cobrindo os 27 alvos do corpus. Resultado: **27/27 casos passaram** com 100% de
  paridade em Rust ON e C++ OFF.
- [x] **Matriz nativa do corpus**: executada com `verify_popular_apps_native.cmake`
  sob prefixos temporários isolados. Resultado: **5/5 casos passaram** com exit
  codes e marcadores diagnósticos idênticos em Rust ON e C++ OFF.
- [x] **Smokes GUI / CLI sob Xvfb**:
  - `seven_zip_cli_smoke`: ciclo completo (stored, deflate, 7z LZMA2, password, stdin/stdout, overwrite) concluído com exit `0` em Rust ON e C++ OFF.
  - `winrar_extract_smoke`: extração real dos 28 arquivos com verificação de integridade e licença concluída com exit `0` em Rust ON e C++ OFF.
  - `winrar_sfx_smoke`: inicialização gráfica e cancelamento SFX concluídos com exit `0` em Rust ON e C++ OFF.
  - `putty_smoke`: criação de diálogo de configuração concluída com exit `0` em Rust ON e C++ OFF.
  - `putty_ssh_smoke`: probe de configuração SSH concluído com limitação registrada e exit `0` em Rust ON e C++ OFF.
  - `notepadpp_smoke`: fecha `Configurator`, exige a ausência de `Load stylers.xml failed`, confirma a rejeição controlada de exceção C++ em módulo/plugin e o término em `ExitProcess(3)` sem sinal ou timeout em Rust ON e C++ OFF.
- [x] **Correção genérica de caminhos de shell**: `SHGetKnownFolderPath`, `SHGetFolderPathW`,
  `SHGetFolderPathAndSubDirW` e `SHGetPathFromIDListW` em `src/runtime/shell32.cpp`
  passaram a retornar caminhos no formato Windows canônico (`Z:\...` / `C:\...`)
  usando `prefix::to_windows_path`.
- [x] **Testes unitários**: `ShellPathTest.SHGetFolderPathWReturnsWindowsPath` adicionado
  e suíte de testes passando 100%.
- [x] `docs/compatibilidade.md` atualizado com o status final da Rodada F.
- [x] `git diff --check` aprovado e nenhum arquivo temporário versionado.

### F4 concluído — 7zG: `KERNEL32!lstrcatW` (2026-09-11)

Objetivo: remover uma lacuna genérica de strings wide observada ao analisar o
`7-Zip/7zG.exe`, sem introduzir regra específica para o aplicativo.

Evidência reproduzível:

- [x] Antes da mudança, o `--report` do `7zG.exe` retornava `5` com `207/208`
  imports resolvidos e classificava `KERNEL32.dll!lstrcatW` como
  `unknown-symbol`.
- [x] `lstrcatW` foi implementada no módulo genérico `KERNEL32.dll`, com
  strings UTF-16 guest, retorno do buffer de destino e validação do intervalo
  gravável da concatenação.
- [x] A fixture `tl_lstrcat.exe` cobre a ABI Microsoft x64, concatenação
  `C:\\` + `Temp`, saída, `--report` e exit `0`; o teste unitário wide também
  protege o resultado.
- [x] Depois da mudança, o `--report` real do `7zG.exe` retorna `0` e
  `208/208` imports resolvidos. Sob Xvfb, o aplicativo resolve `lstrcatW`,
  cria a janela `7-Zip` e termina somente por timeout controlado (`72`) sem
  interação.
- [x] A matriz recursiva do corpus foi atualizada de `7zG.exe|5` para
  `7zG.exe|0`; não houve DLL, shim ou regra específica de aplicativo.

### F5 concluído — 7zG: `PostMessageA/W` cross-thread (2026-09-11)

Objetivo: permitir que uma thread convidada secundária publique mensagens na
fila GUI do thread principal, contrato usado pela operação real de criação de
arquivo do `7zG.exe`, sem liberar o restante do estado USER32 para acesso
cross-thread.

Evidência reproduzível:

- [x] Antes da mudança, `7zG.exe a ...` criava a janela `Progress`, mas o
  trace registrava `api-failure` em `PostMessageA` por `thread-affinity`,
  produzia somente um arquivo de 32 bytes e terminava em `guest-timeout 72`.
- [x] `PostMessageA/W` agora valida o `HWND`, enfileira mensagens de threads
  secundárias em uma fila limitada a 4096 entradas e entrega a mensagem no
  thread principal por `GetMessageA/W` ou `PeekMessageA/W`; `lParam` permanece
  opaco e não há cópia arbitrária de payload.
- [x] A fixture genérica `tl_gui_cross_thread.exe`, sua manifestação e a
  unitária `Win32GuiTest.AllowsCrossThreadPostMessageToPrimaryQueue` cobrem
  criação de thread, post, `GetMessage` e `PeekMessage` sem regra de aplicativo.
- [x] Os quatro testes da fixture e os smokes `runtime_gui_smoke` e
  `seven_zip_gui_smoke` passaram sob Xvfb.
- [x] Depois da mudança, uma execução real de `7zG.exe a ...` terminou com exit
  `0`, criou um arquivo 7z válido e `7z_x64.exe l ...` executado pelo
  TradutorLinux confirmou a presença de `input.txt`. O tamanho do arquivo e da
  entrada dependem do caso de teste; essa evidência não fixa os valores
  históricos de 749/1596 bytes como contrato.
- [x] Nenhuma DLL, shim ou seleção específica do 7-Zip foi adicionada; as
  demais APIs stateful de USER32 continuam exigindo o thread principal.

### F6 concluído — USER32: `CreateDialogParamW` modeless (2026-09-12)

Objetivo: substituir o sentinela de `CreateDialogParamW` por criação real de
diálogo modeless no subconjunto USER32 já usado por aplicativos x64, sem
introduzir tratamento específico para Notepad++ ou PuTTY.

Evidência reproduzível:

- [x] `CreateDialogParamW` agora reutiliza o parser de `RT_DIALOG`, a criação
  X11/modelo lógico, controles padrão, `WM_INITDIALOG` e o ciclo de vida já
  exercitado por `CreateDialogParamA`; falhas de recurso, callback, thread ou
  janela retornam erro controlado em vez de um handle falso.
- [x] A fixture genérica `tl_dialog` cria e destrói primeiro um diálogo
  modeless wide, valida seu callback e depois executa o modal existente; o
  metadata passou e `runtime_gui_smoke` passou sob Xvfb fora do sandbox.
- [x] A unitária cobre a rejeição de template/callback inválidos e o report do
  Notepad++ continua com `CreateDialogParamW` resolvido em `supported`.
- [x] O smoke real do Notepad++ permaneceu fora da promoção: nesta execução o
  ambiente reproduziu o bloqueio C++/SEH antes de criar a janela esperada; isso
  não é usado como evidência de suporte funcional nem de que o aplicativo
  tenha exercitado a nova API.
- [x] Nenhuma DLL, shim, seleção por aplicativo ou regra exclusiva foi criada;
  o contrato é genérico para USER32 e continua limitado a templates padrão.

### F7 concluído — USER32: `DialogBoxParamA` numérico (2026-09-12)

Objetivo: retirar o retorno permissivo `IDOK` de `DialogBoxParamA` e alinhar o
contrato ANSI ao modal wide já validado, cobrindo o formato usado pelos
templates numéricos de aplicativos Win32 como PuTTY.

Evidência reproduzível:

- [x] `DialogBoxParamA` agora valida thread, nome nulo e `MAKEINTRESOURCE`,
  delega o template numérico para `DialogBoxParamW` e preserva falhas como
  `-1`; nomes textuais ANSI continuam explicitamente fora do subconjunto.
- [x] `tl_dialog` executa o mesmo recurso por `DialogBoxParamA` depois do
  modal wide e exige retorno 42; a unitária cobre callback inválido sem criar
  uma janela falsa.
- [x] O report do PuTTY continua resolvendo 348/348 imports. O trace GDB no
  fluxo sem interação observou `CreateDialogParamA`, mas não `DialogBoxParamA`
  antes do timeout; portanto esta melhoria não é declarada como correção do
  bloqueio SSH do PuTTY.
- [x] Nenhum tratamento específico de aplicativo foi adicionado; o wrapper é
  compartilhado por USER32 e limitado a recursos `RT_DIALOG` numéricos.

### F8 concluído — USER32: estado básico de menus (2026-09-12)

Objetivo: substituir os stubs de estado de menu identificados nos reports de
7zFM e Notepad++ por operações genéricas sobre o modelo `MenuItem::state`.

Implementação:

- [x] `EnableMenuItem` atualiza enabled/grayed/disabled e retorna o estado
  anterior; `CheckMenuItem` atualiza `MFS_CHECKED`; `CheckMenuRadioItem`
  marca um item dentro de uma faixa e desmarca os irmãos.
- [x] As três APIs aceitam `MF_BYPOSITION` quando aplicável, rejeitam handles
  e faixas inválidos de forma controlada e deixam `InsertMenuItemW`,
  `SetMenuItemInfoW`, `RemoveMenu` e `TrackPopupMenuEx` como limitações
  explícitas.
- [x] A unitária cobre seleção por comando/posição, retorno do estado anterior,
  rádio e handle inválido; a cobertura agregada de USER32 deixou de esperar
  sucesso falso dos stubs.
- [x] Os reports reais de 7zFM e Notepad++ resolvem as três APIs como
  `support=limited`; o report continua com 100% dos imports resolvidos.
- [x] `seven_zip_gui_smoke` passou com a cópia interativa real em 2,72 s;
  `putty_real_gui_smoke` e `putty_ssh_local_probe` continuam passando com o
  mesmo resultado de configuração e limitação SSH documentado.
- [x] A unitária focada passou nos builds Rust ON e sanitize, com
  `ASAN_OPTIONS=detect_leaks=0` por causa da restrição de LeakSanitizer sob
  ptrace; `git diff --check` passou.
- [x] Nenhuma chamada aos estados de menu foi observada durante a inicialização
  ou fechamento automático de PuTTY/7zFM; a implementação é uma extensão de
  contrato baseada nos imports reais e no modelo de menu já usado pelo smoke,
  não uma declaração de que esses fluxos passaram a usar a API.

### F9 concluído — smoke Notepad++ para bloqueio pré-GUI controlado (2026-09-12)

Objetivo: alinhar o cenário de integração ao primeiro bloqueio reproduzível
atual do Notepad++, sem transformar uma falha controlada em suporte funcional.

Evidência reproduzível:

- [x] O import de `WinVerifyTrust` é resolvido, mas o trace atual não mostra uma
  chamada à API: o processo lança a exceção C++ `0xE06D7363` durante a
  verificação inicial, registra `unsupported-cxx-handler-during-search` e
  termina em `ExitProcess(3)`; nesta execução pode terminar antes de criar
  `Configurator`.
- [x] `notepadpp_smoke` agora aceita somente os dois caminhos observados:
  fechar `Configurator` quando ela existe, ou detectar o bloqueio C++/SEH
  direto; ambos exigem ausência de `Load stylers.xml failed`, sinal e timeout.
- [x] O smoke continua classificando o aplicativo como não suportado e não
  adiciona fallback para `WinVerifyTrust`, CMS, Authenticode ou exceções C++.

### F10 concluído — USER32: inserção de itens de menu observada no 7-Zip (2026-09-12)

O report estático do 7zFM listava `InsertMenuItemW` como stub, e a sondagem
dinâmica com GDB confirmou chamadas reais durante a montagem do menu de classe:
`item=0`, `fByPosition=1`, máscara `0x17`, ID `540`, tipo textual e estrutura
`MENUITEMINFOW` x64 de 80 bytes. Após a implementação, uma nova sondagem não
observou `SetMenuItemInfoW`, `RemoveMenu` ou `TrackPopupMenuEx` nesse fluxo.

- [x] `InsertMenuItemW` agora valida thread GUI, handle, `cbSize`, ponteiros e
  máscaras; aceita estado, tipo, ID, submenu e texto UTF-16, rejeitando
  bitmaps/dados arbitrários com erro controlado.
- [x] A inserção funciona por posição ou ID, mantém `MenuItem` e o vetor de
  itens do popup sincronizados e emite trace de sucesso; o export passou de
  `stub` para `limited`.
- [x] A unitária cobre máscara Microsoft, ID, texto e sincronização visual; o
  report de 7zFM/Notepad++ continua com todos os imports resolvidos.
- [x] O smoke real do 7-Zip foi revalidado sob Xvfb após a implementação e
  retornou `0`, confirmando cópia e encerramento limpo.
- [x] `SetMenuItemInfoW`, `RemoveMenu` e `TrackPopupMenuEx` permanecem stubs
  até outro fluxo real justificar uma promoção em etapas separadas.

### F11 concluído — USER32: clique secundário normal versus bandeja (2026-09-12)

O backend X11 já capturava o botão 3, mas o message loop tratava qualquer
`RightPress` como callback de bandeja. Isso impedia que uma janela normal
recebesse `WM_RBUTTONDOWN`/`WM_RBUTTONUP` e também tornava impossível distinguir
um clique real no 7-Zip de um clique no surrogate do Simple Todo.

- [x] X11 e Wayland agora preservam pressão e soltura do botão secundário;
  `GetMessageA`/`GetMessageW` entrega as duas mensagens Win32 para janelas
  sem registro de bandeja.
- [x] `Shell_NotifyIconA/W` valida o prefixo x64 de `NOTIFYICONDATA`, rastreia
  `NIM_ADD`/`NIM_MODIFY`/`NIM_DELETE` por `HWND` e mantém o callback lógico da
  bandeja somente para a janela que o registrou.
- [x] A unitária cobre registro, alteração e remoção nos wrappers ANSI/Wide;
  o teste Sanitizer focalizado passou sem leaks habilitados.
- [x] O smoke real do 7-Zip continuou retornando `0`. Em sondagem GDB no
  mesmo Xvfb, um clique secundário em `7-Zip` alcançou `DispatchMessageW` com
  `WM_RBUTTONDOWN` (`0x0204`). O fluxo não alcançou `TrackPopupMenuEx`, que
  continua stub até haver uma chamada real observada.
- [x] O smoke Simple Todo foi alinhado ao contrato de prefixos do runtime: a
  fixture legada recebe `TL_PREFIX` temporário, o artefato persistente é lido
  de `drive_c/users/guest/AppData/Roaming`, e o driver X11 separa cliques
  normais da seleção de popup. A execução end-to-end passou em Debug e cobriu
  adicionar, editar, buscar, concluir, esconder, mostrar, persistir em uma
  segunda execução, excluir e sair pela bandeja, sem alterar o runtime por
  aplicativo.

### F12 concluído — matriz nativa: rejeição pré-entry do WinGup (2026-09-12)

O componente `Notepad++/updater/GUP.exe` é PE32+ x64, mas a execução direta
encontra o `libcurl.dll` local e rejeita a dependência `WLDAP32.dll!ordinal(46)`
antes de qualquer entry point. A matriz nativa passou a proteger esse resultado
controlado junto dos cenários diretos já autorizados.

- [x] O `--report` registra 149/153 imports resolvidos; os quatro imports do
  executável pertencem a `libcurl.dll`, cuja cadeia também possui 18 ordinais
  não registrados de `WLDAP32.dll`, além de lacunas independentes de criptografia
  e normalização.
- [x] O cenário direto retorna `5` nos builds Rust ON e C++ OFF, contém o
  marcador `provider-rejected` com `WLDAP32.dll!ordinal(46)`, desmonta a imagem
  principal e não registra `ExitProcess`, sinal ou timeout convidado.
- [x] `popular_apps_native_matrix` passou de 5/5 para 6/6 nos dois backends,
  sem adicionar DLL, shim ou regra específica ao runtime.

### F13 concluído — Notepad++: ordenar o bloqueio C++/SEH antes do WinTrust (2026-09-12)

Uma execução direta controlada sob Xvfb foi repetida nos builds Rust ON e C++
OFF para confirmar qual API é realmente alcançada pelo Notepad++ sem
interação.

- [x] Os dois builds retornaram `3`, resolveram o import
  `WINTRUST.dll!WinVerifyTrust` e registraram o mesmo `cxx-throw` com a
  mensagem `Checking certificate ...`.
- [x] Nenhuma chamada `wintrust` foi registrada antes do bloqueio; o dispatcher
  encontrou três `handler-data` que não satisfazem o contrato `FuncInfo` v3,
  registrou `unsupported-cxx-handler-during-search` com índice da função e
  RVAs do handler/metadata e transferiu para `ExitProcess(3)`, sem
  `guest-signal` ou `guest-timeout`.
- [x] A sondagem GDB confirmou que os dados rejeitados são tabelas relativas
  sem o magic `0x19930522` ou uma tabela SEH estática com contagem de escopos.
  Nenhum desses formatos foi interpretado como C++ por tentativa.
- [x] A matriz e o smoke continuam classificando o aplicativo como não
  suportado. `WTD_CHOICE_FILE`/Authenticode permanece um gate posterior, caso
  o suporte C++/SEH avance; não foi criado fallback criptográfico.

### F14 concluído — Diagnóstico de threads e eventos para o `7zG` (2026-09-12)

O fluxo `7zG.exe a` alterna entre concluir e permanecer em uma espera modal,
sem que o trace anterior permitisse distinguir worker não criado de evento não
sinalizado. A tentativa de inserir `yield` no runtime não alterou a falha e
foi descartada.

- [x] O runtime agora registra genericamente `thread-create`, `thread-start`,
  `thread-exit`, `event-create`, `event-set`, `wait-single-begin` e
  `wait-single-end`, incluindo thread convidada, tipo de handle e timeout.
- [x] A fixture `tl_thread` exige o ciclo de thread e espera no trace, sem
  depender do 7-Zip nem criar regra por aplicativo.
- [x] O contrato foi documentado em `docs/diagnostico.md`; um início de espera
  sem o término correspondente permanece uma evidência diagnóstica, não uma
  mudança de semântica.
- [x] O cenário foi repetido com `--trace=loader,process,runtime,gui`: no
  caso que termina em `guest-timeout 72`, o worker `thread-id=2` é criado,
  iniciado e encerrado; o thread principal sinaliza o primeiro evento, o
  worker sinaliza o segundo e a espera do primeiro termina com `result=0`.
- [x] Não houve retorno incorreto de `WaitForSingleObject`, falha de criação
  de thread ou espera sem término. O trace também não registra um segundo
  worker nem `WM_TIMER` antes do timeout; o próximo bloqueio é a entrega do
  diálogo modal, não a semântica básica de eventos.
- [x] Nenhuma correção especulativa de sincronização foi aplicada. A próxima
  etapa deve instrumentar ou corrigir genericamente a fila modal/timer do
  USER32 somente depois de uma fixture que reproduza esse contrato.

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

---

## `feitos/ROADMAP-RUST.md` (legado)

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

---

## `feitos/ROADMAP.md` (legado)

# Roadmap do TradutorLinux

Este arquivo acompanha a execução do projeto. O documento de visão, escopo e arquitetura está em [PROJETO.md](../PROJETO.md).

## Como usar este roadmap

Cada fase só deve avançar quando seus critérios de saída estiverem atendidos. Uma API nova entra no projeto apenas quando existir um teste ou aplicativo-alvo que justifique seu comportamento.

Os itens marcados como concluídos devem ter evidência no repositório: código, teste, documentação ou um artefato reproduzível. O roadmap descreve ordem de dependências, não uma promessa de prazo.

Este era o backlog normativo da rodada histórica. `PROXIMAS-ETAPAS.md`, os
registros de análise, `ideia.md` e as propostas em `docs/` permanecem apenas
como referências históricas; as próximas tarefas agora são registradas no
`ROADMAP.md` da raiz. O backlog consolidado ao final deste documento usa IDs
`B1`, `B2` etc. para preservar as referências das fases históricas.

## Stack decidido

- **Linguagem principal:** C++20.
- **C:** estruturas PE, interfaces C e trechos que precisem de ABI simples.
- **Assembly x86-64:** somente trampolins, bootstrap ou outras fronteiras que não possam ser expressas com segurança pelo compilador.
- **Build:** CMake + Ninja.
- **Hospedeiro inicial:** Linux x86-64.
- **Binários de teste:** PE32+ x86-64 produzidos com `mingw-w64`.

## Estado atual

- **Fase atual:** Fase 13 — compatibilidade ampla por portfólio.
- **Próximo ciclo:** B2 fica estacionada até uma decisão de produto específica
  sobre tradução de interface; B6 e B9 continuam condicionadas a evidência
  externa. B1, B8, B10, B12, B15 e B16 deste ciclo foram concluídas.
- **Decisões vigentes:** a triagem dos itens condicionais está registrada em
  [Decisões registradas](#decisões-registradas--2026-09-05); nenhuma API ou
  capacidade será ampliada apenas para eliminar uma caixa desmarcada.
- **Último incremento:** a Fase 13.14 concluiu TLS genérico e a fixture
  reutilizável Worker/RSL. O caso comercial do Roblox continua como benchmark:
  imports resolvidos, mas execução interrompida em `RBXCRASH`/`ExitProcess 3`.
- **Última etapa funcional:** B5/B7 concluíram um fluxo principal restrito do
  7zFM 24.08. O smoke externo versionado seleciona `input.txt`, aciona
  `Copy` (`546`), verifica a cópia dentro da raiz e encerra o runtime com exit
  `0`; a execução direta sem interação continua sujeita a timeout, portanto o
  alvo não é declarado de uso diário nem como suporte geral.
- **Última etapa de infraestrutura:** B12 passou a rastrear regiões privadas de
  memória como `RESERVE`/`COMMIT`, proteções e divisão por página após
  `VirtualProtect`; a fixture `tl_virtual_query.exe` valida o contrato
  completo. B1 continua cobrindo limites opcionais de CPU/RAM e herança POSIX
  para `CreateProcessW`.
- **Última validação de recursos:** B10 passou no `x11_popup_smoke` do build
  `sanitize`, executado fora de `ptrace` com Xvfb próprio e
  `detect_leaks=1`. O smoke repetiu 512 desenhos de cores e cobriu Escape,
  clique externo, destruição externa e timeout, sem relatório de ASan/LSan.
- **Última validação de distribuição:** B8 instalou a fixture reproduzível
  `native-fixture.msix` em prefixo exclusivo, extraiu o PE32+ x86-64 declarado
  no `AppxManifest.xml`, cadastrou-o no catálogo e confirmou `app run` com
  stdout e exit code esperados. A extração valida central directory, CRC,
  DEFLATE, limites, traversal, symlinks e colisões; bundles, .NET/Mono e
  assinaturas Authenticode continuam fora do contrato.

Os demais bullets desta seção são registro cronológico de marcos já entregues;
para decidir o próximo trabalho, use somente a ordem do backlog abaixo.
- **Marco concluído:** a Fase 7 foi validada de ponta a ponta e a decisão de produto foi tomada: **seguir com a GUI Win32 mínima como objetivo experimental**. `tl_gui.exe` abriu a janela X11, recebeu o clique em OK e encerrou com código `0`; `tl_win.exe` criou uma janela real e executou um message loop completo (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `GetMessageA`, `DispatchMessageA`, `DefWindowProcA`, `PostQuitMessage`), encerrando via `WM_CLOSE`/autoclose com código `0`; o modo `--report` lista imports suportados sem executar o PE; `tl_hello`, `tl_echo` e `tl_file` têm regressões e limitações publicadas na matriz.
- **Marco concluído:** o smoke test de GUI passou a ter cobertura automática em CI. O teste `runtime_gui_smoke` sobe um `Xvfb` próprio e executa `tl_win.exe`, `tl_win2.exe`, `tl_key.exe`, `tl_timer.exe`, `tl_gdi.exe`, `tl_paint.exe` e `tl_dialog.exe` de ponta a ponta, cobrindo message loop, `WM_DELETE_WINDOW`, teclado, duas janelas, timers, pintura e diálogo modal. O `x11_popup_smoke` cobre Escape, clique externo, destruição externa e timeout; a conexão X11 do runtime é fechada no teardown (`DisplayCloser`). A validação Debug desta retomada passou nos dois smokes.
- **Marco concluído:** `CreateWindowExA` agora despacha `WM_CREATE` ao `WNDPROC` do convidado antes de devolver o `HWND` (retorno `-1` aborta a criação e devolve `NULL`). A fixture `tl_win.c` marca uma flag no `WM_CREATE` e propaga no exit code via `PostQuitMessage`, então o `runtime_gui_smoke` prova o despacho exigindo exit-code `1`.
- **Marco concluído:** o message loop ganhou entrada real de teclado. `KeyPress` X11 vira `WM_KEYDOWN` (virtual key: letras em maiúsculas) com o caractere guardado; `TranslateMessage` converte em `WM_CHAR` enfileirado (entregue antes dos próximos eventos X11) e registra o evento de trace `TranslateMessage message="WM_CHAR" wparam status="translated"`. A fixture `tl_win.c` encerra a janela ao receber `WM_CHAR('q')`, e o terceiro cenário do `runtime_gui_smoke` envia um `KeyPress` sintético sob `Xvfb` e exige exit-code `3`. O teste agora sobe sempre um `Xvfb` próprio (sem window manager): com WM a janela é reparentada e o `XSendEvent` para o frame não chega ao cliente.
- **Marco concluído:** o pump passou a usar fila de eventos por janela. Todos os eventos X11 pendentes são demultiplexados para a fila da janela-alvo a cada consulta, então nada se perde entre janelas independentemente da ordem do message loop. A fixture `tl_win2.exe` cria duas janelas simultâneas com `WNDPROC`s independentes ("Janela A" e "Janela B"); o quarto cenário do `runtime_gui_smoke` envia `KeyPress 'q'` à A e `'k'` à B e exige exit-code `15` (flags 1+2+4+8), provando o roteamento independente por janela.
- **Marco concluído:** diagnóstico de falhas com isolamento em processo filho (ideia §1). O convidado agora executa em um processo filho (`fork`/`waitpid`); o pai prepara o PE, o mapeamento e os imports, e o filho só executa o entry point e reporta o resultado por um pipe antes de `_exit`. Sinais fatais do filho são restaurados para `SIG_DFL` para que um `SIGSEGV` do convidado não seja engolido por handlers do hospedeiro (ex.: AddressSanitizer). O pai distingue saída normal de término por sinal: no término por sinal emite `[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória"` e retorna `71` (`GuestFault`), novo código de saída do hospedeiro. A fixture `tl_crash.exe` (sem imports) acessa o endereço `0` e valida o diagnóstico no preset `sanitize` (onde o ASan interceptaria o sinal sem o reset), no `debug` e no `release`; o exit code do convidado continua propagado integralmente pelo pipe (não truncado pelo status POSIX).
- **Marco concluído:** a Fase 8 começou com a definição dos primeiros aplicativos-alvo reais, de código aberto e compilados em CI: `xxd` (vim `v9.2.0957`), `bzip2` (`1.0.8`) e `dos2unix`/`unix2dos` (`7.5.6`). O módulo `tests/targets` baixa as fontes pinadas por hash SHA-256, faz o cross-build com `mingw-w64` (opção `TL_BUILD_TARGET_APPS=ON`, job `target-apps` do CI) e protege os imports reais em manifests via `llvm-readobj` e `--report` (8 testes, label `targetapp`). Nenhum alvo executava ainda: todos importam `msvcrt.dll` (fora de escopo até a Fase 9) e o `--report` os classificava como `result: unsupported` / `execution: not-attempted`. Os imports capturados (xxd: 73 símbolos; bzip2: 69; dos2unix/unix2dos: 88, incluindo `SHELL32.dll!CommandLineToArgvW`) guiam o subconjunto mínimo de CRT da Fase 9.
- **Marco concluído:** o subconjunto mínimo de `msvcrt.dll` foi implementado e registrado (57 símbolos, ordinais 1–57), junto com as 14 APIs de `KERNEL32.dll` que o CRT interno do mingw e o `xxd.exe` exigem (`VirtualQuery`/`VirtualProtect` via `/proc/self/maps` + `mprotect`, `MultiByteToWideChar`/`WideCharToMultiByte` com CP 0/1252/65001, critical sections no-op para convidado single-thread, `TlsGetValue`, `GetConsoleMode`/`SetConsoleMode`, `Sleep`, `SetUnhandledExceptionFilter`, `IsDBCSLeadByteEx`). O `--report` do `xxd.exe` passou a `result: supported`.
- **Marco concluído:** `xxd.exe` executa de ponta a ponta com saída **byte-idêntica** ao `xxd` do sistema (exit `0`). Para isso a fronteira agora aloca um TEB de uma página e aponta o segmento `%gs` via `arch_prctl(ARCH_SET_GS)` durante a execução do convidado (o mingw lê `%gs:[0x30]` no `__mingw_CRTStartup`), restaurando o `GS` e liberando o TEB em seguida. O teste e2e fixa um ouro em `tests/targets/golden/xxd/` (entrada de 4880 bytes que cruza a coluna de offset em `0x1000`) e verifica em CTest: modo padrão, `-p` (plain) e caminho de erro (arquivo inexistente → exit `2`, stderr não vazio), 3 testes novos com label `targetapp`. As conversões de código de página, `VirtualQuery`/`VirtualProtect`, `TlsGetValue`, critical sections e o subconjunto de CRT têm 42 testes unitários novos (`test_win32.cpp`, `test_msvcrt.cpp`). Total: 180 testes verdes no preset com alvos; 169 em `debug`, `release` e `sanitize`.
- **Marco concluído:** `bzip2.exe` executa de ponta a ponta nos dois sentidos. `__iob_func()` devolve o ponteiro do array `GuestFile` (o convidado indexa `[0..2]` com `sizeof(_iobuf)` = 48 bytes — o argumento `rcx` é ignorado, como no CRT MSVC clássico) e `_stat64` passou de stub `ENOSYS` para implementação real que preenche o `struct _stat64` do MinGW (pack 8, `st_mode` em `0x06`, tamanho 56 bytes) via `stat()` do host; a verificação `S_ISREG` do bzip2 (`testb $0x40, +7`) depende dos bits de tipo do Linux, idênticos aos do Windows (`S_IFREG = 0x8000`). Compressão (`-c`) e descompressão (`-d`, `-d -c`) de arquivo são byte-idênticas ao `bzip2` nativo; e2e em CTest (ouro em `tests/targets/golden/bzip2/` e `golden/bzip2_decompress/`, comparação binária via `verify_target_run_bytes.cmake`), 2 testes novos com label `targetapp`, e 3 testes unitários novos para `_stat64` (`test_msvcrt.cpp`). Total: 265 testes verdes no preset com alvos; 218 em `debug` e `sanitize`.
- **Marco concluído:** `tl_thread.exe` fecha a validação do subconjunto atual de concorrência. O fixture cria duas threads convidadas sequenciais, cada uma escreve `Thread done`, termina via `ExitThread` e é aguardada por `WaitForSingleObject`; a thread principal escreve `Main done` e encerra com código `0`. Metadata e execução passam em Debug, Release e Sanitize (`LSAN_OPTIONS=detect_leaks=0`); a regressão é coberta por `fixture_tl_thread_metadata` e `runtime_tl_thread_matches_readobj`. A expansão seguinte de concorrência e WinSock passou a ser validada pelas fixtures genéricas descritas no marco abaixo.
- **Marco concluído:** a primeira entrega genérica do plano foi validada por fixtures próprias. `tl_files_wide.exe` cobre arquivos Unicode, posição, metadados, tempos, cópia e movimentação; `tl_resources.exe` acessa `RCDATA` somente leitura com limites validados; `tl_sync.exe` cobre eventos, mutex recursivo, semáforo, timeouts e `WaitForMultipleObjects`; `tl_process_parent.exe` cria filhos PE32+ pelo mesmo loader e testa código de saída/encerramento; `tl_network_loopback.exe` cobre TCP/UDP local, `localhost` e `WSAPoll`; `tl_registry_unicode.exe` cobre armazenamento genérico persistente Unicode. Cada fixture possui manifesto, metadata, execução e `--report`, sem tratamento específico para Roblox.
- **Marco concluído:** o alvo pinado `Efeckc17/simple-todo-c` (`bcdf3d5fcebb8c0b445edb791d54511194c1b6ca`) compila como PE32+ x64 com manifesto/ícone, overlay Linux versionado e metadata/`--report` protegidos. O relatório resolve **105/105 imports**; `USER32` possui controles lógicos EDIT/BUTTON/COMBOBOX/STATIC/SysListView32, comandos/notificações, foco, teclado e fechamento nativo; `SHELL32` usa menu X11 como bandeja emulada, sem a opção de autorun exclusiva do Windows. O smoke foi atualizado para o layout Linux e cobre adicionar, editar, buscar, concluir, excluir, esconder, mostrar, persistência e saída pela bandeja ou pela janela.
- **Marco concluído:** `dos2unix.exe` e `unix2dos.exe` executam o fluxo de conversão validado. O `--report` resolve 91/91 imports em cada binário; regressões e2e cobrem CRLF→LF, LF→CRLF e expansão de `uni_el_*.txt` com nome UTF-8. Os testes `targetapp_dos2unix_eol`, `targetapp_unix2dos_eol` e `targetapp_dos2unix_unicode-glob` passam com exit `0`; os arquivos de entrada CRLF/LF vêm da fonte pinada do dos2unix e o ouro UTF-8 está versionado em `tests/targets/golden/dos2unix/`.
- **Marco concluído:** carregamento dinâmico `KERNEL32` completo em `tl_dynload.exe` (`LoadLibraryA/W`, `LoadLibraryExA/W`, `FreeLibrary`, `GetModuleHandleExA/W`, `GetProcAddress` por nome e ordinal). A fixture prova `C:\Windows\System32\kernel32.dll` (extração de filename), API Set `api-ms-win-core-file-l1-1-0.dll` via forwarder, `LoadLibraryEx` com flags ignoradas, `GetProcAddress("GetTickCount64")` chamado dinamicamente, `FreeLibrary` e `GetModuleHandleEx` com `PIN`/`FROM_ADDRESS` (token `0x1000` e endereço `tl_entry`). `--report` resolve 14/14 imports, execução `dynload\n` exit `0`.
- **Marco concluído:** versão e locale `KERNEL32` em `tl_version.exe` (`GetVersionExA/W` 10.0.19044 `VER_PLATFORM_WIN32_NT`, `VerifyVersionInfoW`/`VerSetConditionMask` chain `VER_MAJOR|MINOR` `GREATER_EQUAL`, `GetUserDefaultLocaleName` `en-US` com `ERROR_INSUFFICIENT_BUFFER` e `LocaleNameToLCID` `en-US`→`0x0409`/`pt-BR`→`0x0416` case-insensitive). `--report` 10/10, `version\n` exit `0`.
- **Marco concluído:** espera por endereço `KERNEL32`/`api-ms-win-core-synch-l1-2-0.dll` em `tl_waitaddr.exe` (`WaitOnAddress` 1/2/4/8 alinhado, `ERROR_TIMEOUT` 1460, `ERROR_INVALID_PARAMETER` 87, `WakeByAddressSingle`/`All` com `version`+`cv`). Thread waiter bloqueia 5s e acorda via `Wake`, `waitaddr\n` exit `0`, `--report` 12/12.
- **Marco concluído:** fibras `KERNEL32` em `tl_fiber.exe` (`ConvertThreadToFiber`/`ConvertThreadToFiberEx` com flags, `CreateFiber`/`CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread` com `g_current_fiber_data`). `--report` 11/11, `fiber\n` exit `0`.
- **Marco concluído:** enumeração de processos `KERNEL32` em `tl_toolhelp.exe` (`CreateToolhelp32Snapshot` `TH32CS_SNAPPROCESS` via `/proc`, `Process32FirstW`/`NextW` `PROCESSENTRY32W` 568, `OpenProcess` token `kProcessHandleBase+pid`, `CloseHandle` para snapshot/process). `--report` 10/10, `toolhelp\n` exit `0`.
- **Marco concluído:** GUI Unicode `USER32` em `tl_win_w.exe` (`RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` via `wide_to_utf8`). `--report` 12/12, execução `Xvfb` análoga a `tl_win` com `WM_CREATE` e `WM_CLOSE`.
- **Marco concluído:** `SHELL32` pastas conhecidas em `tl_shell.exe` (`SHGetKnownFolderPath` `FOLDERID_RoamingAppData`→`$HOME/.config`, `SHGetFolderPathW` `CSIDL_APPDATA`, `SHGetFolderPathAndSubDirW` `TestSub`, `ShellExecuteW` `42`, `ShellExecuteExW` dummy `hProcess`). `--report` 8/8, `shell\n` exit `0`.
- **Marco concluído:** `GDI` estendido em `tl_gdiex.exe` (`GDI32` `CreateFontW`/`SetDCBrush/PenColor`, `gdiplus` 8 APIs, `UxTheme` `SetWindowTheme`, `WINMM` `timeSetEvent`, `dbghelp` `SymFromAddr`, `USER32` `GetDC`). `--report` 21/21, `gdiex\n` exit `0`.
- **Marco concluído:** `COM` mínimo `ole32.dll` em `tl_com.exe` (`CoInitialize`/`CoInitializeEx`/`CoUninitialize`/`OleInitialize`/`OleUninitialize` `S_OK`, `CoCreateInstance`/`CoGetClassObject` `REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`, `CoTaskMemAlloc/Free`). `--report` 12/12, `com\n` exit `0`.
- **Marco concluído (Fase 13.1):** `tl_install_setup.exe` instala
  `tl_install_app.exe` em `C:\\Program Files` de um prefixo exclusivo, cria o
  processo-filho no mesmo ambiente e o runtime cadastra/reabre a aplicação.
  `integration_install_prefix_catalog_run` cobre descoberta automática,
  `--app-exe`, múltiplos/nenhum candidato e isolamento; `qt_launcher_smoke`
  cobre o botão **Instalar** e a atualização da biblioteca. O benchmark WinRAR
  permanece `unsupported`.
- **Marco concluído (Fase 13.2):** o leitor, `--report` e o resolvedor tratam
  `IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT` em descritores RVA (`grAttrs=0x1`),
  classificam cada símbolo e preenchem a IAT antes do entry point. A fixture
  reproduzível `tl_delay_import.exe` executa usando somente
  `KERNEL32.dll!ExitProcess` atrasado; o caso de símbolo ausente retorna `5`
  com diagnóstico `mechanism="delay-import"`. WinRAR e Rockstar foram
  reanalisados apenas com `--report` e continuam `unsupported` pelas APIs
  restantes.
- **Marco concluído (Fase 13.13 — Cadeias de export forwarder):** o registro
  aceita `DLL.Símbolo` e `DLL.#ordinal`; `find_export_forwarded` segue até o
  export direto com limite de 32 saltos e rejeita ciclos, sintaxe inválida e
  destinos ausentes. Testes unitários cobrem cadeia de múltiplos saltos,
  destino ordinal, ciclo e símbolo inexistente. No Debug Linux, 29 testes
  unitários e 14 testes CTest direcionados passaram.
- **Marco concluído (Fase 13.3):** o núcleo reutilizável de unwinding AMD64
  lê e valida `.pdata`/`.xdata` v1, todos os opcodes x64 v1, handlers e
  cadeias; `RtlCaptureContext`, `RtlLookupFunctionEntry`,
  `RtlVirtualUnwind` e `RtlPcToFileHeader` são exports `KERNEL32`. A fixture
  `tl_unwind.exe` captura o contexto, desempilha um frame real e imprime
  `unwind\n`; CTest protege parser, APIs, trace e `--report`. Não há despacho
  SEH nem execução de handlers. WinRAR encontra `UNWIND_INFO` v2 e Rockstar
  um `SET_FPREG` não canônico; ambos permanecem `unsupported` e não foram
  executados.
- **Marco concluído (Fase 13.4):** o núcleo aceita `UNWIND_INFO` V1/V2,
  normaliza epílogos V2 e aceita `UWOP_SET_FPREG` estendido somente quando
  `OpInfo == FrameOffset`. `tl_unwind_v2.exe` prova desempilhamento no corpo,
  trace e `--report`; em epílogo V2 o contexto é preservado e há diagnóstico
  controlado, sem decodificação de instruções. WinRAR (151/251) e Rockstar
  (191/338) foram reanalisados somente com `--report`, continuam
  `unsupported` pelas APIs e pelo despacho SEH ausentes e não foram executados.
- **Marco concluído (Fase 13.5):** o despacho SEH explícito usa
  `RaiseException`, VEH, `__C_specific_handler`, `RtlUnwind`/`RtlUnwindEx` e
  filtro não tratado sobre `.pdata/.xdata` V1/V2 fora de epílogos. As fixtures
  `tl_seh.exe` e `tl_seh_v2.exe` executam `__try/__except` dentro de
  `CreateThread`, imprimem `seh\n` e são protegidas por metadata, trace,
  relatório e execução. WinRAR (153/251) e Rockstar (194/338) foram
  reanalisados apenas com `--report`, continuam `unsupported` e não foram
  executados.
- **Marco concluído (Fase 13.6):** `tl_locale_env_fls.exe` valida ambiente
  Win32 por processo (incluindo bloco UTF-16, expansão e `msvcrt!getenv`),
  ACP `1252`/OEMCP `437`, CP437, locale determinístico `en-US`,
  `GetLocaleInfoW`, `LCMapStringW/Ex` e FLS por thread com callbacks no fim
  da thread e em `FlsFree`; imprime `locale-env-fls\n`. Metadata, `--report`,
  trace e execução são protegidos por CTest. WinRAR (166/251) e Rockstar
  (207/338) foram reanalisados somente com `--report`, continuam
  `unsupported` e não foram executados.
- **Marco concluído (Fase 13.7):** `tl_locale_extended.exe` valida o locale
  estático `en-US` por `IsValidCodePage`, `IsValidLocale`, `GetLocaleInfoEx`,
  `EnumSystemLocalesW`, `GetStringTypeW`, `GetDateFormatW` e
  `GetTimeFormatW`; enumera somente `0409` por callback Microsoft x64 e
  imprime `locale-extended\n`. Metadata, `--report`, trace e execução passam
  em CTest. WinRAR (170/251), Logitech G HUB (92/114) e Rockstar (213/338)
  foram reanalisados somente com `--report`, reduziram lacunas e continuam
  `unsupported`; nenhum binário comercial foi executado.
- **Marco concluído (Fase 13.8):** `tl_process_console.exe` valida
  `STARTUPINFOW`, handles padrão mutáveis, tipo de arquivo, console UTF-16,
  `C:\Windows\System32`, recursos AMD64, encode/decode de ponteiro e SList
  alinhada; imprime `process-console-é\n`. Metadata, `--report`, trace e
  execução passam em Debug e Sanitize. WinRAR (179/251), Logitech G HUB
  (103/114) e Rockstar (223/338) foram reanalisados somente com `--report`,
  continuam `unsupported` e não foram executados.
- **Marco concluído (Fase 13.9):** `tl_file_metadata.exe` valida
  `FindFirstFileExW`, atributos, `FileBasicInfo`, `FileDispositionInfo` e
  `FileDispositionInfoEx` dentro de `C:\\` no prefixo; cobre exclusão no
  fechamento, exclusão POSIX e isolamento entre prefixos. Metadata, `--report`,
  trace e execução passam em Debug e Sanitize. WinRAR (181/251), Logitech G
  HUB (105/114) e Rockstar (225/338) foram reanalisados somente com `--report`,
  continuam `unsupported` e não foram executados.
- **Marco concluído (Fase 13.10):** `tl_security.exe` cria um arquivo em
  `C:\\`, obtém `TokenUser` pelo protocolo de tamanho, confirma
  `TokenElevation=0`, mescla/grava uma DACL e a relê na próxima execução. O
  armazenamento versionado mantém SID artificial e DACL por prefixo; CTest
  prova persistência em A, isolamento em B, metadata, `--report`, saída, exit
  code e trace em Debug e Sanitize. WinRAR (191/251) e Rockstar (232/338) foram
  reanalisados somente com `--report`, continuam `unsupported` e não foram
  executados; Logitech G HUB (105/114) não foi alterado porque o binário não
  está disponível localmente.
- **Marco concluído (Fase 13.11):** `tl_dialog.exe` valida template `DIALOG`
  padrão, `WM_INITDIALOG`, filhos lógicos, texto por ID, tabulação, ícone
  copiado e retorno modal 42. O cenário Xvfb envia Tab/Enter e protege o trace
  de `DialogBoxParamW`, `IsDialogMessageW` e `EndDialog`; o CTest registra 417
  casos sem falhas em Debug e em Sanitize (`LSAN_OPTIONS=detect_leaks=0`),
  com 416 executados e o smoke marcado `Skipped` quando o socket X11 não está
  disponível. WinRAR
  (202/251) e Rockstar (241/338) foram reanalisados somente com `--report`,
  continuam `unsupported` e `execution: not-attempted`.
- **Marco concluído (Fase 13.12 — WinINet HTTPS local):** `tl_wininet.exe`
  valida `InternetOpenW`, `InternetConnectW`, `HttpOpenRequestW`,
  cabeçalhos, envio, status, disponibilidade, leitura parcial, fechamento e
  `InternetCrackUrlW` contra um servidor TLS efêmero em `127.0.0.1`.
  O smoke usa uma CA local confiável no fluxo positivo e outra CA no fluxo
  negativo; hosts externos e HTTP simples têm rejeições unitárias. Não há
  proxy, cookies, credenciais, redirecionamento, Internet ou afirmação de
  WinTrust, e o Rockstar não foi executado.
- **Marco concluído (Fase 13.12 — OLE stream em memória):** `tl_stream.exe`
  resolve `ole32.dll!CreateStreamOnHGlobal` e valida a vtable Microsoft x64 de
  `IStream`, referências, `Read`/`Write`, `Seek`, `SetSize`, `Stat`,
  `Commit`/`Revert` e liberação. Cópia, clone, regiões bloqueadas, `OLEAUT32`
  e `IDispatch` continuam fora do contrato; o Rockstar não foi executado.
- **Marco concluído (Fase 13.12 — cadeia WinTrust explícita):** `tl_trust.exe`
  valida uma cadeia X.509 DER de dois certificados com raiz fornecida pela
  fixture, rejeita política com UI e raiz incorreta e registra o mecanismo
  `libcrypto`. Não há loja Windows, revogação, Authenticode ou suporte ao
  Rockstar.
- **Marco concluído (Fase 13.12 — lacunas KERNEL32 do LGHub):** `tl_k32_gap.exe`
  cobre `InitializeCriticalSectionAndSpinCount`, `InitializeCriticalSectionEx`,
  `FormatMessageA` e `AreFileApisANSI` com buffers, flags e erros controlados.
  O `--report` de `lghub_installer.exe` passou a resolver 114/114 imports; a
  execução em prefixo temporário entrou na fase de execução, mas expirou em 20 s
  (`GuestTimeout`) sem produzir arquivos, portanto o fluxo do instalador não é
  declarado compatível.
- **Marco concluído (Fase 13.12 — memória Global/Local compartilhada):**
  `tl_globalmem.exe` cobre `GlobalAlloc`, `GlobalLock`, `GlobalUnlock`,
  `GlobalFree`, `LocalAlloc` e `LocalFree`, incluindo `GMEM_MOVEABLE`,
  `GMEM_ZEROINIT`, contagem de locks e handles inválidos. A reanálise de
  2026-08-26 passou a resolver 209/251 imports do WinRAR e 261/338 do Rockstar;
  ambos continuam `unsupported` e não foram executados.
- **Marco concluído (Fase 13.12 — nome de certificado DER):**
  `tl_crypt32.exe` cobre `CRYPT32.dll!CertGetNameStringW` com
  `CERT_CONTEXT` explícito, nomes subject/issuer, consulta de capacidade e
  buffers insuficientes. O subconjunto não consulta SAN, loja Windows,
  Authenticode ou cadeia; a reanálise passou a resolver 262/338 imports do
  Rockstar, que continua `unsupported` e não foi executado.
- **Marco concluído (Fase 13.12 — travessia WTHelper):**
  `tl_wthelper.exe` cria e fecha estado com `WTD_STATEACTION_VERIFY/CLOSE`,
  percorre signer e certificados folha/raiz pelos três `WTHelper*`, extrai o
  CN pela `CertGetNameStringW` e rejeita índices inválidos/ponteiros após o
  fechamento. A reanálise passou a resolver 265/338 imports do Rockstar, que
  continua `unsupported` e não foi executado; Authenticode e loja Windows
  permanecem fora do contrato.
- **Marco concluído (Fase 13.12 — APIs estendidas de UI e Retângulos):**
  `tl_user_ext.exe` cobre 17 APIs de `USER32.dll` (`GetDesktopWindow`,
  `GetFocus`, `SetCapture`, `ReleaseCapture`, `GetCapture`, `BringWindowToTop`,
  `GetWindow`, `GetClassNameA/W`, `GetWindowThreadProcessId`, `CallWindowProcA/W`,
  `PeekMessageA/W`, `RedrawWindow`, `PtInRect`, `CopyRect`, `MapWindowPoints`,
  `MonitorFromWindow`, `GetSysColor`, `CharUpperW`, `CharLowerW`, `DrawTextA/W`).
  WinRAR avançou para 216/251 imports e Rockstar para 276/338 imports.
- **Marco concluído (Fase 13.13 — Sistema KERNEL32, Processos e Tempo):**
  `tl_k32_system.exe` cobre `OutputDebugStringA/W`, `SetDllDirectoryW`,
  `VirtualQueryEx`, `GetTimeZoneInformation`, `GetProcessId`,
  `QueryFullProcessImageNameW`, `FileTimeToLocalFileTime`,
  `GetLongPathNameW`, `GetShortPathNameW`, `SetThreadPriority` e
  `GetProcessAffinityMask`. `--report` resolve 14/14 imports, saída `k32system\n`,
  exit `0`.
- **Marco concluído (Fase 13.13 — Caminhos SHLWAPI e Shell SHFileOperation):**
  `tl_shell_path.exe` cobre `SHAutoComplete`, `PathIsRelativeA/W`,
  `PathCombineW`, `PathRemoveFileSpecW` e `SHELL32.dll!SHFileOperationW`.
  `--report` resolve 9/9 imports, saída `shellpath\n`, exit `0`.
- **Marco concluído (Fase 13.13 — Suporte Estrutural e Parser de Pacotes MSIX/AppX):**
  `tradutorlinux::package` valida a central directory ZIP/MSIX, limites,
  traversal, CRC e manifesto DEFLATE/data descriptor; o parser estrutural de
  `AppxManifest.xml` extrai identidade, aplicações e executável principal sem
  resolver DTDs ou recursos externos. As regressões cobrem namespaces,
  comentários, CDATA, entidades e XML malformado. No Debug Linux, os oito
  testes `MsixParserTest.*` e o teste de afinidade passaram no unitário e no
  CTest. A etapa B8 posterior adicionou instalação/execução apenas para
  pacotes com PE32+ x86-64 nativo; .NET/Mono continua fora do escopo.
- **Marco concluído (Fase 13.13 — Análise e Bateria de Testes do Portfólio Popular):**
  Bateria automatizada de `--report` e execução controlada no conjunto de aplicativos
  Windows x64 mais demandados pela comunidade:
  - `Logitech_GHUB_x64.exe`: **100% (114/114)** de imports resolvidos, execução
    completa do bootstrap CRT/FLS sem falhas de memória (código 72 por timeout controlado).
  - `WinRAR_x64.exe`: **88% (222/251)** de imports resolvidos (+13 pendências eliminadas).
  - `Rockstar-Games-Launcher.exe`: **84% (284/338)** de imports resolvidos (+19 pendências eliminadas).
  - `7z_x64.exe` (7-Zip CLI): **67% (90/133)** de imports resolvidos.
  - `putty_x64.exe` (PuTTY SSH): **64% (225/348)** de imports resolvidos.
  - `7zFM_x64.exe` (7-Zip GUI): **60% (179/298)** de imports resolvidos.
  - `notepad++.exe` (Notepad++ x64): **50% (294/584)** de imports resolvidos.
  - `Affinity x64.msix`: Reconhecido como pacote de aplicativo válido pelo parser de manifesto.
  - `HWiNFO64.exe` e `Rufus_x64.exe`: Validados com segurança pelo parser PE contra cabeçalhos corrompidos/fora da imagem.
  - Instaladores com wrappers 32-bit (NSIS/Inno): Rejeitados com segurança pelo filtro de arquitetura x64.
- **Histórico do ciclo (entregue nos marcos seguintes):** implementar o incremento de APIs
  compartilhadas mapeadas pelo portfólio (`KERNEL32!CreateHardLinkW`, `KERNEL32!K32GetModuleFileNameExW`,
  `OLEAUT32!ordinais`, `GDI32!CreateBitmap` e `COMCTL32!CreateToolbarEx`), protegido por fixtures PE32+ reproduzíveis.
- **Marco concluído (Fase 13.13 — OLEAUT32 BSTRs/Variantes e Ordinais de Automação):**
  `tl_oleaut_bstr.exe` cobre `SysAllocString`, `SysFreeString`, `SysStringLen`,
  `VariantInit` e `VariantClear`, com tabela de exports por ordinais (2, 4, 6, 7, 8, 9, 10,
  15, 16, 149, 150, 200, 201). `--report` resolve 8/8 imports, saída `oleautbstr\n`, exit `0`.
- **Marco concluído (Fase 13.13 — KERNEL32 HardLinks e GDI32 Bitmaps):**
  `tl_k32_gdi_link.exe` cobre `CreateHardLinkW`, `K32GetModuleFileNameExW`,
  `CreateBitmap`, `GetObjectW`, `StretchBlt` e `CreateDIBSection`. `--report` resolve 9/9
  imports, saída `k32gdilink\n`, exit `0`.
  - Reanálise do portfólio: WinRAR atingiu **90% (228/251)**, Rockstar subiu para
    **86% (292/338)**, 7-Zip CLI subiu para **73% (98/133)**, Notepad++ para
    **51% (300/584)** e PuTTY para **64% (226/348)**.
- **Marco concluído (Fase 13.13 — WinRAR x64 100% de Resolução de Imports):**
  Implementado lote completo de 23 APIs restantes em `KERNEL32.dll` (`GetTickCount`, `SetCurrentDirectoryW`,
  `DeviceIoControl`, `FoldStringW`, `SetThreadExecutionState`, `AllocConsole`, `FreeConsole`, `SystemTimeToTzSpecificLocalTime`,
  `IsDBCSLeadByte`, `GetNumberFormatW`), `USER32.dll` (`SetUserObjectInformationW`, `WaitForInputIdle`, `FindWindowExW`,
  `SetProcessDefaultLayout`), `ADVAPI32.dll` (`LookupPrivilegeValueW`, `AdjustTokenPrivileges`), `SHELL32.dll`
  (`SHGetFileInfoW`, `SHGetPathFromIDListW`, `SHBrowseForFolderW`, `SHGetMalloc`, `SHChangeNotify`) e `ole32.dll`
  (`CLSIDFromString`).
  - `tl_winrar_kernel_shell.exe`: `--report` resolve 19/19 imports, saída `winrarkernelshell\n`, exit `0`.
  - **`WinRAR_x64.exe`**: atingiu **100% (251/251 imports resolvidos)** e entra em execução no bootstrap CRT/FLS.
  - Reanálise do portfólio: Rockstar Launcher subiu para **87% (295/338)**, 7-Zip CLI subiu para **77% (103/133)**,
    PuTTY subiu para **65% (228/348)** e Notepad++ para **52% (304/584)**.
- **Histórico do ciclo (entregue nos marcos seguintes):** implementar rotinas CRT para o 7-Zip CLI (`7z_x64.exe`)
  e controles comuns de `COMCTL32.dll` (`CreateToolbarEx`, `ImageList_GetImageInfo`), protegido por fixtures PE32+ reproduzíveis.

### Estudo de caso: `RobloxPlayerInstaller.exe` (benchmark de cobertura)

Em 2026-08-19, o instalador encontrado localmente em `Downloads` foi analisado
estaticamente, sem executar o entry point. O arquivo analisado é um PE32+ x86-64
com 17 DLLs importadas e 430 imports; o `--report` resolveu 75/430 imports
(17%), classificou o resultado como `unsupported` e registrou
`execution: not-attempted`. SHA-256 do arquivo analisado:
`d156faf0c712d4ce26d95a596ad9b1dfc813021b5c422c93887b2522d8b01a59`.

Em 2026-08-22, o mesmo arquivo foi reanalisado com o `--report` atual:
196/430 imports resolvidos (45%), ainda `unsupported`, com `execution:
not-attempted`; o avanço vem das fases 10–12.

Em 2026-08-22, após `LoadLibrary`/`GetVersionEx`/`Locale`, o `--report` resolve
206/430 imports (47%), ainda `unsupported`, com `execution: not-attempted`.

Em 2026-08-22, após `WaitOnAddress`, o `--report` resolve 208/430 (48%), ainda
`unsupported`, com `execution: not-attempted` (imports `api-ms-win-core-synch-l1-2-0.dll` agora resolvidos via `KERNEL32`).

Em 2026-08-22, após `Fibers`, o `--report` resolve 210/430 (48%), ainda
`unsupported`, com `execution: not-attempted` (`CreateFiberEx`/`ConvertThreadToFiberEx`/`SwitchToFiber`/`DeleteFiber`).

Em 2026-08-22, após `Toolhelp`, o `--report` resolve 214/430 (49%), ainda
`unsupported`, com `execution: not-attempted` (`CreateToolhelp32Snapshot`/`Process32FirstW`/`NextW`/`OpenProcess`).

Em 2026-08-22, após `GUI Unicode`, o `--report` resolve 221/430 (51%), ainda
`unsupported`, com `execution: not-attempted` (`RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW` etc.).

Em 2026-08-22, após `SHELL32`, o `--report` resolve 225/430 (52%), ainda
`unsupported`, com `execution: not-attempted` (`SHGetKnownFolderPath`/`SHGetFolderPathW`/`ShellExecuteW`/`ExW`).

Em 2026-08-22, após `GDI` estendido, o `--report` resolve 240/430 (55%), ainda
`unsupported`, com `execution: not-attempted` (`GDI32` `CreateFontW`/`SetDCBrush/PenColor`, `gdiplus` 8, `UxTheme` `SetWindowTheme`, `WINMM` `timeSetEvent`, `dbghelp` `SymFromAddr`).

Em uma leitura completa histórica de 2026-08-23, o relatório resolveu 244/430
(56%). No runtime atual, a mesma amostra para antes dos imports: o
`UWOP_SET_FPREG` estendido em RVA `0xbdb0b8` traz `OpInfo=10` e
`FrameOffset=0`, combinação fora do padrão aceito na Fase 13.4. O parser retorna
controladamente `unsupported-mechanism`/exit `5`; o arquivo continua
`unsupported` e essa variante de unwind deve ser tratada como dependência de
portfólio, sem criar uma exceção específica para Roblox.

O Roblox não é o único alvo nem autoriza implementação exclusiva para si. Ele
fica registrado como um benchmark grande para priorizar capacidades
reutilizáveis por várias classes de aplicativos. As lacunas observadas são:

- [x] ampliar o núcleo `KERNEL32` para arquivos e caminhos Unicode, recursos,
  tempo, sincronização e processos filhos, com fixtures próprias; a memória
  mapeada (`MapViewOfFile`/`CreateFileMappingW`) foi entregue depois;
- [x] implementar o núcleo de unwinding x64 da imagem convidada:
  `.pdata`/`.xdata` V1/V2, epílogos V2, `RtlCaptureContext`,
  `RtlLookupFunctionEntry`, `RtlVirtualUnwind` e `RtlPcToFileHeader`, com
  fixtures e regressões;
- [x] completar o despacho SEH explícito x64: `__C_specific_handler`,
  `RtlUnwind`/`RtlUnwindEx`, transferência de contexto, VEH, filtro não
  tratado e `__try/__except` V1/V2 fora de epílogos; C++/`__finally` e sinais
  Linux continuam fora do escopo;
- [x] implementar carregamento dinâmico real: `LoadLibraryA/W`,
  `LoadLibraryExA/W`, `FreeLibrary` e `GetModuleHandleExA/W` + `GetProcAddress` por nome e ordinal (fixture `tl_dynload.exe` cobre `C:\` path, API Set `api-ms-win-core-file-l1-1-0.dll`, `LoadLibraryEx`, `GetProcAddress`/`FreeLibrary`/`GetModuleHandleEx` `PIN`/`FROM_ADDRESS`); `GetProcAddress` agora resolve via `find_export_global`;
- [x] implementar APIs de versão e locale: `GetVersionExA`,
  `VerifyVersionInfoW`/`VerSetConditionMask`, `GetUserDefaultLocaleName` e
  `LocaleNameToLCID` (fixture `tl_version.exe` cobre 10.0.19044, `Verify`/`VerSetConditionMask` chain, `en-US`→`0x0409`/`pt-BR`→`0x0416`);
- [x] implementar espera por endereço (`WaitOnAddress`/
  `WakeByAddressSingle`/`WakeByAddressAll`) exposta pela API Set
  `api-ms-win-core-synch-l1-2-0.dll` (fixture `tl_waitaddr.exe` cobre `size` 1/2/4/8, timeout 1460, tamanho inválido 87, `WaitOnAddress` em thread e `WakeByAddressSingle`);
- [x] implementar fibers (`CreateFiberEx`, `ConvertThreadToFiberEx` e
  `SwitchToFiber`) sobre a infraestrutura de threads da Fase 11 (fixture `tl_fiber.exe` cobre `ConvertThreadToFiberEx` com flags, `CreateFiberEx` commit/reserve, `SwitchToFiber`/`GetFiberData`/`DeleteFiber`/`ConvertFiberToThread`);
- [x] implementar enumeração de processos: `CreateToolhelp32Snapshot`,
  `Process32FirstW`/`Process32NextW`, `OpenProcess` (fixture `tl_toolhelp.exe` cobre `/proc` enumeração, `PROCESSENTRY32W` 568, `OpenProcess` token); `K32*` permanece via `PSAPI` existente;
- [x] ampliar `SHELL32`/`SHLWAPI` para pastas conhecidas, execução de processos
  e manipulação de caminhos (fixture `tl_shell.exe` cobre `FOLDERID_RoamingAppData`/`CSIDL_APPDATA`/`TestSub`, `ShellExecuteW`/`ExW`);
- [x] criar uma camada `WS2_32`/rede com sockets, resolução local e polling,
  validada somente em loopback;
- [x] criar o armazenamento genérico Unicode de `ADVAPI32` para registro;
  demais APIs `CRYPT32`, loja Windows e Authenticode continuam pendentes fora
  do envelope `TLTC` (a extração restrita de nomes DER é coberta por
  `tl_crypt32.exe`);
- [x] definir uma camada `OLE32`/COM mínima (`CoCreateInstance`/`CoGetClassObject`/`OleInitialize` via `ole32.dll`, fixture `tl_com.exe` cobre `S_OK`/`REGDB_E_CLASSNOTREG`/`CLASS_E_NOAGGREGATION`);
- [x] ampliar a GUI de forma genérica: variantes Unicode de `USER32` (`RegisterClassExW`/`CreateWindowExW`/`DefWindowProcW`/`GetMessageW`/`DispatchMessageW`/`SetWindowTextW`/`GetWindowTextW`/`FindWindowW`/`LoadCursorW`/`SendMessageW` etc. via wrappers `wide_to_utf8`), validado por `tl_win_w.exe` sob `Xvfb` análogo a `tl_win`;
- [x] ampliar `SHELL32`/`SHLWAPI` para pastas conhecidas, execução de processos
  e manipulação de caminhos (fixture `tl_shell.exe`);
- [x] avaliar `GDI32`, `gdiplus`, `UxTheme`, `WINMM`, `dbghelp` para desenho, imagens, temas,
  temporizadores multimídia e diagnóstico (fixture `tl_gdiex.exe` cobre 3+8+1+1+1); `POWRPROF.dll` e `IPHLPAPI.DLL` permanecem avaliação futura;
- [x] manter isolamento de processo, timeout, `--report`, mensagens de falha e
  testes de integração para as famílias implementadas;
- [x] definir e implementar limites configuráveis de CPU/RAM por aplicativo
  (item `B1` do backlog consolidado); contrato, catálogo, herança e
  diagnósticos estão cobertos por fixtures e testes CTest.

A ordem de implementação continua subordinada à fase atual e ao método do
projeto: cada item precisa de um aplicativo-alvo ou fixture independente,
teste de regressão, contrato documentado e registro na matriz de
compatibilidade. O instalador do Roblox só será executado quando o portfólio
da Fase 13 tiver entregado as famílias de dependências necessárias; ele não
substitui os demais alvos do portfólio.

## Fase 0 — Fundação e contrato

- [x] Criar a estrutura CMake, compilação com warnings rigorosos e testes automatizados.
- [x] Fixar o alvo: Linux x86-64 hospedando somente PE32+ x86-64.
- [x] Definir formato do trace, códigos de erro e matriz de compatibilidade.
- [x] Criar binários de teste próprios, incluindo um executável sem CRT para o primeiro salto ao entry point.
- [x] Documentar as convenções Microsoft x64 e System V AMD64 usadas em cada fronteira.
- [x] Configurar sanitizers e análise estática para os testes quando possível.

### Critério de saída — atendido

O projeto compila de forma reproduzível, executa seus testes básicos e possui fixtures Windows versionadas com seus imports documentados.

## Fase 1 — Leitor de PE seguro

- [x] Ler e validar DOS header, NT headers, optional header e section headers.
- [x] Exibir seções, entry point, imports, relocations e arquitetura.
- [x] Rejeitar PE inválido, truncado ou de arquitetura incompatível com mensagens precisas.
- [x] Cobrir o parser com testes unitários e corpus de arquivos malformados.
- [x] Comparar a saída com `llvm-readobj` nas fixtures geradas.

### Critério de saída

O leitor identifica corretamente os fixtures válidos e nunca acessa memória fora dos limites ao processar fixtures inválidos. Validação: fixtures `tl_hello.exe`/`tl_nop.exe` parseados e comparados com `llvm-readobj` em CTest, corpus malformado coberto por testes, presets `debug` e `sanitize` verdes e análise estática sem pendências.

## Fase 2 — Mapeamento de imagem

- [x] Reservar a imagem no endereço preferencial quando possível.
- [x] Copiar headers e seções, respeitando alinhamentos e permissões de página.
- [x] Aplicar base relocations para PE32+ x86-64.
- [x] Validar o mapeamento com executáveis mínimos que ainda não chamam APIs.
- [x] Garantir que a imagem não permaneça inteira com permissão RWX por conveniência.

### Critério de saída

Um executável mínimo pode ser mapeado e inspecionado pelo runtime sem executar funcionalidades fora do escopo.

Validação: `tl_nop.exe`, `tl_hello.exe` e `tl_reloc.exe` mapeados via CLI em `debug` e `sanitize`; relocations aplicadas e verificadas em memória mapeada quando a base difere da preferencial (ASan bloqueia `0x140000000` no preset `sanitize`); permissões de região verificadas via `/proc/self/maps` (headers `r--`, `.text` `r-x`, nunca `rwx`); presets `debug` e `sanitize` verdes e análise estática sem pendências. O contrato de mapeamento está em `docs/arquitetura/mapeamento-imagem.md`.

## Fase 3 — Imports e bootstrap mínimo

- [x] Resolver a import table para módulos internos suportados.
- [x] Implementar trampolins e ponte de ABI para chamadas do programa à camada hospedeira.
- [x] Preparar as estruturas mínimas de processo e thread exigidas pelo escopo inicial.
- [x] Adicionar diagnóstico para DLL, símbolo, ordinal, forwarder ou delay import ausente.
- [x] Definir o comportamento de falha antes do entry point quando uma dependência não for suportada.

### Critério de saída — atendido

O runtime resolve imports conhecidos com o ABI correto e informa de maneira reproduzível qualquer dependência desconhecida.

Validação: registro interno de `KERNEL32.dll`, `USER32.dll` e `GDI32.dll`; resolução por nome e ordinal com patch da IAT e restauração das permissões; fronteiras `ms_abi` testadas diretamente; processo convidado com pilha de 1 MiB e guard page; fixture `tl_missing_dll.exe` retorna `5` sem executar o entry point e emite `unknown-symbol`; falhas de símbolo, ordinal, símbolo sem implementação, delay import e slot de IAT inválido têm testes unitários. Os testes Sanitizer locais exigem `LSAN_OPTIONS=detect_leaks=0` porque a descoberta do GoogleTest falha no LeakSanitizer sob ptrace.

## Fase 4 — Console: primeiro marco público

- [x] Implementar `GetStdHandle`, `WriteFile`, `ReadFile` e `ExitProcess`.
- [x] Definir e testar conversão entre handles Windows e descritores Linux.
- [x] Executar `tl_hello.exe` e uma ferramenta de eco construída no repositório.
- [x] Verificar saída, retorno, trace e tratamento de erros em CI.
- [x] Documentar exatamente quais flags, handles e encodings são suportados.

### Critério de saída — MVP atendido

```text
./tradutorlinux --trace tests/samples/tl_hello.exe
```

O comando escreve a saída esperada, retorna o código correto, produz trace reproduzível e possui testes para todas as APIs usadas pelo fixture.

Validação: `tl_hello.exe` e `tl_echo.exe` executados com stdout verificado; `ExitProcess` e o código de retorno registrados no trace; handles padrão convertidos para descritores Linux por tokens opacos; `ReadFile`/`WriteFile` limitados a I/O síncrono de console e bytes sem conversão de encoding; 95 testes passaram nos presets `debug` e `sanitize` (sanitize local com `LSAN_OPTIONS=detect_leaks=0` por limitação do LeakSanitizer durante descoberta sob ptrace); `cppcheck` e `clang-tidy` passaram.

## Fase 5 — Runtime básico

- [x] Implementar `GetLastError`/`SetLastError` e o mapeamento de erros necessário.
- [x] Implementar `VirtualAlloc`/`VirtualFree` com semântica limitada e documentada.
- [x] Implementar abertura, leitura, escrita e fechamento de arquivos para um subconjunto de flags.
- [x] Definir normalização de caminhos e política explícita para caminhos Windows.
- [x] Adicionar testes de concorrência somente quando o modelo de threads fizer parte do escopo.

### Critério de saída — atendido

Uma aplicação de console consegue ler e escrever arquivos e usar memória alocada pelo runtime, com erros verificáveis e documentados.

Validação: `tl_file.exe` usa `VirtualAlloc`, `CreateFileA`, `WriteFile`, `ReadFile`, `CloseHandle`, `VirtualFree`, `GetLastError` e `SetLastError`; caminhos relativos são normalizados de `\\` para `/`, enquanto caminhos absolutos e drives são rejeitados; `VirtualAlloc` aceita somente `MEM_COMMIT | MEM_RESERVE` com `PAGE_READONLY` ou `PAGE_READWRITE`, e `VirtualFree` somente `MEM_RELEASE` com tamanho zero. Debug e sanitize passaram com 101 testes; `cppcheck`, `clang-tidy` e `git diff --check` também passaram.

## Fase 6 — Carregamento e cobertura controlada

- [x] Adicionar APIs somente guiadas por aplicações-alvo e testes de regressão.
- [x] Evoluir suporte a DLLs, resources, TLS callbacks, forwarders e delay-load conforme necessário.
- [x] Publicar uma matriz com aplicativo, arquitetura, imports, APIs usadas, estado e limitações.
- [x] Adicionar um modo de relatório que mostre o que falta para tentar executar um `.exe`.
- [x] Revisar periodicamente o custo de cada API em relação ao valor para os aplicativos-alvo.

### Critério de saída — atendido

Cada aplicação declarada como suportada possui um teste de regressão e uma lista explícita de limitações.

Validação: `tl_hello.exe`, `tl_echo.exe` e `tl_file.exe` possuem testes de integração e entradas na matriz; `tl_missing_dll.exe` protege o caminho de rejeição; `--report` tem teste unitário e CTest real, retorna `0` para `tl_file.exe`, lista cada import e declara `execution: not-attempted`. O modo não mapeia nem executa a imagem.

## Fase 7 — Avaliar GUI

- [x] Decidir que uma interface Win32 mínima é um objetivo experimental de produto.
- [x] Criar um subsistema de janela e eventos separado do runtime de console.
- [x] Começar por `MessageBoxA` e uma janela simples, com fixture PE32+ e teste automatizado de metadata/report.
- [x] Definir a integração inicial com X11 direto, mantendo a camada isolada para futura decisão sobre Wayland/toolkit.
- [x] Implementar um subconjunto mínimo de janela e eventos (`RegisterClassExA`, `CreateWindowExA`, `ShowWindow`, `UpdateWindow`, `GetMessageA`, `TranslateMessage`, `DispatchMessageA`, `DefWindowProcA`, `DestroyWindow`, `PostQuitMessage`) com fixture `tl_win.exe` que cria janela, desenha texto e encerra ao fechar (`WM_CLOSE`).
- [x] Provar a fronteira de ABI host→convidado invocando o `WNDPROC` do convidado pela convenção Microsoft x64.
- [x] Estender o teclado: `WM_KEYUP`, virtual keys por `keysym` (incluindo teclas sem caractere), `Shift` refletido no `WM_CHAR` (fixture `tl_key.exe`).
- [x] Implementar `SetTimer`/`KillTimer` com despacho periódico de `WM_TIMER` no pump (fixture `tl_timer.exe`).
- [x] Implementar o GDI mínimo (`GDI32.dll!GetStockObject`/`TextOutA`; `USER32.dll!BeginPaint`/`EndPaint`/`GetDC`/`ReleaseDC`) com `HDC == HWND` e `PAINTSTRUCT` real (fixture `tl_gdi.exe`).

Validação local: os 129 testes dos presets `debug`, `sanitize` e `release` passam quando o ambiente fornece X11/Xvfb; sem acesso a `/tmp/.X11-unix`, o `runtime_gui_smoke` é marcado como `Skipped` rapidamente. A suíte inclui `tl_win.exe`, `tl_win2.exe`, `tl_key.exe`, `tl_timer.exe`, `tl_gdi.exe` e `tl_paint.exe`, resolução dos imports de `USER32.dll`/`GDI32.dll`, relatório sem execução, testes de layout ABI e relocations PE32+. O loader valida o entry point, usa a pilha convidada com guard page, aplica somente `DIR64`/`ABSOLUTE` para PE32+ e rejeita execução fora da base quando não há relocations. O `runtime_gui_smoke` usa `Xvfb -displayfd`, não depende de displays fixos e aborta/limpa o servidor de forma controlada. `cppcheck` e `clang-tidy` passam sem pendências.

### Critério de saída — atendido

Uma aplicação gráfica de teste cria uma janela, recebe eventos básicos e encerra corretamente, sem comprometer o runtime de console.

Validação visual (2026-08-15, sessão X11 `DISPLAY=:0` acessível): `tl_gui.exe` executado com `--trace` abriu a janela "TradutorLinux GUI / Fase 7" com botão OK; ao clicar, o runtime registrou `[tl][runtime][info] ExitProcess exit-code="0" status="success" mechanism="guest-transfer"`, `[tl][process][info] exit exit-code="0" explicit="sim"` e encerrou com código `0`, liberando a imagem. `tl_win.exe` executado com `--trace` e `TL_GUI_AUTOCLOSE_MS=1` registrou `RegisterClassExA`, `CreateWindowExA`, `GetMessageA message="WM_QUIT"` e `ExitProcess exit-code="0"`, confirmando o message loop de ponta a ponta (autoclose → `WM_CLOSE` → `DefWindowProcA` → `DestroyWindow` → `WM_DESTROY` → `PostQuitMessage(0)`).

## Fase 8 — Aplicativos-alvo reais

O projeto deixa de medir progresso apenas por fixtures e passa a medir por
aplicativos pequenos, úteis e reproduzíveis. Cada aplicativo-alvo deve ser
fixado por versão, arquitetura, toolchain e lista de imports.

- [x] Definir de 3 a 5 aplicativos-alvo reais, preferencialmente de código aberto e compiláveis no CI.
- [x] Priorizar utilitários de console: ferramentas de texto, arquivos, configuração e empacotamento simples.
- [x] Criar teste por aplicativo com stdout, stderr, exit code, arquivos produzidos e timeout.
- [x] Fazer o `--report` agrupar imports ausentes por DLL e por fase.
- [x] Separar "não suportado", "falhou durante a execução" e "resultado incorreto".
- [x] Publicar pontuação de compatibilidade por aplicativo; iniciar não é suficiente.

### Critério de saída

Pelo menos três aplicativos reais, pequenos e úteis executam um fluxo completo
de teste no Linux, com limitações publicadas e regressão automatizada. Fixtures
continuam obrigatórias para proteger contratos de ABI, mas deixam de ser a
única evidência do produto.

## Fase 9 — Base de processo e CRT

A maior barreira para aplicativos compilados normalmente será a camada de
runtime C e o estado básico do processo. Esta fase é guiada pelos imports dos
aplicativos escolhidos.

- [x] Implementar `GetModuleHandleA/W`, `GetProcAddress` limitado aos módulos registrados e informações básicas do processo.
- [x] Implementar linha de comando e ambiente: `GetCommandLineA/W`, `GetEnvironmentVariableA/W` e conversão documentada de encoding.
- [x] Implementar heap básico: `HeapAlloc`, `HeapFree`, `HeapReAlloc`, `GetProcessHeap`.
- [x] Implementar a espera exigida pelo primeiro alvo: `Sleep`.
- [x] Implementar tempo adicional quando um aplicativo-alvo justificar: `GetTickCount64` e `GetSystemTimeAsFileTime`.
- [x] Adicionar o subconjunto mínimo de `msvcrt.dll` exigido pelo primeiro alvo (`xxd`).
- [x] Expandir a CRT somente pelos imports e fluxos exigidos pelos próximos aplicativos-alvo.
- [x] Cobrir inicialização/encerramento do CRT, argumentos `argc/argv`, retorno de `main` e erros para o primeiro alvo.
- [ ] Ampliar essa cobertura para cada nova família de CRT ou aplicativo
  suportado, conforme o portfólio admitir novos alvos (item `B5`).

### Critério de saída

Ao menos dois aplicativos compilados com CRT executam seus fluxos principais,
recebem argumentos e retornam seus códigos corretamente.

## Fase 10 — Sistema de arquivos e utilitários

Expandir a camada para programas que trabalham com diretórios, configuração e
arquivos, mantendo uma tradução de caminhos segura e explícita.

- [x] Implementar `FindFirstFileA/W`, `FindNextFileA/W` e `FindClose`.
- [x] Implementar `GetFileAttributesA/W`, `DeleteFileA/W`, `MoveFileA/W` e `CreateDirectoryA/W`.
- [x] Implementar `SetFilePointer`, tamanhos de arquivo e modo append quando exigidos.
- [x] Definir diretório atual, diretório do executável e variáveis de ambiente sem inventar letras de drive.
- [x] Implementar conversão UTF-16/UTF-8 e testar nomes não ASCII.
- [x] Adicionar testes de permissões, arquivos inexistentes, diretórios e concorrência controlada.
- [x] Adicionar tamanho/posição, tempos, metadados, cópia, movimentação,
  diretórios wide e leitura segura de recursos PE com fixtures independentes.

### Critério de saída

Um aplicativo-alvo consegue descobrir arquivos, criar saída em diretório, ler
configuração e lidar com erros de filesystem sem caminhos fixos do projeto.

## Fase 11 — Concorrência e rede opcional

Esta fase só começa se um aplicativo-alvo justificar threads ou rede.

- [x] Implementar `CreateThread`, `ExitThread`, `WaitForSingleObject` e `CloseHandle`.
- [x] Implementar `CRITICAL_SECTION` compatível com o escopo atual de convidado single-thread.
- [x] Ampliar sincronização para eventos, mutexes, semáforos e `WaitForMultipleObjects` com fixture própria.
- [x] Definir TLS, encerramento de threads e chamadas ABI em threads convidadas.
- [x] Criar uma camada WinSock mínima separada de `KERNEL32.dll`, com teste TCP/UDP em loopback.
- [x] Criar `CreateProcessW`, `GetExitCodeProcess` e `TerminateProcess` sob o
  contrato de filhos PE32+ validados pelo mesmo loader.
- [x] Testar deadlock, timeout, cancelamento e propagação de falha do convidado.

### Critério de saída

Um aplicativo-alvo multithread passa testes repetíveis sem corrida conhecida,
deadlock ou corrupção de estado. Rede só entra com alvo concreto e testes
reprodutíveis.

## Fase 12 — GUI útil por aplicativo

A GUI evolui a partir de um aplicativo-alvo, e não de uma lista abstrata de
APIs.

- [x] Escolher e fixar `Efeckc17/simple-todo-c` no commit `bcdf3d5fcebb8c0b445edb791d54511194c1b6ca`.
- [x] Implementar o subconjunto de mouse, foco, teclado, comandos, notificações, menus e ciclo de vida exigido pelo alvo.
- [x] Implementar fontes, brushes, desenho e invalidação somente no subconjunto usado pelo alvo.
- [x] Implementar EDIT, BUTTON, COMBOBOX, STATIC e SysListView32 como controles lógicos.
- [x] Validar o smoke completo sob Xvfb e promover o alvo após evidência de integração.
- [x] Avaliar Wayland/toolkit depois de existir uma aplicação GUI real suportada;
  a camada plain Wayland está documentada e separada do backend X11, enquanto
  os smokes automatizados continuam usando X11/Xvfb.
- [x] Automatizar propriedades observáveis, stdout, exit code, trace, persistência e visibilidade; screenshot permanece fora do escopo.

### Critério de saída

Um aplicativo GUI real abre, recebe interação, renderiza seu fluxo principal e
encerra corretamente em uma sessão X11 de teste, com limitações publicadas.

## Fase 13 — Compatibilidade ampla por portfólio

Esta fase transforma a expansão por um único aplicativo GUI em cobertura por
classes de uso. A meta de longo prazo é maximizar a cobertura prática de
aplicativos Win32 PE32+ x86-64 de espaço de usuário; ela não equivale a prometer
compatibilidade imediata com qualquer executável, jogo ou mecanismo protegido.

- [x] Fixar o núcleo reproduzível do portfólio com fontes, versões, hashes,
  manifests e regressões para `xxd`, `bzip2`, `dos2unix`/`unix2dos` e
  `simple-todo`, além das fixtures de instalador e rede;
- [x] Completar o portfólio versionado com representantes reais adicionais de
  instalador (`WinRAR`), GUI de produtividade (`7zFM`/`Notepad++`) e ferramenta
  de rede (`PuTTY`); os níveis funcionais continuam separados da existência
  do registro e permanecem publicados na matriz (item `B5`).
- [x] **Prioridade 13.1 — instaladores x64 nativos:** usar as amostras WinRAR,
  Logitech G HUB, Rockstar e Roblox como evidência de cobertura, mas escolher
  um instalador PE32+ x86-64 reproduzível como alvo de regressão inicial.
- [x] Criar um prefixo exclusivo para cada instalação e propagá-lo de forma
  explícita ao runtime, ao catálogo e a todos os processos-filhos do
  instalador; o prefixo padrão compartilhado não é suficiente para este fluxo.
- [x] Implementar `delay-import` no leitor, relatório e resolvedor, com
  diagnóstico separado para cada símbolo atrasado.
- [x] **Prioridade 13.4 — metadados de unwinding x64 V2:** normalizar
  `UOP_Epilog`, aceitar a extensão compatível de `UWOP_SET_FPREG` e manter
  `RtlVirtualUnwind` seguro fora de epílogos, com fixture e regressões.
- [x] Implementar o despacho SEH explícito e o núcleo reutilizável de
  ambiente/locale/FLS: `tl_seh*.exe` e `tl_locale_env_fls.exe` cobrem os
  contratos sem declarar suporte aos benchmarks comerciais.
- [x] Validar `install -> arquivos no prefixo -> cadastro do executável
  instalado -> app run` com teste de integração e artefatos reproduzíveis.
- [x] Adicionar descoberta estrutural de formatos de distribuição: distinguir
  PE direto de pacotes MSIX/AppX, validar o arquivo, ler `AppxManifest.xml`,
  localizar o PE interno e instalar/relançar pacotes que contenham PE32+
  x86-64 nativo. O teste `integration_msix_install` cobre extração, catálogo
  e execução; .NET/Mono, bundles e assinatura Authenticode continuam fora do
  contrato.
- [x] Registrar imports, versão, hash e fluxo principal dos alvos acompanhados
  no catálogo e em `docs/requisitos-aplicativos.md`, usando recorrência de
  dependências para ordenar o trabalho; o preenchimento de amostras comerciais
  sem metadados completos continua sendo manutenção do inventário.
- [x] Expandir famílias de APIs somente quando a implementação servir a mais
  de um alvo ou completar uma capacidade delimitada; cada entrega recente tem
  fixture ou regressão de integração associada.
- [x] Priorizar e executar o núcleo comum na ordem publicada: locale ampliado,
  contexto de processo/console, enumeração de arquivos, identidade/ACL,
  controles GUI, automação, HTTP e confiança.
- [x] Manter o `--report` como porta de entrada, agrupando imports ausentes por
  DLL e capacidade e separando resolução estática de carregamentos dinâmicos e
  do fluxo efetivo de execução.
- [x] Avaliar o `RobloxPlayerInstaller.exe` como benchmark do portfólio sem
  criar stubs específicos; o aplicativo continua sem declaração de suporte.

### Sequência planejada a partir do portfólio local

Esta ordem usa somente as lacunas já registradas em
`docs/requisitos-aplicativos.md`. Cada item ainda precisa de um plano técnico
aprovado antes de começar; não autoriza implementar APIs extras por antecipação
nem declarar os benchmarks comerciais suportados.

#### Fase 13.7 — locale determinístico ampliado

- [x] Implementar `IsValidCodePage`, `IsValidLocale`, `GetLocaleInfoEx`,
  `EnumSystemLocalesW`, `GetStringTypeW`, `GetDateFormatW` e `GetTimeFormatW`
  sobre a mesma tabela estática `en-US` da Fase 13.6.
- [x] Manter a enumeração limitada a locales estáticos documentados, validar
  callbacks e flags e retornar erro controlado para sort keys, host locale,
  normalização, CJK e mutação de locale por thread.
- [x] Criar `tl_locale_extended.exe`, sem CRT implícito, para provar consulta,
  enumeração por callback, tipo de caractere e formatação; cobrir buffers,
  flags e callbacks inválidos em CTest.
- [x] Atualizar `--report` de WinRAR, Logitech G HUB e Rockstar somente depois
  dos testes. A fase só é concluída se reduzir lacunas nos três, sem executar
  binários comerciais.

#### Fase 13.8 — contexto de processo e console Win32

- [x] Implementar o grupo compartilhado `GetStartupInfoW`,
  `GetSystemDirectoryW`, `GetFileType`, `SetStdHandle`, `ReadConsoleW`,
  `WriteConsoleW`, `IsDebuggerPresent`, `IsProcessorFeaturePresent`,
  `EncodePointer`, `DecodePointer` e `InitializeSListHead`.
- [x] Definir o contrato para handles padrão por processo/prefixo e para o
  comportamento sem console, sem criar `AllocConsole`/`AttachConsole` nesta
  etapa.
- [x] Criar uma fixture de processo/console que valida dados de startup,
  redirecionamento, tipo de handle, codificação UTF-16 e operações de lista;
  proteger APIs, trace e execução em CTest.
- [x] Reanalisar WinRAR, Logitech G HUB e Rockstar apenas com `--report`.

#### Fase 13.9 — enumeração e metadados de arquivos x64

- [x] Completar as operações de arquivos que se repetem no portfólio:
  `FindFirstFileExW`, `SetFileAttributesW` e a extensão de metadados de handle
  justificada pelas amostras; long/short paths e APIs exclusivas ficam fora
  até aparecerem em outro alvo.
- [x] Reutilizar o mapeamento de caminhos e o prefixo existente, validando
  flags, estruturas, buffers e `GetLastError` sem expor caminhos do host.
- [x] Criar fixture de enumeração/metadados no prefixo e testes de isolamento
  entre prefixos; atualizar os relatórios de pelo menos dois benchmarks x64.

#### Fase 13.10 — identidade e ACLs funcionais por prefixo

- [x] Implementar uma representação coerente, limitada e persistente de SID,
  token, descritor de segurança e DACL para os arquivos do prefixo, cobrindo
  as operações comuns exigidas por Logitech G HUB, WinRAR e Rockstar.
- [x] Incluir somente APIs validadas pelo fluxo: consulta de token/SID,
  `Get/SetNamedSecurityInfoW`, `SetEntriesInAclW`,
  `InitializeSecurityDescriptor` e operações de SID/ACL associadas.
- [x] Criar fixture de segurança que consulta identidade e grava/lê uma DACL
  dentro do prefixo; provar que isso é compatibilidade funcional, **não**
  sandbox, autenticação do host ou aplicação real de permissões Linux.
- [x] Manter certificados, WinTrust, privilégios elevados, ACLs de rede e
  herança complexa fora desta fase.

#### Fase 13.11 — diálogos e controles GUI reutilizáveis

- [x] Promover somente o subconjunto compartilhado de `USER32`/`COMCTL32`
  necessário para diálogos modais, tabulação, textos/ícones e controles comuns
  observados em WinRAR e Rockstar.
- [x] Criar fixture X11 determinística com interação automatizada; não incluir
  GDI completo, impressão, shell de arquivos ou todos os controles Windows.
- [x] Reanalisar os dois benchmarks e só iniciar execução manual quando todos
  os imports estáticos e atrasados correspondentes estiverem resolvidos.
- [x] Projetar o `HDC` de controles lógicos na superfície X11 da janela
  principal, acumulando offsets de pais para `TextOut`, `FillRect` e
  `Rectangle`, com regressão para filhos aninhados e handles órfãos.
- [x] Modelar `CreateStatusWindowW` e `CreateToolbarEx` como controles lógicos
  filhos, validar parent/`TBBUTTON`, renderizar o estado mínimo na superfície
  compartilhada e encaminhar comandos básicos da toolbar por `WM_COMMAND`.
- [x] Processar o ciclo mínimo de mensagens `TB_*`/`SB_*` usado para montar e
  atualizar esses controles, com validação de buffers e regressões de
  `idCommand`, contagem, exclusão e texto UTF-16.
- [x] Roteiar eventos de mouse para filhos lógicos customizados encontrados pelo
  hit-test, preservando o `HWND` do filho e convertendo as coordenadas para o
  espaço local antes de despachar `WM_LBUTTONDOWN`/`UP` e `WM_MOUSEMOVE`.
- [x] Fazer `InvalidateRect` de uma janela lógica produzir um `WM_PAINT` na fila
  do próprio filho, com validação do `RECT`, deduplicação de repaints e flush da
  superfície X11 projetada.
- [x] Aceitar `TB_ADDBUTTONSW` e `TB_AUTOSIZE` no modelo de toolbar, usar a ordem
  real de `idCommand` do 7-Zip no shell visual e proteger o hit-test da faixa
  visual com regressão de `WM_COMMAND`.
- [x] Capturar o estado de pressão da toolbar visual: redesenhar no `Press`,
  manter o controle lógico durante a captura e cancelar a ação quando o
  `Release` ocorrer fora do botão pressionado; somente o `idCommand` original
  pode ser encaminhado.
- [x] Exibir feedback de hover na toolbar e nas linhas da lista do shell visual,
  limpar o destaque ao sair da área e manter esse estado independente da
  seleção; regressões cobrem entrada, troca e saída do ponteiro.
- [x] Exibir feedback de hover na árvore lateral sem alterar a pasta selecionada
  e limpar o destaque quando o ponteiro sai da navegação; regressão cobre o
  estado independente da seleção.
- [x] Carregar o menu de classe `RT_MENU` MENUEX v1 do 7-Zip, incluindo o
  `MENUHELPID` de popups, alinhamento, IDs, texto e submenus no modelo lógico;
  associá-lo à janela principal e proteger `LoadMenuW`/`GetMenuItemInfoW` com
  fixture de recurso.
- [x] Abrir o dropdown do menu real na superfície visual do 7-Zip, destacar
  itens no mouse e encaminhar a seleção de itens folha como `WM_COMMAND` ao
  `WNDPROC` da janela principal; `Up`/`Down`/`Enter`/`Escape` operam o menu
  pelo teclado. Mutações e submenus aninhados continuam fora do contrato. As
  regressões cobrem a seleção por mouse e por teclado de comandos folha.
- [x] Tornar a lista do shell explorável: clicar seleciona uma entrada e
  `Enter` ou duplo clique abre uma pasta no diretório Linux correspondente, com
  retorno visual para `..`; operações de arquivo e despacho de navegação ao
  convidado continuam fora do contrato. A árvore lateral também retorna à raiz
  visual e seleciona `Home`, `Desktop` e `Documents` Linux quando disponíveis.
  A barra `Address` aceita caminhos `Z:\...`, confirma somente diretórios dentro
  da raiz visual e rejeita destinos inválidos. As regressões usam um diretório
  temporário.

#### Fase 13.12 — automação, rede e confiança, em entregas separadas

- [x] Separar OLE Automation/streams, HTTP WinINet e
  certificados/WinTrust em subfases independentes, cada qual exigindo ao menos
  duas evidências do portfólio ou uma fixture de protocolo reproduzível.
- [x] Para OLE streams, limitar a primeira entrega a `CreateStreamOnHGlobal`
  com `IStream` em memória e ABI Microsoft x64 explícita: referências,
  `Read`/`Write`, `Seek`, `SetSize`, `Stat`, `Commit`/`Revert`; `tl_stream.exe`
  cobre metadados, `--report`, trace e execução.
- [x] Para HTTP, limitar a primeira entrega a cliente HTTPS previsível por
  prefixo, sem cookies globais ou credenciais do host: `tl_wininet.exe`
  cobre HTTPS em loopback com CA TLS efêmera, CA não confiável, protocolo,
  handles e leitura; proxy, DNS externo, Internet e redirecionamento são
  rejeitados ou inexistentes.
- [x] Para confiança, limitar a primeira entrega a `WinVerifyTrust` com
  `WTD_CHOICE_BLOB`, política sem UI/revogação e cadeia DER explícita
  folha→raiz; `tl_trust.exe` cobre metadados, `--report`, trace, sucesso,
  política incompatível e raiz incorreta. `WTD_CHOICE_FILE`, loja Windows,
  revogação e Authenticode continuam fora do contrato. A API
  separada `CRYPT32!CertGetNameStringW` é coberta por `tl_crypt32.exe` apenas
  para `CERT_CONTEXT`/DER explícito; `tl_wthelper.exe` cobre a travessia
  limitada de estado, signer e certificados folha/raiz.
- [x] Não usar esses componentes para declarar compatibilidade do Rockstar
  antes de validar um fluxo de instalação/atualização inteiro; a matriz atual
  mantém o alvo como execução não concluída.

#### Backlog condicionado — formatos, arquitetura e unwind adicional

- [x] MSIX/AppX: detectar pacote, ler `AppxManifest.xml` e localizar
  estruturalmente o executável interno; a validação de central directory,
  DEFLATE, CRC e limites está coberta por testes.
- [x] Instalação e execução de pacotes MSIX/AppX nativos exigiram a fase B8 e
  agora são cobertas pelo prefixo próprio; bundles, .NET/Mono e assinatura
  Authenticode continuam fora do contrato.
- PE32/x86, .NET/Mono, ARM e WOW64 continuam fora do alvo. Não há plano de
  executar esses binários sem uma decisão explícita de arquitetura/emulação.
- [ ] A forma de `UWOP_SET_FPREG` do Roblox (`OpInfo=10`, `FrameOffset=0`)
  permanece diagnóstico de portfólio. Só será promovida a uma fase de unwind
  genérica se outra amostra confirmar a mesma semântica e houver fixture
  determinística; não será criada uma exceção exclusiva para Roblox (item `B9`).

#### Fase 13.13 — Portfólio Aplicativos_Windows_Populares (2026-08-31 — ciclo A→D→B)

- [x] `7z.dll` `84/86→86/86` `USER32!CharPrevExA` + `KERNEL32!DosDateTimeToFileTime` — marco histórico; referências atuais ficam nos módulos em `src/runtime/dlls/` e no relatório em `src/cli/report.cpp`.
- [x] `HWiNFO64.exe` `20/28→28/28` `GDI32!Arc` `gdi32.cpp:1109` `SHLWAPI!PathIsUNCW` `shlwapi.cpp:328` + `MSIMG32!AlphaBlend` `NETAPI32!NetApiBufferFree` `OLEACC!LresultFromObject` `tdh!TdhGetPropertySize` `WINSPOOL.DRV!OpenPrinterW` `WTSAPI32!WTSFreeMemory` `winapi.cpp:782` `winapi.hpp:2072` — exec `ExitProcess 44544`
- [x] `RTSSHooks64.dll` `205/256→256/256` `GDI32 5` `USER32 7` `KERNEL32 16` `SHLWAPI 4` `WINMM 1` `SETUPAPI 7` + `delay DirectX 11` `winapi.cpp:782` `gdi32.cpp:1109` `user32.cpp:3769` `shlwapi.cpp:328` `winmm.cpp:70` `winapi.hpp:2072` — stubs `E_FAIL/S_OK` `module.cpp:1412,1545`
- [x] `RobloxPlayerInstaller.exe` `SIGSEGV 0x68 rva 0x39ab exit 71 → RBXCRASH Worker,28 exit 3` — `TLS slot 0x430==NULL` `objdump 0x1400039ab` `teb.hpp:92` `pe_reader.cpp:685` `template 0x88c rva 0xb6a520` `winapi.cpp:751` `*TLS(0x430)=base+0xc2c800` após `invoke_thread_tls_callbacks`

#### Fase 13.14 — TLS genérico e Worker RSL (Roblox) — subetapa concluída

- [x] Generalizar o contrato do slot pointer-backed `TLS 0x430` com validação do span raw + `SizeOfZeroFill`, alocação sob demanda de bloco `0x1000` zerado, tabela por TEB e liberação no encerramento; `tl_tls_generic.exe` cobre `TLS zero-init` + leitura do ponteiro + `mov 0x68(%rax)` sem `SIGSEGV` (Debug: teste unitário e 4 CTest passaram; fixture emite registros PE TLS explícitos sem CRT)
- [x] Implementar e validar o subconjunto Linux reutilizável observado no Worker: `GetAdaptersInfo`/`GetAdaptersAddresses`/`if_nametoindex` via `getifaddrs`, `CertOpenStore` com provedores controlados e `WTSEnumerateSessionsW`/`WTSFreeMemory`; `tl_worker_rsl.exe` cobre `WSAStartup`/`getaddrinfo`, interfaces IPv4, loja em memória e sessão local (Debug: 10 testes unitários passaram, 1 skip controlado sem IPv4 + 4 CTest de fixture)
- [x] Reexecutar a fixture `tl_worker_rsl.exe` com os canais válidos
  `--trace=pe,imports,runtime,process,crt` (`process/isolate.cpp:155`): os
  `14/14` imports foram resolvidos e a execução parou de forma controlada em
  `GetAdaptersAddresses` (`ERROR_NO_DATA`, exit `77`) porque este host não
  expõe interface IPv4. Não surgiu uma API adicional justificada; qualquer
  expansão continua exigindo evidência, fixture e registro em
  `docs/requisitos-aplicativos.md`.
- [x] Manter `Roblox` como benchmark sem criar stubs exclusivos; a resolução
  de imports, o TLS genérico e a fixture Worker/RSL estão registrados sem
  promover o aplicativo a suportado.
- [ ] Declarar `Roblox` como `supported` somente quando `install --prefix`
  extrair `drive_c` e `app run` completar sem `panic`, após a amostra comercial
  correspondente estar disponível (item `B6`).

PE32/x86 e .NET/Mono continuam requisitos separados nesta primeira subetapa.
MSIX/AppX nativo avançou pela B8 somente para PE32+ x86-64; bundles, assinatura
Authenticode e demais arquiteturas ficam registrados para expansão posterior,
mas não bloqueiam a base de instalação nativa.

### Critério de saída

O portfólio contém alvos de pelo menos três classes de uso, cada um com fluxo
principal automatizado e limitações publicadas. O runtime demonstra que novas
famílias de APIs atendem mais de um alvo ou uma capacidade reutilizável, e o
catálogo distingue honestamente o que inicia, o que executa o fluxo principal e
o que ainda não é suportado.

## O que fica explicitamente fora do estágio atual

- Jogos, DirectX, drivers, anti-cheat, .NET, COM/ActiveX amplo, automação
  `IDispatch` e serviços Windows, até que exista decisão explícita, alvo
  concreto e fase própria. Os fixtures mínimos de `ole32.dll` e streams em
  memória não representam suporte geral a COM.
- Implementar centenas de APIs sem aplicativo-alvo e regressão.
- Declarar suporte porque o programa abriu; o fluxo principal precisa ser verificável.

## Objetivo estratégico — compatibilidade ampla por etapas

O objetivo do projeto é tornar o TradutorLinux útil para aplicativos Windows em
geral, aumentando continuamente a quantidade, as categorias e o tamanho dos
aplicativos que funcionam no Linux. Isso inclui chegar progressivamente a
aplicativos grandes, desde que suas dependências possam ser implementadas com
segurança e testadas.

“Aplicativos em geral” é um objetivo de cobertura, não uma declaração de que
qualquer `.exe` já funciona. O progresso será medido por dados: aplicativos
reais testados, categorias cobertas, imports implementados, fluxos principais
aprovados, resultados corretos e falhas reproduzíveis. Não será medido por
quantidade de APIs declaradas sem uso real.

### Estratégia de expansão

- [x] Criar um catálogo de aplicativos reais por categoria: console, arquivos, rede, ferramentas de desenvolvimento, produtividade e GUI (`docs/catalog.md` com `xxd`/`bzip2`/`dos2unix`/`tl_*`/`simple_todo`/`Roblox`).
- [x] Manter níveis funcionais de compatibilidade: analisado, inicia, fluxo
  principal restrito, fluxo principal, uso diário e cobertura avançada
  (definidos em `docs/catalog.md`); resolução de imports permanece uma dimensão
  separada.
- [x] Coletar imports de muitos aplicativos e priorizar APIs que aparecem em vários alvos (`Roblox` `430` imports, `gdiplus` `8/8`, `SHELL32` `5/5`).
- [x] Implementar famílias de DLLs por demanda: `KERNEL32`, `NTDLL` limitada, `ADVAPI32`, `USER32`, `GDI32`, `SHELL32`, `OLE32`, `COMDLG32`, `WS2_32`, `WININET`, `WINTRUST`, `CRYPT32` e CRTs (23 módulos `tests/test_module.cpp:87`).
- [x] Criar testes de integração por aplicativo e uma matriz pública de limitações (`docs/compatibilidade.md` + `tests/samples` 36 fixtures).
- [x] Adicionar execução isolada, timeout e diagnóstico para que aplicativos grandes não derrubem o host (`process/isolate.cpp` `71`/`72`).
- [x] Definir e implementar limites efetivos de CPU/RAM por aplicativo; o
  isolamento de processo e o timeout não equivalem a contenção de recursos.
  `--cpu`/`--memory`, `RLIMIT_CPU`/`RLIMIT_AS` e a herança em processos-filhos
  são validados pela B1.
- [x] Avaliar compatibilidade por versões e builds específicos, sem assumir que
  duas versões do mesmo aplicativo usam as mesmas APIs; os targets reproduzíveis
  são fixados por versão, commit/hash, arquitetura e toolchain.

### Etapas para aplicativos grandes

1. **Base de execução:** PE, relocations, imports, TLS, exceções, processo,
   argumentos, ambiente, heap e CRT.
2. **Sistema operacional básico:** arquivos, diretórios, Unicode, registry
   limitado, sincronização, threads, processos filhos e tempo.
3. **Bibliotecas comuns:** shell, diálogos, controles, recursos, clipboard,
   fontes, GDI e rede.
4. **Aplicativos de médio porte:** ferramentas com múltiplas DLLs, plugins,
   configuração e vários threads.
5. **Aplicativos grandes:** GUI complexa, instaladores, suítes de produtividade
   e outros alvos escolhidos por cobertura e valor.
6. **Recursos especializados:** COM, DirectX, áudio, impressão e .NET somente
   quando houver decisão explícita de escopo e aplicativos-alvo.

Cada etapa depende da anterior. Um aplicativo grande não será considerado
suportado por simplesmente abrir a janela: ele precisa concluir operações
representativas sem corrupção, travamento ou resultado incorreto.

## Marcos de referência (histórico)

Os marcos abaixo registram a sequência já cumprida do projeto. Eles não são a
fila atual de trabalho; novas tarefas devem entrar somente no backlog
consolidado e seguir a ordem do próximo ciclo indicada adiante.

| Marco | Resultado verificável |
|---|---|
| M1 — Parser | PE32+ válido lido; PE inválido rejeitado com segurança. |
| M2 — Image mapper | Imagem mínima mapeada, relocada e protegida. |
| M3 — Imports | Imports conhecidos resolvidos; ausentes diagnosticados. |
| M4 — Console | `tl_hello.exe` executa com saída e retorno corretos. |
| M5 — Arquivos | Fixture lê e escreve arquivo com semântica documentada. |
| M6 — Cobertura | Primeira aplicação-alvo adicional incluída na matriz e na regressão. |
| M7 — GUI | Janela real com message loop (`tl_win.exe`) e decisão de produto tomada: GUI Win32 mínima segue como objetivo experimental. |
| M8 — Diagnóstico controlado | Convidado executado em processo filho isolado; término por sinal vira `guest-signal` no trace e `71` no exit code; falhas controladas (ponteiro, arquivo, memória, imports) cobertas por testes. |
| M9 — Alvos reais | Três aplicativos pequenos e úteis executam fluxos completos com regressão no CI. |
| M10 — CRT mínimo | Um aplicativo compilado com CRT recebe argumentos, usa ambiente e termina corretamente. |
| M11 — Arquivos reais | Um aplicativo cria, enumera e manipula arquivos e diretórios usando caminhos traduzidos. |
| M12 — GUI real | Um aplicativo GUI escolhido por seus imports completa um fluxo principal sob X11. |

Com o marco M7 concluído, a GUI mínima avançou além do plano original:
teclado estendido (`WM_KEYUP`, virtual keys, `Shift`), timers (`WM_TIMER`) e
GDI mínimo já possuem fixtures e regressões. Qualquer expansão visual futura
depende de um alvo escolhido em `B5` ou `B7`; não é uma tarefa implícita do
roadmap.

## Backlog consolidado

Este inventário reúne as pendências históricas de `PROXIMAS-ETAPAS.md`, dos
registros de análise, `ideia.md`, `docs/propostas-evolucao.md` e da proposta de
reorganização. As pendências atuais foram separadas no `ROADMAP.md` da raiz.
Uma tarefa fica pronta somente com a evidência exigida na definição de pronto
abaixo.

### Decisões registradas — 2026-09-05

Estas decisões encerram a triagem do ciclo e orientam o backlog. Elas não
marcam itens condicionais como concluídos; definem os gates para retomá-los.

- **Direção do produto:** a prioridade continua sendo o runtime Win32, o
  loader, a ABI, memória, imports, diagnóstico e fluxos reproduzíveis do
  portfólio. **B2 — decisão confirmada pelo usuário em 2026-09-05:** fica
  adiada e não terá implementação até existir uma decisão explícita de produto
  para tradução de interface, incluindo formato, diretório, identificação,
  idioma, precedência e fallback.
- **B6 e Roblox — decisão confirmada pelo usuário em 2026-09-05:** manter a
  etapa adiada. O runtime não declara `Roblox` como `supported`; a fixture
  `tl_worker_rsl.exe` não substitui o executável comercial `Worker`/`RSL`.
  B6 só será reaberta com a amostra exata, hash registrado, `install --prefix`,
  `app run`, trace e efeitos observáveis reproduzíveis.
- **B9 e unwind — opção 1 confirmada pelo usuário em 2026-09-05:** manter a
  etapa adiada. Não será criada uma exceção exclusiva para Roblox. A forma não
  canônica de `UWOP_SET_FPREG` só entra após outra aplicação confirmar a mesma
  semântica e uma fixture determinística proteger o comportamento.
- **B11 e objetos — opção 1 confirmada pelo usuário em 2026-09-05:** manter a
  etapa adiada. As tabelas separadas de arquivos, sincronização, threads e
  mapeamentos permanecem. O cabeçalho comum só será iniciado por uma regressão
  de handle misturado ou por uma medição concreta de benefício de manutenção.
- **B13 e codepages — opção 1 confirmada pelo usuário em 2026-09-05:** manter
  como estão os codepages já exigidos pelo portfólio (`0`, `1252`, `437` e
  `65001`). A extração para dados gerados só será feita quando um alvo exigir
  outra página, com fonte versionada e testes de conversão e erro.
- **B14 e extensões por aplicativo — decisão confirmada pelo usuário em
  2026-09-05:** manter as DLLs genéricas internas como fallback e permitir
  extensões PE32+ AMD64 selecionadas explicitamente por perfil, isoladas por
  aplicativo e prefixo. `TL_DLL_OVERRIDES` continua separado do mecanismo de
  perfis. Não são aceitos bibliotecas Linux, scripts ou código nativo arbitrário
  no perfil; DLLs PE convidadas executam com os privilégios do runtime, que não
  é sandbox.
- **Auditoria do 7-Zip — decisão preservada em 2026-09-05:** o 7-Zip 24.08 não
  apresentou uma necessidade reproduzível de regra condicional além dos
  arquivos auxiliares da B14.3. A extensão geral de DLL não cria regra
  específica para o 7-Zip; `7-Zip::FM` continua no shell GUI experimental e
  `TL_7ZFM_COPY_DESTINATION` continua somente hook de teste.
- **Backend Proton — decisão confirmada pelo usuário em 2026-09-05:** registrar
  o Proton como backend opcional do aplicativo, sem reimplementá-lo do zero e
  sem substituir o runtime próprio. O backend será selecionado por aplicativo,
  terá prefixo e diagnóstico próprios, e só será promovido após validação
  reproduzível de versão, dependências, execução, exit code e limitações. O
  Roblox é um alvo candidato, não uma promessa de suporte; a integração deverá
  respeitar as restrições do aplicativo e registrar qualquer bloqueio externo.
- **B17 e processos — opção 1 confirmada pelo usuário em 2026-09-05:** manter
  o supervisor adiado. O protocolo atual de `fork`/`waitpid`/pipe é suficiente
  para o portfólio conhecido; um supervisor só será criado se um aplicativo
  exigir estado compartilhado além desse protocolo.
- **B18 e SEH — opção 1 confirmada pelo usuário em 2026-09-05:** manter
  adiado o suporte avançado. Permanece o subconjunto atual de exceções/unwind
  x64. C++, `__finally` e outras extensões só entram com aplicativo-alvo,
  contrato e fixture; sinais Linux continuam sendo tratados pelo isolamento do
  processo.
- **B19 e novas famílias — opção 1 confirmada pelo usuário em 2026-09-05:**
  manter threadpool, ALPC e outras famílias abundantes adiadas. Cada uma
  exigirá um alvo, um subconjunto pequeno, trace, matriz e regressão.
- **Rust — decisão confirmada pelo usuário em 2026-09-05:** adotar Rust
  seletivamente nas partes de parsing, validação, segurança de caminhos,
  materialização, pacotes, hashes e assinaturas, sem reescrever o runtime por
  preferência tecnológica. A execução do convidado, a ABI Microsoft x64, as
  pontes Win32, callbacks e GUI permanecem em C++ enquanto não houver alvo e
  benefício medidos. A integração usará uma fronteira C estável, com tipos
  opacos, códigos de erro e buffers explícitos; não atravessarão a fronteira
  exceções C++ nem tipos da STL/Rust.
- **Gate comum:** toda retomada de item condicionado precisa registrar no
  roadmap o alvo ou fixture, o contrato, a limitação, os testes unitários e de
  integração, a atualização da matriz e a validação relevante antes de ser
  marcada como concluída e receber seu commit.

### B4 — etapa concluída

- [x] **B4 — Uniformizar o inventário de aplicativos.** Atualizar
  `docs/requisitos-aplicativos.md` para que toda medição informe data, hash,
  versão, arquitetura, ferramenta/versão e se foi somente `--report` ou
  execução. O snapshot canônico marca explicitamente dados não registrados,
  corrige as entradas atuais e alinha `docs/catalog.md`, a matriz e o contrato
  de diagnóstico; nenhuma compatibilidade foi promovida por inferência.

### B5/B7 — etapas concluídas

[x] **B5 — Aprofundar um fluxo real versionado.** O representante autorizado
`7zFM_x64.exe` 24.08 tem versão, hash, dependência `7z.dll`, manifest em
`tests/targets/manifests/7zfm_24.08.json`, entrada na matriz e limitações
publicadas. O driver externo `seven_zip_smoke` valida o fluxo restrito de
selecionar um arquivo, copiar para um diretório existente dentro da raiz e
encerrar com exit `0`.

[x] **B7 — Tornar o shell visual do 7-Zip um fluxo funcional restrito.** A
fixture cobre submenus aninhados e navegação por teclado; o shell real cobre
lista, seleção, navegação visual, toolbar e `Copy` (`546`) opt-in, sem
sobrescrita e com confinamento à raiz visual. O teste externo versionado
`build/debug/tests/seven_zip_smoke` confirma a cópia e o encerramento normal;
as demais operações do File Manager continuam limitadas.

### Ciclo concluído — B1, B8, B10, B12, B15 e B16

B1, B8, B10, B12, B15 e B16 foram concluídas e validadas. B2 é uma trilha de produto separada
do runtime Win32 e fica estacionada até haver decisão explícita sobre tradução
de interface. B6 e B9 permanecem condicionadas, respectivamente, à amostra
comercial Worker/RSL e a evidência de outra aplicação para o unwind.

**Decisão registrada:** o 7-Zip File Manager 24.08 foi escolhido porque já
possui amostra PE32+ x86-64 local, 298/298 imports resolvidos, classe Win32
identificada, fixture de contratos GUI e shell visual experimental. O escopo
do fluxo será navegação confinada ao prefixo, seleção de arquivo e uma
operação de arquivo verificável; abertura da janela ou aumento de cobertura de
imports não encerra B5.

- [ ] **B2 — Arquivos de tradução isolados por aplicativo.** Se a tradução de
  interface for confirmada como prioridade de produto, criar uma trilha
  independente do loader e das APIs Win32 para programas de terceiros. Definir
  formato, diretório, identificação por aplicativo (ID/hash/versão), seleção de
  idioma, precedência, fallback e comportamento para arquivo ausente ou
  inválido. A aplicação deve ser opt-in, isolada por aplicativo e validada com
  um programa externo de teste; não misturar essa camada ao contrato do runtime.
- [x] **B1 — Limites de CPU e RAM por aplicativo.** O contrato usa `--cpu
  <segundos>` e `--memory <MiB>` (zero desabilita o limite) no modo direto,
  `app add`, `app run` e `install`; valores persistentes ficam em
  `library.json`, com override explícito no `app run`. O filho isolado aplica
  `RLIMIT_CPU`/`RLIMIT_AS`, e seus processos Win32 descendentes herdam os
  limites pelo `fork`. `SIGXCPU` retorna `73` e traceia
  `guest-resource-limit`; falha de instalação retorna `70`. As fixtures
  `tl_hang.exe`, `tl_memory_limit.exe` e `tl_process_limit_parent.exe`, os
  testes de CLI/catálogo e o CTest direcionado comprovam CPU, memória, herança
  e diagnóstico. A contenção não é sandbox.
- [x] **B10 — Fechar a validação de recursos X11 com LeakSanitizer.** O
  `x11_popup_smoke` do build `sanitize` foi executado fora de `ptrace`, sob Xvfb
  próprio, com `detect_leaks=1`; passou com desenho repetido de 512 cores e os
  caminhos Escape, clique externo, destruição externa e timeout, sem relatório
  de ASan/LSan. O CTest mantém essa configuração automaticamente para builds
  com `TL_ENABLE_SANITIZERS`.
- [x] **B8 — Instalação e execução de MSIX/AppX nativo.** O comando
  `install` aceita `.msix`/`.appx`, lê o manifesto, extrai somente um pacote
  ZIP validado para `Program Files/<id>`, seleciona o executável declarado,
  exige PE32+ x86-64, grava o catálogo e permite `app run`. A fixture
  `native-fixture.msix` e o teste `integration_msix_install` comprovam
  `--report`, extração, registro e execução. O extrator rejeita traversal,
  symlinks, colisões, métodos ZIP desconhecidos, CRC inválido e limites
  excedidos; .NET/Mono, bundles e assinatura Authenticode continuam fora.

### Estacionadas até haver condição explícita

- [ ] **B6 — Reexecutar Worker/RSL e concluir o caso Roblox.** Localizar ou
  receber a amostra comercial exata, registrar hash e repetir
  `install --prefix` seguido de `app run` com trace. Só promover o estado para
  `supported` quando o fluxo terminar sem `panic`, com saída e efeitos
  observáveis corretos. Esta tarefa está bloqueada nesta cópia porque não há
  executável comercial `Worker`/`RSL`; `tl_worker_rsl.exe` é apenas a fixture
  reutilizável e o exit `77` sem IPv4 continua sendo skip controlado.
- [ ] **B9 — Generalizar `UWOP_SET_FPREG` somente por evidência.** Só aceitar a
  forma não canônica observada no Roblox (`OpInfo=10`, `FrameOffset=0`) após
  outra amostra confirmar a mesma semântica e existir fixture determinística.
  Não criar uma exceção exclusiva para o Roblox.

### Evolução condicionada a alvo ou benefício medido

- [x] **B11 — Cabeçalho comum para objetos e handles.** Padronizado `ObjectHeader`
  com `HandleObjectType` e `ref_count` para arquivos, mapeamentos de memória,
  objetos de sincronização (eventos, mutexes, semáforos), threads, snapshots e handles
  de busca (`Find`). Fechamento unificado em `CloseHandle` respeitando contagem de
  referência (`DuplicateHandle`) e rejeitando handles misturados (ex.: proibir fechar
  `FindFirstFile` via `CloseHandle` em vez de `FindClose`). Validado com testes unitários
  dedicados (`Win32HandleObjectTest`).
- [x] **B12 — `VirtualQuery` coerente com alocações do runtime.** As alocações
  próprias agora registram `MEM_RESERVE`/`MEM_COMMIT`, `AllocationBase`,
  `AllocationProtect` e proteções por região; `VirtualProtect` divide a tabela
  quando altera uma faixa parcial e `/proc/self/maps` fica como fallback para
  mapeamentos externos. A fixture `tl_virtual_query.exe` valida reserva,
  commit, mudança de proteção, `RegionSize`, `Type` e liberação; unitário,
  metadata, `--report` e execução passam em Debug.
- [x] **B13 — Tabelas de codepage versionadas como dados.** Extraídas e expandidas
  tabelas de codepage com dados de fontes públicas para CP 1250 (Central European),
  CP 1251 (Cyrillic) e CP 28591 (ISO-8859-1 Latin-1), além de CP 0/1252/437/65001.
  Suportadas em `MultiByteToWideChar`, `WideCharToMultiByte`, `GetCPInfo` e
  `IsValidCodePage`, com busca reversa constexpr O(log N) e testes unitários de
  conversão e ida-e-volta (`Win32CodePageTest`).
- [x] **B14 — Perfil de compatibilidade por aplicativo e extensões
  condicionados.** Cada prefixo poderá ter uma área `compat/` ao lado de
  `drive_c`: `drive_c` mantém os arquivos reais do convidado, enquanto
  `compat/` guarda os arquivos auxiliares e o perfil do TradutorLinux daquele
  aplicativo, por exemplo `compat/profile.json` e `compat/files/`. O perfil
  deverá ser selecionado pelo ID estável do catálogo, com hash ou versão
  opcional quando necessário, e poderá declarar somente recursos validados,
  como arquivos e DLLs PE32+ personalizadas. A seleção de backend Proton
  permanece planejada separadamente na B14.6. A pasta não será automaticamente
  visível ao aplicativo.
  Bibliotecas Linux, scripts e código nativo arbitrário continuarão proibidos;
  DLLs PE convidadas exigirão isolamento por prefixo, precedência, fallback,
  diagnóstico e validação próprios. Sem extensão aplicável, usa-se o
  comportamento genérico. Diferenciar esse mecanismo do override global já
  existente (`TL_DLL_OVERRIDES`) e exigir alvo, fixture e regressão antes de
  permitir qualquer divergência específica por API.

  Subetapas executadas, na ordem:

  1. [x] **B14.1 — Contrato e layout.** O contrato v1 está documentado em
     `docs/arquitetura/perfis-compatibilidade.md`. Cada prefixo cria `compat/`
     e `compat/files/`; `profile.json` exige schema e ID, aceita hash/versão
     opcionais e declara arquivos com origem confinada a `compat/files/` e
     destino `C:\...` confinado a `drive_c`. O parser, a validação estrita, o
     SHA-256 do executável no catálogo, o fallback genérico com aviso e o trace
     têm regressão unitária. A exposição efetiva ao convidado permanece na
     B14.3.
  2. [x] **B14.2 — Descoberta e validação.** A árvore do perfil é criada no
     prefixo, o perfil é carregado somente para o aplicativo correspondente,
     schema, identidade e caminhos são validados, e o resultado é emitido no
     trace. Os testes unitários e a integração de `app run` protegem o
     fallback para perfil ausente ou inválido e a contenção em `compat/` e
     `drive_c`. A exposição efetiva dos arquivos permanece na B14.3.
  3. [x] **B14.3 — Exposição controlada de arquivos.** Um perfil válido do
     `app run` copia cada arquivo regular de `compat/files/` para o destino
     `C:\\...` dentro de `drive_c`, sem sobrescrever destinos existentes ou
     seguir symlinks de origem/destino. Diretórios-pai ausentes são criados
     temporariamente, cópias parciais são desfeitas e a limpeza remove apenas
     os caminhos criados pela execução, preservando substituições e dados
     novos do convidado. A fixture `tl_compat_file.exe`, os testes unitários,
     o trace e a integração reproduzem cópia, colisão, rollback e limpeza. A
     pasta `compat/` não é exposta automaticamente.
  4. [x] **B14.4 — Extensões de DLL por aplicativo.** O schema v2 mantém
     perfis v1 válidos e permite apenas mapeamentos explícitos de módulos PE32+
     AMD64 para `compat/dlls/`, sem descoberta automática, scripts ou
     bibliotecas Linux. O `GuestModuleGraph` é privado por execução e aplica
     relocations, W^X, exports por nome/ordinal/forwarder, imports estáticos e
     delay imports eager, dependências, TLS, `DllMain`, `LoadLibrary`,
     `GetProcAddress`, `FreeLibrary`, refcounts, ciclos e unload determinístico.
     A precedência é perfil → `drive_c` → genérico, inclusive por export; um
     provider inválido ou com attach rejeitado é descartado inteiro e usa
     fallback disponível. A fixture PE32+ `tl_compat_dll_app.exe`, suas duas
     variantes, a dependência `compatdep.dll`, TLS callback e a integração
     `integration_compat_dll_profile` reproduzem execução, fallback, isolamento,
     fonte preservada, ausência de cópia em `drive_c`, trace e exit code.
     A auditoria do 7-Zip continua sem regra específica e permanece separada
     do mecanismo geral.
  5. [x] **B14.5 — Integração e promoção.** A integração
     `integration_compat_profile_isolation` reutiliza `tl_compat_file.exe` com
     dois IDs de catálogo e dois prefixos independentes. Perfis com conteúdos
     distintos no mesmo destino Windows são executados sequencialmente e
     confirmam isolamento, conteúdo correto, fontes preservadas, limpeza dos
     destinos e ausência de `compat/` dentro de `drive_c`. A integração
     `integration_compat_profile` mantém a evidência de perfil carregado,
     ausente e inválido com fallback genérico, trace e exit code preservado.
     A matriz de compatibilidade foi atualizada. Com as subetapas aplicáveis
     validadas por testes reproduzíveis, a base de perfis foi integrada e
     promovida; o runtime usa comportamento genérico quando não houver extensão
     aplicável.
  6. [x] **B14.6 — Backend Proton opcional.** Integrar uma
     distribuição Proton versionada como backend separado para aplicativos que
     exigem uma camada Win32/DirectX maior que o subconjunto próprio, mantendo
     o runtime TradutorLinux como backend padrão e preservando a seleção por
     aplicativo. O contrato, a validação, o staging isolado e o piloto com
     mock foram implementados e o piloto real foi promovido. A matriz de
     componentes reais controlados
     agora cobre D3D11→DXVK/Vulkan, D3D12→VKD3D-Proton/Vulkan, entrada de
     janela e XAudio2, todos validados em Debug e Sanitize com Proton
     Experimental. A B14.6.6 também confirmou dois aplicativos cadastrados em
     prefixos independentes, com fallback nativo preservado e sem vazamento de
     arquivos. O Proton não
     será reimplementado do zero nem carregado como biblioteca Linux
     arbitrária a partir de `compat/`.

     Subetapas:

     - [x] **B14.6.1 — Contrato de seleção.** Definir no perfil/catálogo a
       escolha explícita entre runtime próprio e Proton. O schema 3 aceita
       `backend.kind` como `native` ou `proton`, sem `auto`; perfil sem o campo
       mantém `native`. A instalação fica em configuração externa
       (`backends.json`), com `TL_PROTON_ROOT` somente para testes. `files[]`
       pode ser usado no Proton, mas `dlls[]` continua exclusivo do loader
       próprio; Proton ausente, inválido ou incompatível retorna erro explícito
       sem fallback silencioso. Documentado em
       `docs/arquitetura/perfis-compatibilidade.md` e protegido pelos testes de
       parsing de perfil.
     - [x] **B14.6.2 — Descoberta e validação.** Localizar uma instalação
       configurada do Proton, validar executável, arquitetura, versão, hash e
       componentes necessários, sem download silencioso nem dependência
       implícita do Steam. `load_config` aceita o arquivo externo, valida o
       inventário SHA-256 opcional e `TL_PROTON_ROOT` substitui a raiz somente
       no processo de teste/diagnóstico.
     - [x] **B14.6.3 — Execução isolada.** Criar e controlar um prefixo Proton
       separado, preparar `WINEPREFIX`, ambiente, diretório de trabalho,
       argumentos, drives e arquivos do aplicativo, sem misturar o processo
       Proton com o contexto interno do runtime próprio. A árvore do aplicativo
       é sincronizada somente de `drive_c` para `proton/compatdata/pfx`, sem
       sobrescrever conflitos e sem alterar o prefixo nativo.
     - [x] **B14.6.4 — Diagnóstico e ciclo de vida do adaptador.** Registrar
       seleção, versão, staging, launcher, limpeza, término, timeout, sinal e
       exit code; preservar `stdout` do convidado e prefixar os logs do
       launcher em `stderr` com `[tl][proton]`. O mock também protege grupo de
       processos e limites herdados. Componentes gráficos não são declarados
       nesta subetapa.
     - [x] **B14.6.5 — Componentes gráficos e dependências.** Validar de forma
       incremental Vulkan, DXVK, VKD3D-Proton, entrada, áudio e demais
       dependências somente quando um aplicativo-alvo exigir cada componente.
       Os slices gráficos controlados estão validados: `tl_graphics_probe.exe`
       cria uma janela X11, inicializa D3D11, apresenta um frame e retorna `0`
       em `integration_proton_graphics`; `tl_d3d12_probe.exe` cria dispositivo,
       fila, command list, fence, swapchain flip de dois buffers e `Present`,
       retornando `0` em `integration_proton_d3d12`. Ambos passam com Proton
       Experimental sob Xvfb e comprovam somente D3D11→DXVK/Vulkan e o caminho
       controlado D3D12→VKD3D-Proton/Vulkan. A entrada de janela também está
       validada por `tl_input_probe.exe` e `integration_proton_input`, que
       confirma movimento, clique, `WM_KEYDOWN/CHAR/UP`, stdout, exit `0` e
       limpeza do prefixo com o driver X11/XTest. O áudio também está validado
       por `tl_audio_probe.exe` e `integration_proton_audio`: a fixture cria
       engine XAudio2, vozes master/source, submete buffer PCM, inicia/paralisa
       a reprodução e retorna `0`; o teste passa em Debug e Sanitize com o
       Proton Experimental e o servidor de áudio do host. A evidência cobre
       somente esses slices controlados, não fidelidade perceptual, codecs,
       raw input, gamepad/XInput ou suporte amplo por instalar o backend.
     - [x] **B14.6.6 — Piloto real e promoção.** A fixture PE32+
       `tl_proton_probe.exe` e o teste `integration_proton_backend` já cobrem o
       piloto controlado com mock, incluindo seleção, staging, ambiente,
       limpeza e ausência de fallback. O teste real
       `integration_proton_graphics` já prova a execução de uma fixture em uma
       instalação Proton configurada. A integração real
       `integration_proton_isolation` reutiliza `tl_compat_file.exe` com os IDs
       `proton-isolation-a` e `proton-isolation-b`, dois prefixos independentes
       e conteúdos distintos para o mesmo destino Windows; em Debug e Sanitize
       cada execução observou somente sua fonte, retornou `0`, preservou a
       origem, removeu o destino temporário e manteve staging/manifesto isolados.
       As limitações do backend e dos componentes estão publicadas na matriz e
       no diagnóstico. Roblox continua apenas exploratório e não é marcado como
       suportado: ainda exige execução reproduzível de launcher, rede, gráficos
       e bloqueios do fornecedor.
- [x] **B15 — Drives do prefixo como symlinks ou mecanismo equivalente.** O
  prefixo cria `dosdevices/c:` → `../drive_c` e `dosdevices/z:` → `/`; a
  resolução de `C:` canoniza e confina o caminho ao `drive_c`, enquanto `Z:`
  representa explicitamente o sistema de arquivos externo e não é sandbox.
  O fluxo de instalação e o shell visual do 7-Zip exercitam `Z:\...`; a
  regressão `PrefixTest.DriveLinksAreExplicitAndKeepCDriveConfined` verifica os
  links, a contenção e a rejeição de traversal.
- [x] **B16 — Expectativas explícitas nos fixtures.** O argumento CMake
  `KNOWN_LIMITATION` vincula uma expectativa de rejeição à matriz de
  compatibilidade; `verify_known_limitation.cmake` falha se o fixture ou o
  marcador desaparecer da matriz. O teste de runtime continua separado e
  falha quando o caso antes rejeitado passa a ter sucesso, evitando mascarar
  uma mudança de comportamento. `tl_missing_dll.exe` é a primeira aplicação
  desse contrato com `unknown-symbol`.

### Longo prazo, somente com decisão explícita

- [ ] **B17 — Supervisor de objetos entre processos convidados.** Considerar
  um supervisor para herança de handles, processos e threads somente se um
  aplicativo-alvo exigir estado compartilhado além do protocolo atual de
  `fork`/`waitpid` e pipe.
- [ ] **B18 — SEH estruturado além do subconjunto atual.** Incluir unwinding e
  exceções adicionais, como C++/`__finally`, apenas com aplicativo-alvo,
  contrato x64 e fixtures; sinais Linux continuam sendo tratados pelo
  isolamento do processo.
- [ ] **B19 — Novas famílias abundantes de API.** Threadpool, ALPC e qualquer
  outra família só entram quando um alvo justificar o subconjunto, com teste,
  trace e matriz. Fibras já entregues não devem voltar ao backlog.
- [x] **B20 — Adoção seletiva de Rust (concluída).** A B20 promove somente a
  validação lexical opt-in de caminhos no fluxo `app run`; parser, loader,
  APIs Win32, materialização física e runtime continuam em C++. A fronteira
  FFI, a toolchain reproduzível, os testes de robustez e a integração
  operacional foram validados sem criar dependência Rust para o build padrão.
  Não houve migração ampla nem declaração de compatibilidade adicional para
  aplicativos reais. O plano específico dos próximos candidatos está em
  [ROADMAP-RUST.md](ROADMAP-RUST.md).

  Subetapas:

  - [x] **B20.1 — Fronteira e contrato FFI.** Foi criada uma `staticlib` Rust
    mínima, ligada ao probe C++ somente quando `TL_BUILD_RUST=ON`, com
    `extern "C"`, handle opaco, buffers caller-owned, códigos de erro e
    liberação no mesmo lado que alocou. O contrato documenta ownership,
    UTF-8/UTF-16, tamanhos, limites, panics e concorrência. O probe cobre
    argumentos nulos, truncamento, múltiplos handles e chamadas concorrentes e
    passou em Debug, Sanitize e Release; `TL_BUILD_RUST=OFF` continua sem
    requisito Rust. Nenhum componente de produção foi migrado.
  - [x] **B20.2 — Toolchain reproduzível.** Cargo e CMake agora usam a
    toolchain Rust `1.97.1` fixada em `rust-toolchain.toml`, com `Cargo.toml`,
    `Cargo.lock` versionado e builds `--locked --offline` sem crates externas.
    Os presets `debug-rust`, `sanitize-rust` e `release-rust` preservam os
    presets C++ normais sem Rust; o CI instala a versão fixa e executa o probe
    nos três perfis. O probe passou nos três builds locais e o runtime C++ foi
    compilado com `TL_BUILD_RUST=OFF`. A suíte local isolada por prefixo passou
    em 653/655 testes; duas expectativas preexistentes de fixtures ainda
    esperam `unknown-symbol`, enquanto o runtime atual informa `unknown-dll`.
    A divergência foi reconciliada posteriormente na B20.6 com uma regressão
    que distingue `unknown-symbol` de `unknown-dll`. Nenhum componente de
    produção foi migrado.
  - [x] **B20.3 — Validação lexical de caminhos com Rust.** O validador Rust
    agora cobre, de forma opt-in (`TL_BUILD_RUST=ON`), as fontes relativas dos
    perfis e os destinos `C:\\...` do materializador. O FFI adiciona status de
    caminho inválido, rejeição explícita de NUL, limite de 1 MiB, handles locais
    e falha fechada; o C++ continua verificando filesystem, symlinks, colisões
    e confinamento físico. O parser, loader, APIs Win32 e runtime não foram
    migrados, e o build padrão (`TL_BUILD_RUST=OFF`) continua sem Rust.
    `rust_ffi_probe`, o corpus diferencial Rust↔C++, os testes de perfil e
    materialização passaram em Debug e Sanitize; o probe e o corpus isolado
    também passaram em Release. O Cargo passou com `--locked --offline`, e a
    regressão C++ sem Rust passou. O bloqueio histórico de Release causado pelo
    `-Werror` sobre `write_le_u32`, não utilizado em `src/loader/image_mapper.cpp`,
    foi removido na B20.6 sem alterar o comportamento do mapper.
  - [x] **B20.4 — Testes e robustez.** A fronteira FFI agora possui testes
    unitários Rust para UTF-8/UTF-16, paths, limites, overflow, panic e falha
    de alocação simulada, além de geração property-based determinística sem
    crates externas. O probe C++ cobre ponteiros nulos, precedência de erros,
    todas as capacidades de diagnóstico e regiões sentinela; o corpus
    diferencial amplia a equivalência Rust↔C++ e mantém NUL como única
    rejeição adicional intencional. `rust_cargo_tests`, `rust_cargo_clippy`,
    `rust_ffi_probe` e `rust_path_validation` passaram nos três perfis Rust;
    `RustPathValidationTest.*` passou nos perfis Debug e Sanitize, e o core C++
    também passou com `TL_BUILD_RUST=OFF`. A documentação registra os limites
    e a ausência de migração de produção.
  - [x] **B20.5 — Integração operacional.** O fluxo `app run` agora cria uma
    sessão RAII Rust por fase (`load_profile` e materialização), reutiliza um
    handle local para todos os caminhos, registra checks/rejeições/duração e
    distingue entrada lexical inválida de falha interna. Perfil ou
    materialização lexicalmente inválidos preservam o fallback genérico; falha
    interna do adaptador impede o convidado e retorna `70`, sem fallback
    silencioso. O trace emite `path-validation` no componente `runtime` e,
    para a materialização Proton, no componente `proton`; `TL_BUILD_RUST=OFF`
    mantém o caminho C++ sem esses eventos.

    `integration_rust_operational` passou em Debug e Sanitize Rust e no mesmo
    cenário Debug sem Rust: duas execuções catalogadas de
    `tl_compat_file.exe`, perfil inválido de `tl_hello.exe`, timeout `72` de
    `tl_hang.exe` com limpeza e fonte preservada, e `tl_proton_probe.exe` com
    mock Proton, stdout/stderr, exit code, prefixos, destino temporário e
    invisibilidade de `compat/` verificados. As regressões de perfil,
    materialização, isolamento e Proton também passaram. Na validação original,
    o runtime Release ainda não pôde ser relinkado por causa do warning
    preexistente de `write_le_u32`; a B20.6 removeu o helper, tratou os retornos
    de `write()` e corrigiu o salto cross-stack intencional para `_longjmp`. O
    baseline local do cenário foi aproximadamente 1,42 s com Rust e 1,41 s sem
    Rust; é medição informativa, não um limite de hardware. Nenhum parser,
    loader, runtime Win32 ou componente de produção foi migrado.
  - [x] **B20.6 — Promoção por evidência.** A adoção seletiva foi promovida
    somente para a validação lexical de caminhos integrada ao `app run`.
    Debug Rust e Release Rust passaram na suíte completa, sem os cinco testes
    opcionais de Proton real: 0 falhas entre 668 testes em cada perfil (os
    quatro testes ambientais aplicáveis foram `skipped`). O gate específico B20
    (probe, Cargo, Clippy, paths, integração operacional, regressão de imports
    e `tl_missing_dll`) passou 7/7 em Debug, Sanitize e Release. O piloto real
    opcional do Proton passou 5/5 em Debug com a instalação configurada. O
    baseline C++ isolado com `TL_BUILD_RUST=OFF` passou sem falhas entre 659
    testes, sem staticlib nem eventos Rust. A suíte completa Sanitize foi
    executada; dez falhas
    históricas ou ambientais fora deste gate permanecem reproduzíveis
    (ASan/UBSan em helpers e fixtures, `RLIMIT_AS`, imagens sem relocations e
    Xvfb/LSan) e não foram atribuídas à B20. Nenhuma falha nova apareceu no
    escopo promovido. A divergência de `tl_missing_dll` foi reconciliada para
    distinguir `unknown-symbol` de `unknown-dll`, e o bloqueio histórico de
    Release foi removido. Rust continua opt-in, Cargo/toolchain ficam fora do
    build padrão, e nenhum parser, loader, API Win32 ou componente de produção
    foi migrado. A matriz e a documentação foram atualizadas; o worktree foi
    limpo após o commit desta etapa.

### Itens das listas antigas já absorvidos

O isolamento em processo filho, o crash log com endereço/RVA/seção quando
possível, o timeout, os forwarders, TLS genérico, o parser MSIX, a divisão da
suíte Win32, a centralização de helpers e a auditoria de stubs já possuem
implementação e evidência no repositório. A entrada/localização básica por
caminho também já existe em `<app.exe>`, `app add`, `install --app-exe` e no
launcher; isso corresponde ao item `B3`. Eles não devem ser reabertos por causa
das versões históricas dos documentos auxiliares.

As restrições de PE32/x86, ARM, WOW64, .NET/Mono, drivers, anticheat e serviços
Windows continuam limites de escopo, não tarefas abertas. DirectX e áudio
continuam fora do runtime próprio; a B14.6 avalia esses recursos somente pelo
backend Proton opcional e por aplicativo-alvo, sem transformar a avaliação em
promessa de suporte geral.

## Definição de pronto

Uma tarefa do roadmap só é considerada pronta quando:

- o código foi compilado com as configurações suportadas;
- existe um teste automatizado ou uma justificativa documentada para teste manual;
- falhas são observáveis pelo trace ou por uma mensagem de erro útil;
- a documentação da API ou limitação foi atualizada;
- o comportamento não quebra os fixtures já suportados.
