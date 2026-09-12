# Triagem de melhorias — registro histórico — 2026-09-12

## Estado

Esta análise foi encerrada. As sugestões abaixo eram hipóteses de manutenção,
performance ou diagnóstico; nenhuma delas, por si só, autorizava alteração de
código. O arquivo não deve ser usado como backlog.

## Decisões preservadas

- Não há melhoria de performance aprovada sem medição antes/depois e teste que
  preserve o contrato. Isso vale para cópias, caches, `mprotect`, alocações,
  parsing de ZIP e criação de processos.
- Refatorações de `file.cpp`, `process.cpp`, `winapi.cpp`,
  `module_graph` e `runtime_context` continuam sendo propostas estruturais,
  não correções funcionais já realizadas.
- A matriz já separa resolução de imports, execução, nível funcional e
  limitações; essa separação deve ser mantida em qualquer alteração do
  relatório ou catálogo. A referência é
  [`docs/compatibilidade.md`](docs/compatibilidade.md).
- A classificação de exports continua uma regra de qualidade: uma nova API não
  deve ser registrada como `Full` por omissão quando seu comportamento ainda
  não tiver teste. O tipo ainda aceita `Full` como valor padrão, portanto uma
  futura etapa pode tornar a classificação explícita sem misturar essa mudança
  com uma correção funcional.
- Sugestões de Qt opcional, presets, CI, sanitizers e artefatos do Git só devem
  ser retomadas após conferir o estado real do build e do índice do repositório.

## Conclusão

As melhorias que forem escolhidas para o novo roadmap devem receber escopo,
alvo, teste ou medição e critério de conclusão próprios. Este arquivo não
mantém mais a lista antiga de trinta itens nem transforma hipóteses em tarefas.
