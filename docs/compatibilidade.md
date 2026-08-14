# Matriz de compatibilidade

Esta matriz declara o comportamento suportado; ela não é uma promessa de compatibilidade geral com Windows.

## Aplicações de teste

| Fixture | Arquitetura | CRT | Imports esperados | Estado atual | Próximo marco |
|---|---|---:|---|---|---|
| `tl_nop.exe` | PE32+ AMD64 | Não | Nenhum | Gerado, verificado, parseado, mapeado e com imports resolvidos na Fase 3; ainda não executado | Fase 4 |
| `tl_hello.exe` | PE32+ AMD64 | Não | `KERNEL32.dll!ExitProcess`, `GetStdHandle`, `WriteFile` | Gerado, verificado, parseado, mapeado e com os 3 imports resolvidos para stubs `ms_abi` na Fase 3; ainda não executado | Fase 4 |
| `tl_reloc.exe` | PE32+ AMD64 | Não | Nenhum | Gerado com `-Wl,--dynamicbase`, verificado, parseado e mapeado na Fase 2; usado para validar base relocations | Fase 4 |
| `tl_missing_dll.exe` | PE32+ AMD64 | Não | `USER32.dll!MessageBoxA` | Gerado, verificado e rejeitado na Fase 3: o runtime diagnostica `unknown-dll` e retorna `5` sem executar o entry point | Fase 4 |

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

## Mapeamento de imagem (Fase 2)

O mapeador (`include/tradutorlinux/loader/image_mapper.hpp`, `src/loader/image_mapper.cpp`) reserva a imagem no endereço preferencial quando possível e aplica base relocations quando a base real difere da preferencial. O contrato detalhado (layout de memória, política de permissões, tipos de relocations suportados) está em `docs/arquitetura/mapeamento-imagem.md`.

Comportamento de rejeição:

| Condição | Resultado |
|---|---|
| `SizeOfImage` inválido (0) ou que excede o espaço de endereço do host | `InvalidImage` |
| Seção que excede o tamanho da imagem | `InvalidImage` |
| Seções sobrepostas na imagem | `InvalidImage` |
| Diretório de relocations inválido ou com alvo fora da imagem mapeada | `InvalidImage` |
| Falha do `mmap`/`mprotect` por falta de memória | `OutOfMemory` |

O CLI emite eventos `loader` no trace (ver `docs/diagnostico.md`) ou um resumo em `stderr` quando `--trace` não é usado. O mapa é liberado (`unmap`) ao final do comando.

## Resolução de imports (Fase 3)

O resolvedor (`include/tradutorlinux/loader/import_resolver.hpp`, `src/loader/import_resolver.cpp`) percorre a import table do PE, procura cada DLL no registro de módulos internos e grava o endereço resolvido no slot correspondente da IAT da imagem mapeada. Os módulos internos registrados embutidos são declarados em `include/tradutorlinux/loader/module.hpp` e `src/loader/module.cpp`; o contrato (registro, tabela de exports, ordinais internos, ABI) está em `docs/arquitetura/imports.md`.

O contexto mínimo de processo (`include/tradutorlinux/loader/process.hpp`, `src/loader/process.cpp`) mapeia a imagem, resolve imports e prepara a pilha do thread inicial com guard page; o entry point nunca é executado nesta fase.

Comportamento de rejeição:

| Condição | `status` no trace | Resultado |
|---|---|---|
| DLL não registrada | `unknown-dll` | `5` (`Unsupported`) |
| Símbolo não exportado pela DLL | `unknown-symbol` | `5` |
| Ordinal não exportado pela DLL | `unknown-ordinal` | `5` |
| Símbolo conhecido sem implementação | `not-implemented` | `5` |
| Delay import directory presente | `unsupported-mechanism` | `5` |
| Slot da IAT fora das seções mapeadas | `unsupported-mechanism` | `5` |

Em qualquer falha o entry point não é executado e todas as entradas são reportadas no trace (ver `docs/diagnostico.md`).

## APIs atuais (stubs da Fase 3)

Até a Fase 4, os exports de `KERNEL32.dll` apontam para stubs hospedeiros com a convenção Microsoft x64 (`TL_MSABI`) e comportamento placeholder seguro e documentado. Eles nunca são chamados nesta fase (o entry point não é executado), mas a fronteira de ABI é exercitada e testada diretamente.

| Módulo | API | Estado | Comportamento atual do stub |
|---|---|---|---|
| `KERNEL32.dll` | `GetStdHandle` | Stub `ms_abi` (Fase 3); semântica real na Fase 4 | Retorna `NULL` e registra aviso `stub` no trace |
| `KERNEL32.dll` | `WriteFile` | Stub `ms_abi` (Fase 3); semântica real na Fase 4 | Retorna `0` (FALSE) e registra aviso `stub` no trace |
| `KERNEL32.dll` | `ExitProcess` | Stub `ms_abi` (Fase 3); semântica real na Fase 4 | Não faz nada (no-op) e registra aviso `stub` no trace |

Os avisos usam o componente `runtime` com `detail="placeholder da Fase 3; semântica real na Fase 4"`. A semântica de cada stub está em `docs/arquitetura/imports.md`.
