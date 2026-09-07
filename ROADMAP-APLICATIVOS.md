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
- [x] Analisado o pacote MSIX, que foi classificado como
  `unsupported-format` pelo parser Rust.

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
| HWiNFO64 | Falha estrutural em exports/RVA | Confirmar se é layout legítimo ou imagem inválida |
| Rufus | Report passou; execução parou por entry point fora de página executável | Investigar imagem empacotada e política de execução |
| MSIX Affinity | ZIP válido com 1.284 entradas, mas rejeitado como formato não suportado | Isolar a convenção ZIP/MSIX não coberta |

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

### A2 — Estrutura de exports do HWiNFO64

Objetivo: distinguir uma imagem PE legítima de uma imagem malformada sem
enfraquecer a validação de RVA.

Tarefas:

- [ ] Reproduzir o erro de diretório de exports fora da imagem.
- [ ] Inspecionar seções, diretório de dados, ranges e conversões RVA/offset.
- [ ] Se houver layout legítimo, implementar a menor correção segura e criar
  uma fixture de regressão.
- [ ] Se a imagem for inválida, manter a rejeição e melhorar o diagnóstico.

Aceitação:

- [ ] A decisão é sustentada por fixture e trace estruturado.
- [ ] Não há acesso fora da entrada nem relaxamento genérico de limites.

### A3 — Rufus e imagens PE empacotadas

Objetivo: separar suporte de análise de suporte de execução para imagens com
seções como `UPX0`/`UPX1`.

Tarefas:

- [ ] Inspecionar entry point, seções, permissões e relocations.
- [ ] Determinar se o erro ocorre no parser, no mapeamento ou na política de
  execução.
- [ ] Definir uma política segura para imagens empacotadas e registrar a
  limitação quando não houver suporte.
- [ ] Criar regressão para análise aprovada sem prometer execução.

Aceitação:

- [ ] O report continua distinto da execução.
- [ ] Falhas ocorrem antes de execução quando as pré-condições não são válidas.

### A4 — Pacote MSIX do Affinity

Objetivo: identificar por que um ZIP estruturalmente válido é rejeitado pelo
parser MSIX Rust.

Tarefas:

- [ ] Reproduzir `unsupported-format`, código `16`, fase `2`, offset
  `672846681` e valor `65535`.
- [ ] Comparar central directory, EOCD, flags, extra fields, timestamps,
  compressão, atributos e possíveis estruturas Zip64.
- [ ] Verificar descriptor, multi-disco, encryption, links e convenções de
  bundle sem presumir que todo ZIP é MSIX simples.
- [ ] Criar uma fixture mínima representativa e um caso de rejeição.

Aceitação:

- [ ] O formato é aceito somente com validações completas, ou a rejeição fica
  documentada como limitação precisa.
- [ ] Extração, PE interno e catálogo continuam fora da análise do parser sem
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
