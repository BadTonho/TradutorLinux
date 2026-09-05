# Análise crítica — arquivo histórico

A análise crítica de 2026-09-04 foi incorporada ao
[backlog consolidado do `ROADMAP.md`](ROADMAP.md#backlog-consolidado).
Este arquivo é mantido como ponto de referência para links e contexto
histórico; ele não é mais uma lista de tarefas.

Na data desta análise, as pendências identificadas foram convertidas nos itens
`B1` (limites de CPU/RAM), `B2` (arquivos de tradução por aplicativo), `B4`
(metadados do inventário), `B5`/`B6` (portfólio e Worker/RSL), `B7`/`B8`
(fluxos ainda incompletos), `B9` (unwind condicionado) e `B10` (validação
LeakSanitizer). Desde então, `B1`, `B4`, `B5`, `B7`, `B8`, `B10` e `B16` foram
implementados e validados; `B2`, `B6` e `B9` permanecem condicionados no
backlog atual. Os achados já corrigidos — isolamento, estado GUI, parser MSIX,
forwarders, TLS, helpers e stubs — estão marcados como absorvidos no roadmap e
não devem ser reabertos sem nova evidência.

Não adicione novas pendências neste arquivo. A fonte de verdade é sempre o
`ROADMAP.md`.
