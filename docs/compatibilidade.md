# Matriz de compatibilidade

Esta matriz declara o comportamento suportado; ela não é uma promessa de compatibilidade geral com Windows.

## Aplicações de teste

| Fixture | Arquitetura | CRT | Imports esperados | Estado atual | Próximo marco |
|---|---:|---|---|---|---|
| `tl_nop.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado e parseado na Fase 1; ainda não executado | Fase 2 |
| `tl_hello.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | Gerado, verificado e parseado na Fase 1; ainda não executado | Fase 4 |

As fontes e manifestos das fixtures ficam em `tests/samples/`. Os binários são produtos de build e ficam em `build/<preset>/tests/samples/generated/`.

## Leitor de PE (Fase 1)

O leitor de PE (`include/tradutorlinux/pe/pe_reader.hpp`, `src/pe/pe_reader.cpp`) valida e interpreta:

- DOS header, assinatura PE, COFF header e optional header PE32+ (magic `0x20B`).
- Tabela de seções, com verificação de que headers e dados crus cabem no arquivo.
- Import table por nome (hint) e por ordinal, com limites de DLLs e símbolos.
- Base relocations por bloco e entrada.

Comportamento de rejeição:

| Entrada | Resultado |
|---|---|
| Arquivo truncado no meio de qualquer estrutura | `Truncated` |
| Assinatura DOS/PE ausente, offsets inconsistentes, tamanhos inválidos | `Malformed` |
| Arquitetura diferente de `x86-64` (machine `0x8664`) | `UnsupportedArchitecture` |
| Optional header PE32 (magic `0x10B`) ou outro formato | `UnsupportedFormat` |

O CLI expõe o leitor via `--trace` (eventos do componente `pe`, ver `docs/diagnostico.md`) e via resumo em `stderr`. A saída do leitor é comparada em teste de integração com `llvm-readobj` para as fixtures geradas.

## APIs planejadas

| Módulo | API | Estado | Marco planejado |
|---|---|---|---|
| `KERNEL32.dll` | `GetStdHandle` | Não implementada | Fase 4 |
| `KERNEL32.dll` | `WriteFile` | Não implementada | Fase 4 |
| `KERNEL32.dll` | `ExitProcess` | Não implementada | Fase 4 |

Até a implementação de um resolvedor de imports, nenhuma API é exposta ao código PE.
