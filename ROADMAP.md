# Roadmap atual — TradutorLinux

## Estado do documento

Este é o roadmap de trabalho atual do TradutorLinux. Ele substitui as listas
de análise como fonte de próximas etapas, mas não altera o histórico preservado
em [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md).

O escopo abaixo contém somente pendências que permaneceram após a triagem de
2026-09-12. Não são itens de compatibilidade já concluídos nem autorização
para ampliar famílias de DLL sem alvo, contrato e regressão.

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

**Problema:** src/runtime/dlls/net/mpr.cpp ainda retorna NO_ERROR em operações
que não são executadas e WNetOpenEnumW publica o valor constante 'WNet' como
se fosse um handle válido.

**Tarefas:**

- [ ] inventariar quais APIs MPR.dll são consumidas pelas fixtures e pelo
  7-Zip File Manager;
- [ ] definir o subconjunto realmente implementável: estado lógico validado ou
  stub controlado com ERROR_NOT_SUPPORTED;
- [ ] remover o handle fictício e aceitar somente handles emitidos por uma
  tabela de ownership do runtime, se a enumeração for implementada;
- [ ] validar ponteiros, contagens, buffers e GetLastError em todos os caminhos
  de sucesso e falha;
- [ ] classificar cada export como Full, Limited ou Stub;
- [ ] adicionar regressão unitária e atualizar a fixture/integração do 7-Zip
  sem promover resolução de import a suporte funcional.

**Aceitação:** nenhuma API retorna sucesso para uma operação não realizada;
nenhum handle inventado atravessa a ABI; os testes cobrem entradas válidas,
inválidas e a limitação publicada; a matriz registra o resultado observado.

### R2 — Fechar a falha controlada de inicialização de thread (E7)

**Problema:** a criação de thread já valida o endereço inicial e trata a falha
de configuração do TEB, mas ainda não existe uma fixture capaz de provocar uma
falha real de arch_prctl de modo determinístico.

**Tarefas:**

- [ ] introduzir uma fronteira de teste estreita para injetar a falha de
  arch_prctl, sem alterar o caminho normal nem a ABI Microsoft x64;
- [ ] verificar limpeza de stack, TEB, descritores de thread, handles e
  sinalização de término quando a inicialização falhar;
- [ ] garantir que a thread convidada não execute o entry point após a falha;
- [ ] adicionar regressão de concorrência e trace do erro controlado;
- [ ] atualizar a documentação de ABI e concorrência.

**Aceitação:** a falha injetada produz resultado reproduzível, não deixa estado
parcial observável e não permite execução convidada após arch_prctl falhar.
O caminho normal de CreateThread permanece protegido pelos testes existentes.

### R3 — Delimitar acesso seguro à memória convidada (E11)

**Problema:** a validação por /proc/self/maps é uma fotografia. Entre a
validação e o acesso, o mapeamento pode mudar; isso deixa uma janela TOCTOU em
rotinas que leem ou escrevem memória do convidado.

**Tarefas:**

- [ ] catalogar os pontos de leitura e escrita que atravessam a fronteira de
  memória convidada;
- [ ] escolher um mecanismo de acesso protegido compatível com o processo
  convidado, sem exceção C++ atravessar a ABI e sem mascarar falhas;
- [ ] definir comportamento para páginas desmontadas, somente leitura,
  desalinhamento, overflow e buffers parcialmente acessíveis;
- [ ] adicionar testes de robustez para acesso concorrente e truncado;
- [ ] publicar a garantia efetiva e os limites residuais em docs/arquitetura/
  e docs/compatibilidade-runtime.md.

**Aceitação:** nenhum caminho documentado depende apenas de uma fotografia de
/proc/self/maps sem declarar a limitação; acessos inválidos falham de forma
controlada; os testes não permitem corrupção do host nem falso sucesso.

### R4 — Tornar explícita a classificação de exports

**Problema:** ExportSupport::Full ainda é o valor padrão de estruturas de
registro. Uma nova exportação pode parecer completa por omissão, mesmo sem
contrato funcional e regressão.

**Tarefas:**

- [ ] levantar registros que dependem do valor padrão e separar os casos
  intencionais dos acidentais;
- [ ] exigir classificação explícita para novas exportações sem alterar a ABI
  dos módulos já publicados;
- [ ] auditar aliases e direct_export para que não promovam stub ou retorno
  vazio a Full;
- [ ] adicionar teste de registro e diagnóstico que detecte classificação
  ausente ou incompatível com o contrato;
- [ ] atualizar a documentação da API e a matriz quando uma classificação
  mudar.

**Aceitação:** cada exportação nova tem classificação, comportamento testado e
limitação publicada; nenhuma alteração de classificação é feita apenas por
nome de símbolo ou por resolução de import.

## Fora desta rodada

Não entram neste roadmap, por enquanto:

- otimizações sem benchmark;
- grandes refatorações de arquivos ou do runtime_context;
- Qt opcional, novos presets e reorganização de CI sem uma necessidade
  reproduzida;
- novas famílias de DLL ou suporte a PE32/x86;
- exclusão dos arquivos de análise histórica.

Esses temas só podem entrar em uma revisão futura com evidência, alvo,
critério de aceite e etapa própria.

## Referências

- [PROJETO.md](PROJETO.md) — missão e limites do produto;
- [docs/compatibilidade.md](docs/compatibilidade.md) — matriz de estado;
- [docs/compatibilidade-runtime.md](docs/compatibilidade-runtime.md) —
  contratos do runtime;
- [docs/arquitetura/api-win32.md](docs/arquitetura/api-win32.md) — contratos
  de APIs Win32;
- [ANALISE-ERROS-2026-09-12.md](ANALISE-ERROS-2026-09-12.md) — registro
  histórico das falhas que originaram R1–R3;
- [ANALISE-MELHORIAS-2026-09-12.md](ANALISE-MELHORIAS-2026-09-12.md) —
  registro histórico da regra de classificação de R4.
