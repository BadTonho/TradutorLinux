# Propostas de evolução — arquivo histórico

As propostas de organização e comportamento desta análise foram incorporadas
ao [backlog consolidado legado](../feitos/ROADMAP-LEGADO.md#backlog-consolidado).
Este documento permanece para preservar a origem das ideias, mas não é um
backlog paralelo.

O mapeamento atual é:

- header comum de objetos/handles → `B11`;
- `VirtualQuery` coerente com alocações → `B12`;
- tabelas de codepage versionadas → `B13`;
- override por aplicativo → `B14`;
- drives do prefixo e symlinks → `B15`;
- expectativas explícitas nos fixtures → `B16`;
- supervisor entre processos → `B17`;
- SEH estruturado adicional → `B18`;
- APIs abundantes, como threadpool e ALPC → `B19`.

O crash log enriquecido, o isolamento do convidado, a fronteira `TL_MSABI`,
os fixtures PE e o catálogo já foram implementados ou documentados; por isso
não retornam como tarefas abertas. Fibras também já foram entregues e não
devem voltar ao backlog.

Toda proposta nova deve passar a existir somente no novo roadmap, com alvo ou
fixture, critério de aceite e evidência esperada.
