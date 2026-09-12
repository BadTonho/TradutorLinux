# Triagem de melhorias — TradutorLinux — 2026-09-12

Análise estática por leitura, sem benchmark. Uma otimização só é melhoria real
quando preserva o contrato, reduz custo medido ou melhora diagnóstico sem
esconder uma falha. Nenhum item abaixo autoriza alteração de código sozinho.

## Classificação

- **Melhoria real de baixo risco:** pode entrar após teste de regressão e, para
  performance, uma medição simples antes/depois.
- **Melhoria real condicionada:** faz sentido, mas depende de ownership, ABI,
  segurança ou benchmark.
- **Refatoração estrutural:** melhora manutenção, mas não deve ser misturada com
  correção funcional.
- **Não priorizar:** prematura, duplicada por intenção ou sem benefício medido.

## Performance e memória

| ID | Classificação | Avaliação |
|---|---|---|
| M1 | **Condicionada** | Trocar cópia de `file_bytes` por `std::move` pode reduzir RAM, mas só é correto se nenhum caminho usar os bytes depois. Primeiro medir ownership e memória por DLL. |
| M2 | **Condicionada e sensível à segurança** | Agrupar patches de IAT por página reduz `mprotect` e TLB, mas deve preservar W^X, páginas compartilhadas e restauração de permissões. Deve ser tratada junto de E10. |
| M3 | **Melhoria real condicionada** | Indexar permissões por página evita busca `O(regions²)` em `image_mapper.cpp`; adicionar teste de páginas sobrepostas e medir imagens grandes. |
| M4 | **Baixo risco** | `reserve()` em vetores com tamanho conhecido reduz realocações e não muda semântica. Aplicar em lote pequeno, com testes existentes. |
| M5 | **Não priorizar sem benchmark** | Substituir leituras byte a byte por `memcpy` pode melhorar custo, mas exige manter alinhamento, endianness e limites. O ganho deve ser medido. |
| M6 | **Condicionada** | Índices e caches para exports, paths, `rva_to_file_offset`, `EnvironmentPaths` e regiões podem melhorar custo, mas exigem invalidação correta e teste de estado. |
| M7 | **Condicionada** | `posix_spawn` para processos externos pode reduzir custo de `fork`, porém não deve ser aplicado ao processo convidado sem preservar TEB, isolamento, sinais e grupos. |
| M8 | **Condicionada** | Reutilizar buffers de stderr, ambiente, ZIP e inflação pode reduzir alocações. O ciclo de vida precisa ser verificado antes, especialmente em erros parciais. |
| M9 | **Condicionada** | Cachear componentes de symlink e nomes normalizados pode reduzir `stat`, mas não pode deixar validação de prefixo obsoleta entre operações. |
| M10 | **Condicionada** | Evitar parse duplicado de ZIP/MSIX e reutilizar `ZipArchive` é uma melhoria clara quando a entrada já foi validada; exige testes de CRC, limites e falha de extração. |
| M11 | **Não priorizar** | Otimizar `validate_mapped_wstring` com `wmemchr` ou reduzir chamadas de relógio só é relevante após perfil de execução; segurança e legibilidade vêm primeiro. |

## Arquitetura e qualidade

| ID | Classificação | Avaliação |
|---|---|---|
| M12 | **Problema de correção, não apenas melhoria** | `ExportSupport::Full` como default e `direct_export` promovendo exports convidados escondem stubs. O contrato deve exigir classificação explícita; relaciona-se diretamente a E-falso-sucesso. |
| M13 | **Melhoria real** | Dar classificação e `SetLastError` próprios a aliases como `ClosePrinter` elimina diagnósticos ambíguos. Remover ordinal D3D sem nome só depois de confirmar consumidores. |
| M14 | **Refatoração estrutural** | Dividir `file.cpp`, `process.cpp`, `winapi.cpp`, `message.cpp` e `window.cpp` por responsabilidade melhora manutenção, mas deve ocorrer sem mudar exports, ABI ou ordem de fallback. |
| M15 | **Refatoração estrutural condicionada** | Extrair lambdas do `module_graph` e desacoplar `runtime_context`, registro de módulos, cache de mapas e backend GUI reduz acoplamento; exige uma etapa própria e testes completos. |
| M16 | **Condicionada** | Generalizar `handle_table` pode eliminar estruturas duplicadas, mas precisa de um piloto com ownership, tipos de handle e concorrência antes de substituir módulos. |
| M17 | **Baixa prioridade** | Adicionar `[[nodiscard]]`, `noexcept` e warnings extras é útil, mas deve ser feito em lotes pequenos e sem alterar a ABI Microsoft x64 por acidente. |
| M18 | **Não priorizar** | A duplicação C++/Rust de parsers é intencional para diferencial. Melhorias adequadas são tabela de paridade, `static_assert` e testes; não remover uma implementação agora. |

## Produto e diagnóstico

| ID | Classificação | Avaliação |
|---|---|---|
| M19 | **Melhoria real de alta prioridade** | Separar na CLI `imports: supported`, `runtime-support`, `execution` e `functional-level` evita promover aplicativo por resolução de imports. Deve ser refletido também na matriz. |
| M20 | **Melhoria real** | Recomendações do relatório devem trazer comando, prefixo, fase, `dll!symbol`, mecanismo e próximo passo; isso melhora ação do usuário sem ampliar API. |
| M21 | **Melhoria real condicionada** | A GUI pode consumir `--report --json` e exibir badges de import, suporte, saída e bloqueio, mas só depois de estabilizar o schema JSON. |
| M22 | **Melhoria real condicionada** | `doctor` deve distinguir prontidão por perfil (`console`, `gui-2D`, rede, Proton) e informar dependências ausentes; cada diagnóstico precisa ser verificável no host. |
| M23 | **Melhoria real** | `app list` e mensagens de erro devem mostrar prefixo, limites, backend, fase, RVA, exit code e símbolo. Isso é observabilidade, não compatibilidade nova. |
| M24 | **Melhoria real condicionada** | Avisos explícitos para fallback Wayland/X11, limite de janelas X11 e menu/modal indisponível evitam que um smoke seja interpretado como suporte funcional. |
| M25 | **Melhoria real** | Uma tabela automática de recorrência `dll!symbol × aplicativos` ajuda a priorizar APIs por valor compartilhado, desde que conte somente imports e execuções claramente separadas. |

## Build e CI

| ID | Classificação | Avaliação |
|---|---|---|
| M26 | **Melhoria real condicionada** | Tornar Qt opcional para um alvo CLI reduz dependências, mas exige confirmar que nenhum alvo CLI liga código GUI. Criar preset `cli-only` separado. |
| M27 | **Baixo risco** | Habilitar clone raso do GoogleTest ou preferir pacote/cache do CI reduz tempo e rede; validar a versão e o modo offline. |
| M28 | **Melhoria real** | Presets explícitos para targetapps, Proton e sem GUI, além de limite `--parallel 2`, tornam a validação reproduzível e compatível com a máquina do usuário. |
| M29 | **Melhoria real condicionada** | Separar jobs rápidos, Xvfb, Proton e Sanitize melhora o sinal do CI; falhas conhecidas devem ser classificadas, não mascaradas como sucesso. |
| M30 | **Manutenção necessária** | Remover do índice do Git artefatos gerados e alinhar `.gitignore` é higiene do repositório, mas os arquivos exatos devem ser confirmados antes de qualquer remoção. |

## Ordem recomendada

1. M12, M13, M19 e M20, pois corrigem falso-sucesso e diagnóstico.
2. M3, M4 e M10, somente com regressões e medição mínima.
3. M26–M30, em etapa separada de build/CI.
4. M14–M18, depois que os contratos funcionais estiverem estáveis.
5. M1, M2, M5–M9 e M11 apenas quando um perfil confirmar benefício.

O novo roadmap deve receber IDs desta triagem somente após cada item ganhar
escopo, teste/medição e critério de conclusão. A análise permanece um insumo;
não é backlog executável.
