# Matriz de compatibilidade

Esta matriz declara o comportamento suportado; ela não é uma promessa de compatibilidade geral com Windows.

## Aplicações de teste

| Fixture | Arquitetura | CRT | Imports esperados | Estado atual | Próximo marco |
|---|---|---:|---|---|---|
| `tl_nop.exe` | PE32+ AMD64 | Não | Nenhum | Gerado e verificado na Fase 0; ainda não executado | Fase 2 |
| `tl_hello.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | Gerado e verificado na Fase 0; ainda não executado | Fase 4 |

As fontes e manifestos das fixtures ficam em `tests/samples/`. Os binários são produtos de build e ficam em `build/<preset>/tests/samples/generated/`.

## APIs planejadas

| Módulo | API | Estado | Marco planejado |
|---|---|---|---|
| `KERNEL32.dll` | `GetStdHandle` | Não implementada | Fase 4 |
| `KERNEL32.dll` | `WriteFile` | Não implementada | Fase 4 |
| `KERNEL32.dll` | `ExitProcess` | Não implementada | Fase 4 |

Até a implementação de um resolvedor de imports, nenhuma API é exposta ao código PE.
