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
  em Rust ON e C++ OFF: 5/5 execuções controladas e 4/4 instalações
  controladas por build.
- [x] `putty_ssh_local_probe` passou em ambos os builds após o clique em
  `Open`, sem bytes no listener e sem processo residual.

Limites mantidos:

- CPU-Z, GPU-Z, HWMonitor, RTSS e os demais PE32/x86 continuam rejeitados
  antes da execução; HWiNFO continua rejeitado por imagem empacotada e
  Affinity por pacote fora do limite estrutural.
- DLLs e instaladores não são iniciados indiscriminadamente. Instalação só é
  exercitada pelos quatro candidatos com staging, timeout, memória e limpeza
  controlados já aprovados pela matriz.
- A criação da janela de sessão não é handshake SSH nem suporte funcional do
  PuTTY; o próximo bloqueio exige evidência nova do fluxo interno antes de
  qualquer API adicional.

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
