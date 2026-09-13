# Roadmap atual — TradutorLinux

## Estado do documento

Este é o roadmap de trabalho atual do TradutorLinux. Ele substitui as listas
de análise como fonte de próximas etapas, mas não altera o histórico preservado
em [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md).

O escopo abaixo contém somente pendências que permaneceram após a triagem de
2026-09-12. Não são itens de compatibilidade já concluídos nem autorização
para ampliar famílias de DLL sem alvo, contrato e regressão.

## Auditoria das pendências — 2026-09-12

- **R1 é um defeito confirmado:** `MPR.dll` retorna sucesso para operações não
  realizadas e `WNetOpenEnumW` publica um handle literal que não pertence ao
  runtime. A cobertura atual reproduz esse comportamento em vez de protegê-lo.
- **R4 é um defeito de classificação confirmado:** `ExportSupport::Full` é o
  padrão e os exports MPR aparecem como `full` no relatório por omissão. A
  parte sobre `direct_export` é somente uma auditoria de regressão: endereços
  zero já terminam como `NotImpl` no resolvedor.
- **R2 não é um defeito de produção reproduzido:** o caminho de falha de
  `ARCH_SET_GS` já existe e limpa o estado; falta uma injeção determinística
  para comprovar esse caminho.
- **R3 é uma limitação arquitetural real:** a validação por `/proc/self/maps`
  não é atômica com o acesso posterior. A primeira parte da correção já possui
  reproducer de página desmontada, permissões e mudança concorrente; a
  migração de todos os consumidores ainda não terminou.

## Regras de execução

Cada etapa deve produzir:

1. implementação mínima no runtime;
2. teste unitário ou de robustez;
3. fixture ou integração com aplicativo-alvo quando a mudança afetar uma API
   Win32 observável;
4. atualização da documentação e da matriz de compatibilidade;
5. validação reproduzível no Linux x86-64;
6. um commit próprio antes da etapa seguinte.

Uma exportação resolvida não pode ser tratada como suporte funcional. Quando a
operação ainda não existir, o runtime deve retornar uma falha controlada,
atualizar o erro correspondente e informar a limitação no diagnóstico.

## Ordem atual

### R1 — Corrigir falso sucesso de MPR.dll

**Problema:** ~~`src/runtime/dlls/net/mpr.cpp` retornava `NO_ERROR` em operações
que não eram executadas e `WNetOpenEnumW` publicava o valor constante `WNet` como
se fosse um handle válido.~~ Corrigido nesta etapa.

**Tarefas:**

- [x] inventariar quais APIs MPR.dll são consumidas pelas fixtures e pelo
  7-Zip File Manager;
- [x] definir o subconjunto realmente implementável: estado lógico validado ou
  stub controlado com ERROR_NOT_SUPPORTED;
- [x] remover o handle fictício e aceitar somente handles emitidos por uma
  tabela de ownership do runtime, se a enumeração for implementada;
- [x] validar ponteiros, contagens, buffers e GetLastError em todos os caminhos
  de sucesso e falha;
- [x] classificar cada export como Full, Limited ou Stub;
- [x] adicionar regressão unitária e atualizar a fixture/integração do 7-Zip
  sem promover resolução de import a suporte funcional.

**Aceitação:** nenhuma API retorna sucesso para uma operação não realizada;
nenhum handle inventado atravessa a ABI; os testes cobrem entradas válidas,
inválidas e a limitação publicada; a matriz registra o resultado observado.

**Evidência 2026-09-12:** `MPR.dll` foi reduzida a stubs explícitos. Os seis
exports validam `NETRESOURCEW`, strings, buffers e saídas; operações válidas
retornam `ERROR_NOT_SUPPORTED`, `WNetOpenEnumW` zera o handle e handles não
emitidos retornam `ERROR_INVALID_HANDLE`. `MprTest.*` passou, o fixture
`tl_7zfm_gui.exe` terminou com `7ZFM GUI APIS OK`/exit `0`, e `--report` mostrou
`MPR.dll (3/3 resolved)` com `support=stub`. O teste agregado
`SevenZipGuiCoverageTest.AllApisAndModules` continua com a falha pré-existente
de `SHGetSpecialFolderPathW`, registrada sem ser atribuída a R1.

### R4 — Tornar explícita a classificação de exports

**Problema confirmado:** ~~`ExportSupport::Full` era o valor padrão de
estruturas de registro. A omissão fazia os exports MPR parecerem completos no
`--report`, embora a implementação fosse um stub com falso sucesso.~~ Corrigido
nesta etapa.

**Tarefas:**

- [x] levantar registros que dependem do valor padrão e separar os casos
  intencionais dos acidentais, começando por MPR;
- [x] exigir classificação explícita para novos exports e migrar registros
  existentes sem alterar a ABI dos módulos já publicados;
- [x] auditar aliases e `direct_export` somente para preservar a distinção
  entre export de DLL convidada, stub, forwarder e endereço vazio; não mudar a
  classificação de um export PE convidado sem um caso de regressão;
- [x] adicionar teste de registro e diagnóstico que detecte classificação
  ausente ou incompatível com o contrato;
- [x] atualizar a documentação da API e a matriz quando uma classificação
  mudar.

**Aceitação:** cada exportação nova tem classificação, comportamento testado
e limitação publicada; nenhum export MPR não implementado aparece como
`Full`; endereço zero não é resolvido como função; nenhuma alteração de
classificação é feita apenas por nome de símbolo ou resolução de import.

**Evidência 2026-09-12:** `ExportedFunction` agora exige `ExportSupport` no
construtor; os registros existentes foram migrados explicitamente, enquanto
exports PE convidados continuam classificados como `Full` somente no caminho
`direct_export`. `register_module` rejeita valores inválidos antes de publicar
o módulo, coberto por `ModuleTest.RejectsInvalidExportSupportLevel`. As suítes
`ModuleTest.*`, `ImportResolverTest.*` e `Win32StubTest.*` passaram (43 testes),
e o relatório do 7-Zip confirmou os três imports MPR como `support=stub`.

### R2 — Adicionar regressão determinística para falha de inicialização de thread (E7)

**Lacuna de evidência:** ~~a criação de thread já valida o endereço inicial,
trata falhas de alocação e possui um caminho de falha para a configuração de
`ARCH_SET_GS`, mas ainda não existe uma fixture capaz de provocar essa falha de
modo determinístico.~~ Corrigida nesta etapa.

**Tarefas:**

- [x] introduzir uma fronteira de teste estreita para injetar a falha de
  arch_prctl, sem alterar o caminho normal nem a ABI Microsoft x64;
- [x] verificar limpeza de stack, TEB, descritores de thread, handles e
  sinalização de término quando a inicialização falhar;
- [x] garantir que a thread convidada não execute o entry point após a falha;
- [x] adicionar regressão de concorrência e trace do erro controlado;
- [x] atualizar a documentação de ABI e concorrência.

**Aceitação:** a falha injetada produz resultado reproduzível, não deixa estado
parcial observável e não permite execução convidada após `ARCH_SET_GS` falhar.
O caminho normal de `CreateThread` permanece protegido pelos testes
existentes, sem transformar uma lacuna de teste em alegação de incompatibilidade.

**Evidência 2026-09-12:** `Win32ConcurrencyTest.CreateThreadCleansUpAfterInjectedGsFailure`
usa um entry point convidado executável, injeta apenas a fronteira de
`set_guest_gs_base`, espera a conclusão, verifica exit code `0`, zero chamadas
ao entry point, handle inválido após `CloseHandle` e slot com TEB/stack limpos.
Também confirma no JSONL o evento `api-failure` de `CreateThread`/`guest-teb`.
Os dois testes de rejeição de entry point inválido continuam passando.

### R3 — Delimitar acesso seguro à memória convidada (E11)

**Risco arquitetural:** a validação por `/proc/self/maps` é uma fotografia.
Entre a validação e o acesso, o mapeamento pode mudar; isso deixa uma janela
TOCTOU em rotinas que leem ou escrevem memória do convidado. A fronteira
protegida e os primeiros consumidores já têm regressão determinística, mas a
auditoria encontrou consumidores legados que ainda precisam de migração.

**Tarefas:**

- [x] catalogar as categorias e os consumidores já auditados em
  `docs/arquitetura/memoria-convidada.md`; a migração dos consumidores legados
  continua sendo o trabalho restante desta etapa;
- [x] escolher `process_vm_readv`/`process_vm_writev`, com fallback controlado
  para `/proc/thread-self/mem`, sem exceção C++ atravessar a ABI;
- [x] definir comportamento para páginas desmontadas, somente leitura,
  desalinhamento, overflow e buffers parcialmente acessíveis por meio de
  `GuestMemoryAccessStatus`;
- [x] adicionar testes de robustez para acesso concorrente e truncado em
  `RuntimeMemoryValidationTest.*`;
- [x] publicar a garantia efetiva e os limites residuais em
  `docs/arquitetura/memoria-convidada.md` e
  `docs/compatibilidade-runtime.md`.

**Aceitação:** nenhum caminho documentado depende apenas de uma fotografia de
`/proc/self/maps` sem declarar a limitação; acessos inválidos falham de forma
controlada no processo convidado, sem corrupção do host nem falso sucesso; os
testes cobrem páginas desmontadas, permissões, overflow e buffers parciais.
Esta aceitação continua pendente enquanto existirem consumidores de ponteiros
convidados que façam acesso direto sem contrato equivalente; o progresso atual
fecha a primitiva e os caminhos de strings/MPR, mas não toda a superfície.

**Progresso 2026-09-12:** além da primitiva, `ReadFile`/`WriteFile`,
`FindFirstFileA/W`, `GetMessageA`, conversões comuns de caminho e entradas de
`WININET`, PSAPI, WINMM e DWM usam cópias protegidas. Os testes focados de
memória, arquivos, metadados, GUI, MPR, WININET e stubs auxiliares passaram; a busca de auditoria ainda encontra
rotas legadas em módulos como locale, segurança, GDI, sincronização e APIs de
rede, portanto a aceitação final permanece aberta.

O lote seguinte migrou também estruturas e buffers de console/tempo, com os
testes de `Win32ConsoleTest`, `Win32ProcessConsoleTest`, `Win32TimeTest` e
`Win32WideTest` passando.

O lote de caminhos migrou as saídas de diretório, módulo, temporários,
capacidade de disco, nomes completos/finais e `file_part`, além das leituras de
strings A/W usadas por essas rotas. As coberturas existentes e a regressão
`Win32DirTest.ProtectedPathOutputsRejectUnmappedPointers` passaram.

O lote de memória virtual migrou as estruturas e escalares de
`GlobalMemoryStatusEx`, `GlobalMemoryStatus`, `VirtualQuery`, `VirtualQueryEx`,
`VirtualProtect` e `GetPhysicallyInstalledSystemMemory`; a regressão
`Win32VirtualTest.ProtectedMemoryOutputsRejectUnmappedPointers` passou junto
com as coberturas de alocação, proteção e consulta.

O lote de sincronização migrou arrays de handles, `WaitOnAddress`, contadores
de semáforo, `INIT_ONCE`, SRW/condição e `RegisterWaitForSingleObject`; a
regressão `Win32ConcurrencyTest.ProtectedSynchronizationPointersRejectUnmappedMemory`
passou com a cobertura existente de eventos, semáforos, mutexes e múltiplos
waits.

O lote WININET migrou o corpo opcional de `HttpSendRequestW`, leitura e
disponibilidade de resposta, consultas HTTP, opções de timeout e
`InternetCrackUrlW`; `WininetTest.*` passou, incluindo a regressão de ponteiro
não mapeado para `URL_COMPONENTS`.

## Fora desta rodada

Não entram neste roadmap, por enquanto:

- otimizações sem benchmark;
- grandes refatorações de arquivos ou do runtime_context;
- Qt opcional, novos presets e reorganização de CI sem uma necessidade
  reproduzida;
- novas famílias de DLL ou suporte a PE32/x86;

Esses temas só podem entrar em uma revisão futura com evidência, alvo,
critério de aceite e etapa própria.

## Referências

- [PROJETO.md](PROJETO.md) — missão e limites do produto;
- [docs/compatibilidade.md](docs/compatibilidade.md) — matriz de estado;
- [docs/compatibilidade-runtime.md](docs/compatibilidade-runtime.md) —
  contratos do runtime;
- [docs/arquitetura/api-win32.md](docs/arquitetura/api-win32.md) — contratos
  de APIs Win32;
- as triagens de 2026-09-12, consolidadas nas etapas R1–R4 deste documento.
