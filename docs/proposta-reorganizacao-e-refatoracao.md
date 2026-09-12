# Proposta de reorganização e refatoração modular — estado atual

## Finalidade

Este documento descreve somente a reorganização que ainda faz sentido no
estado atual do TradutorLinux. A migração estrutural principal já foi
concluída e permanece registrada no histórico ao final; ela não deve ser
reexecutada nem usada como uma lista paralela ao roadmap.

O objetivo desta proposta é reduzir acoplamento, separar código genérico de
código específico de aplicativo e diminuir o risco de manutenção sem alterar
comportamento, ABI Microsoft x64, exports ou contratos de compatibilidade.

## Estado atual já consolidado

As seguintes reorganizações já existem e não precisam ser refeitas:

- `src/runtime/core/` concentra bootstrap, contexto e despacho central;
- `src/runtime/seh/` concentra unwind e trampolins de exceção;
- `src/runtime/dlls/` separa famílias de DLL, incluindo `kernel32/`,
  `user32/`, `net/`, `crypto/` e `com/`;
- `src/runtime/dlls/kernel32/` já separa arquivos, processos, threads,
  sincronização, memória, tempo, console, locale e módulos;
- `src/runtime/dlls/user32/` já separa janelas, mensagens, diálogos e menus;
- `include/tradutorlinux/win32/` já separa tipos e contratos por família;
- `src/cli/` já separa opções, relatório e execução;
- a suíte Win32 já possui arquivos separados para aplicativos, APIs externas,
  GUI, segurança e stubs.

Mover novamente esses diretórios não é uma etapa deste documento.

## Diagnóstico que motivou a atualização

| Área | Estado observado | Decisão |
|---|---|---|
| GUI | `src/runtime/gui_controls.cpp` contém controles genéricos e lógica específica do 7-Zip, incluindo renderização, navegação e cópia. | Separar a extensão do 7-Zip do runtime genérico. |
| Estado global | `src/runtime/core/runtime_context.hpp` declara estado de arquivos, threads, TLS/FLS, processos, janelas, menus, handles e memória. | Dividir por domínio mantendo uma fachada interna estável. |
| KERNEL32 | `src/runtime/dlls/kernel32/kernel32_internal.hpp` é incluído pelos módulos de KERNEL32 e reúne headers, tipos e helpers de vários domínios. | Reduzir o header guarda-chuva e criar dependências explícitas. |
| Arquivos | `src/runtime/dlls/kernel32/file.cpp` mistura I/O, enumeração, metadados, caminhos, volumes, INI e notificações. | Dividir por contrato de API, preservando a tabela de exports. |

Arquivos grandes que já possuem responsabilidade coerente não são motivo
suficiente para uma nova divisão. Isso vale, por enquanto, para
`src/runtime/core/winapi.cpp`, `src/cli/runner.cpp`,
`src/runtime/dlls/user32/message.cpp`, `src/runtime/dlls/user32/window.cpp`
e `tests/test_win32.cpp`.

## Frentes atuais

### F1 — Separar a extensão específica do 7-Zip

**Prioridade:** alta. É uma correção arquitetural real, mas não deve ser
tratada como uma simples movimentação de arquivo.

**Status:** concluída. A seleção explícita por perfil schema 4, a extensão
host-side, o alvo separado e o smoke por `app run` estão implementados e
validados nos commits da F1. As etapas posteriores desta proposta não devem
reintroduzir lógica específica do 7-Zip no runtime genérico.

**Origem histórica:** `src/runtime/gui_controls.cpp` e
`src/runtime/gui_controls.hpp`.

A lógica identificada por nomes como `SevenZipDirectoryEntry`,
`perform_seven_zip_copy` e `render_seven_zip_file_manager` foi movida para uma
extensão explícita em `compat/apps/7zip/`, com os cenários correspondentes em
`tests/apps/7zip/`.

Contratos preservados:

- o runtime genérico continua oferecendo janelas, controles, mensagens e
  desenho que tenham contrato geral;
- a extensão só pode ser selecionada por perfil ou prefixo explícito;
- nenhum comportamento do 7-Zip pode alterar silenciosamente outro aplicativo;
- a extensão não deve conter DLL binária gerada ou baixada;
- o smoke do 7-Zip protege a seleção, o contrato, a execução por `app run`, a
  cópia e o isolamento da extensão; a execução direta também é verificada sem
  ativar a extensão.

**Conclusão:** o código genérico não pode incluir referências específicas ao
7-Zip; a integração reproduzível e a matriz de compatibilidade declaram o
nível funcional separadamente.

### F2 — Dividir o estado interno do runtime

**Prioridade:** posterior e condicionada a evidência. É uma refatoração de
alto risco porque `runtime_context.hpp` participa de muitas fronteiras
internas; só deve avançar depois de mapear dependências e demonstrar benefício
de manutenção ou compilação.

**Origem:** `src/runtime/core/runtime_context.hpp`.

O header deve deixar de ser o ponto obrigatório para todo estado do runtime.
A divisão deve ser incremental, sem expor os novos headers como API pública.
A organização sugerida é:

- `runtime_thread_state.hpp`: TEB, TLS, FLS, threads e sincronização de
  inicialização;
- `runtime_process_state.hpp`: imagem, processos, snapshots e ciclo de vida;
- `runtime_handle_state.hpp`: tabelas e validação de handles;
- `runtime_gui_state.hpp`: classes, janelas, menus, diálogos e mensagens;
- `runtime_memory_state.hpp`: alocações, mapeamentos e recursos de memória;
- `runtime_context.hpp`: fachada interna mínima e pontos de composição.

Os nomes são uma direção de organização, não uma autorização para criar
headers vazios ou duplicar declarações. Cada novo header deve ter um dono
claro, incluir somente o contrato necessário e possuir teste de compilação ou
regressão que justifique sua existência.

**Conclusão:** módulos de DLL não devem incluir estado de GUI, processo ou
memória sem precisar dele; o comportamento e a ABI permanecem inalterados.

### F3 — Reduzir o header interno do KERNEL32

**Prioridade:** alta, depois de F1. A redução do acoplamento entre os módulos de
KERNEL32 prepara a divisão de `file.cpp` sem alterar as exports.

**Status:** concluída. Os módulos de KERNEL32 agora incluem headers por
domínio, os helpers comuns foram separados e `kernel32_internal.hpp` deixou de
ser um guarda-chuva. A tabela de exports e o comportamento das APIs não foram
alterados. A validação focada passou, exceto por uma falha preexistente e
isolada em `Win32HandleObjectTest.DuplicateHandleIncrementsRefCountAndAllowsMultipleClose`.

**Origem:** `src/runtime/dlls/kernel32/kernel32_internal.hpp`.

O arquivo foi reduzido a uma fachada interna mínima, enquanto as dependências
específicas foram separadas em:

- `kernel32_file_internal.hpp`;
- `kernel32_process_internal.hpp`;
- `kernel32_thread_internal.hpp`;
- `kernel32_memory_internal.hpp`;
- `kernel32_common.hpp` para tipos e helpers sem domínio específico.

A migração foi concluída módulo a módulo, removendo dependências implícitas e
mantendo a assinatura das exports, o layout das estruturas convidadas e a
ordem de registro das DLLs.

**Conclusão:** cada módulo de KERNEL32 depende apenas do estado e dos helpers
que utiliza; o comportamento continua protegido pelos testes atuais.

### F4 — Dividir `file.cpp` por contrato

**Prioridade:** média, depois de F3. Deve ser feita incrementalmente, com um
domínio por vez e regressão dos testes correspondentes.

**Origem:** `src/runtime/dlls/kernel32/file.cpp`.

A divisão sugerida é:

- `file_io.cpp`: abertura, leitura, escrita, ponteiro, flush e fechamento;
- `file_find.cpp`: enumeração de arquivos, streams e notificações;
- `file_metadata.cpp`: atributos, tempos, tamanho, volume e informações por
  handle;
- `file_paths.cpp`: diretórios, caminhos completos, temporários, drives e
  operações de cópia/movimentação;
- `file_ini.cpp`: APIs de perfil INI;
- `file_exports.cpp`, somente se a tabela de exports deixar de caber junto
  do domínio sem duplicação.

A separação deve seguir as APIs e os helpers usados, não apenas o tamanho do
arquivo. Cada movimento precisa manter a mesma exportação, o mesmo símbolo
ABI e o mesmo resultado de erro.

**Conclusão:** a mudança só estará pronta quando os testes de arquivo,
enumeração, caminhos, metadados e INI continuarem passando sem regressão.

## Ordem recomendada

1. F1 — separar a lógica específica do 7-Zip, porque é uma fronteira
   arquitetural real entre runtime genérico e extensão de aplicativo;
2. F3 — reduzir `kernel32_internal.hpp` e criar dependências explícitas entre
   os módulos de KERNEL32;
3. F4 — dividir `file.cpp` depois que os helpers comuns estiverem estáveis;
4. F2 — dividir o estado interno somente se o mapeamento de dependências e uma
   medição simples confirmarem benefício suficiente para compensar o risco.

Cada frente deve ser um commit próprio. Uma refatoração não deve ser misturada
com correção de comportamento, nova API, mudança de classificação ou alteração
de compatibilidade.

## Critérios para cada etapa

- exports, nomes de DLL, ABI e layouts Win32 permanecem inalterados;
- nenhuma regra específica de aplicativo entra no runtime genérico;
- o CMake e os includes são atualizados junto com a movimentação;
- testes unitários e fixtures do domínio continuam executando;
- `docs/compatibilidade.md` só muda quando a evidência ou o comportamento
  publicado mudar;
- `git diff --check` passa e o commit contém somente a etapa concluída.

## Histórico da reorganização já concluída

Estas etapas são registro histórico, não tarefas abertas:

- agrupamento das DLLs independentes em `net/`, `crypto/` e `com/`;
- criação de `runtime/core/` e `runtime/seh/`;
- descentralização das tabelas de exports por DLL;
- divisão do antigo KERNEL32 em módulos temáticos;
- divisão parcial do USER32 em janelas, mensagens, diálogos e menus;
- separação dos headers Win32 e dos componentes da CLI;
- separação da suíte Win32 por domínio.

## Referências

- [ROADMAP.md](../ROADMAP.md) — etapas funcionais atuais;
- [PROJETO.md](../PROJETO.md) — missão, limites e regras arquiteturais;
- [docs/compatibilidade.md](compatibilidade.md) — matriz de evidência;
- [docs/compatibilidade-runtime.md](compatibilidade-runtime.md) — contratos
  do runtime;
- [docs/arquitetura/api-win32.md](arquitetura/api-win32.md) — catálogo de
  APIs e limites publicados;
- [feitos/ROADMAP-LEGADO.md](../feitos/ROADMAP-LEGADO.md) — histórico das
  fases antigas.
