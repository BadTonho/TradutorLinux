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
| `notepad++.exe` | PE32+ GUI | 0/0 | 71/71 sob Xvfb válido | — | SIGSEGV controlado após `startup-info` wide; bloqueio real a investigar |
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
primeiro `SHGetFolderPathW(CSIDL_APPDATA)` vem de uma API do runtime, de uma
convenção de memória Win32 ainda incompleta ou do próprio aplicativo.

Tarefas:

- [x] Criar a fixture PE32+ mínima `tl_shell_heap_probe`, sem CRT, cobrindo
  startup wide, mutação/cópia do bloco de ambiente, liberação do bloco
  original, `SHGetFolderPathW`, `HeapSize`, `HeapReAlloc` e alocações
  posteriores, sem copiar código do Notepad++.
- [x] Executar a fixture em Debug Rust ON e C++ OFF; os testes de metadados,
  report, runtime e `app run` passaram em ambos, com exit `0` e stdout igual.
- [ ] Executar a fixture no preset Sanitizer compatível com o parser
  atual, com sentinelas e backtrace; separar corrupção do host de falha guest.
- [x] Comparar a fixture com `tl_shell` e com o Notepad++ sob Xvfb válido;
  registrar a primeira operação divergente antes de mudar o runtime.
- [ ] Se a causa for do runtime, aplicar somente a correção mínima, criar teste
  de regressão e repetir a matriz ON/OFF; se for específica do aplicativo,
  manter exit `71` controlado e documentar a limitação.

Aceitação:

- [ ] Há uma fixture reproduzível ou uma decisão comprovada de que o bloqueio
  é específico do aplicativo.
- [ ] Nenhuma correção relaxa isolamento, W^X, validação de memória ou limites.
- [ ] O Notepad++ só muda de classificação depois de execução reproduzível
  sem corrupção e com stdout/exit/trace comparados nos dois backends.

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
  ambiente também passa; o Notepad++ continua
  reproduzindo a corrupção de heap após `startup-info` wide, portanto o probe
  reduz a hipótese para uma interação posterior específica do aplicativo ou
  para uma API ainda não exercitada pelo probe.
- [x] A fixture foi executada diretamente com `build/sanitize/src/tradutorlinux`
  e `ASAN_OPTIONS=detect_leaks=0`, retornando `0` e sem relatório de memória;
  o ASan emitiu somente o aviso conhecido sobre `__asan_handle_no_return` na
  troca de stack do convidado.
- [ ] O Sanitizer compatível e a primeira escrita causadora ainda precisam ser
  isolados; a descoberta automática do CTest também não foi aceita porque o
  LSan falhou sob ptrace durante a enumeração dos testes, e o binário Sanitizer
  existente ainda precede a correção de unwind. Nenhum patch de runtime foi
  aplicado nesta etapa.

### D2 — Cenários interativos para GUIs x64

Objetivo: transformar os timeouts controlados de 7-Zip File Manager e PuTTY em
cenários automatizados de interação e encerramento, sem declarar suporte GUI
amplo.

Tarefas:

- [ ] Reutilizar o smoke Xvfb do 7-Zip para validar abertura, ação mínima e
  encerramento em Rust ON/OFF.
- [ ] Criar um cenário PuTTY que abra a janela configurável, envie somente
  eventos seguros e encerre por comando/fechamento controlado.
- [ ] Comparar janelas, eventos X11, stdout, exit code, limpeza e trace; manter
  timeout como resultado quando a interação não for determinística.

Aceitação:

- [ ] Cada cenário possui ação e critério de encerramento reproduzíveis.
- [ ] Nenhuma GUI é promovida além das operações realmente exercitadas.

### D3 — Diagnóstico dos instaladores e pacote x64

Objetivo: avançar a identificação de Roblox, G HUB e Affinity sem executar
instaladores x86, ampliar limites MSIX ou prometer .NET/Mono.

Tarefas:

- [ ] Separar, com fixtures e traces, setup convidado, materialização,
  catálogo, executável principal e falha de validação PE interno.
- [ ] Verificar se Roblox/G HUB têm uma etapa controlada que possa ser testada
  sem cadastrar ou deixar arquivos persistentes.
- [ ] Registrar Affinity como limite de pacote/.NET e testar somente rejeições,
  limpeza e diagnósticos estruturados.

Aceitação:

- [ ] Nenhum instalador deixa prefixo ou catálogo parcial.
- [ ] Qualquer mudança passa ON/OFF e preserva os códigos de erro existentes.

### D4 — Regressão e fechamento da rodada D

Objetivo: repetir a matriz completa depois de cada alteração de D1–D3.

Tarefas:

- [ ] Reexecutar `--report` nos 27 arquivos em Rust ON/C++ OFF.
- [ ] Reexecutar a matriz nativa sob Xvfb válido e a instalação seletiva em
  prefixos temporários.
- [ ] Comparar stdout, stderr, exit codes, traces, limpeza e símbolos OFF;
  atualizar a matriz sem promover limitações não resolvidas.

Aceitação:

- [ ] Todos os marcos alterados possuem commit separado e evidência em `/tmp`.
- [ ] `git diff --check` passa e o worktree fica limpo antes da próxima rodada.

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
