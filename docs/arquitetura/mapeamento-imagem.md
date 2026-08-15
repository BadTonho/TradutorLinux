# Mapeamento de imagem (Fase 2)

Este documento define o contrato de mapeamento de imagens PE32+ x86-64 no Linux x86-64. O mapeador é Linux-only (`mmap`/`mprotect`, `<sys/mman.h>`); a fronteira fica explícita em `src/loader/image_mapper.cpp`.

## Objetivo

Produzir uma visão virtual da imagem tal como o loader Windows faria: headers no RVA 0, seções posicionadas em seus RVAs com permissões de página derivadas das characteristics, e ponteiros de endereço absoluto corrigidos para a base real de mapeamento — sem deixar a imagem inteira RWX por conveniência.

## Layout de memória

- O início da memória mapeada corresponde ao **RVA 0**.
- O mapeamento cobre `align_up(SizeOfImage, página do host)` bytes.
- O mapeamento é **anônimo privado** (`MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE`), não file-backed: headers e seções são copiados com `memcpy` a partir do arquivo e o restante permanece zerado.
- A região de headers copiada tem `min(SizeOfHeaders, SizeOfImage, tamanho do arquivo)` bytes e é protegida como `r--`.
- Cada seção ocupa o intervalo `[VirtualAddress, VirtualAddress + max(VirtualSize, SizeOfRawData))`, arredondado a páginas para proteção. Seções são validadas para caber em `SizeOfImage` e para não se sobrepor.

## Endereço preferencial e relocations

- O endereço preferencial é `ImageBase` (configurável via `MapOptions::preferred_base`).
- O mapeador tenta reservar no endereço preferencial com `MAP_FIXED_NOREPLACE` (nunca sobrescreve mapeamentos existentes). Se falhar ou não estiver disponível, mapeia em endereço livre (`mmap` anônimo).
- `delta = base_real - base_preferencial` (int64). Relocations são aplicadas **somente** quando `delta != 0`.
- Tipos de relocations suportados para PE32+: `ABSOLUTE` (0, ignorado) e `DIR64` (10). `HIGHLOW` (3) pertence ao PE32 e é rejeitado no alvo atual. Tipo não suportado, bloco malformado, diretório com bytes residuais ou alvo fora da imagem mapeada resultam em `InvalidImage`.
- Imagem realocada sem diretório de relocations pode ser inspecionada e gera o aviso `cannot-relocate`, mas o runner rejeita sua execução com `execution-rejected`.

## Política de permissões

- Nenhuma página da imagem é exposta como RWX.
- Characteristics de seção mapeiam para permissões de página:
  - sem `MEM_WRITE`/`MEM_EXECUTE`/`MEM_READ`: `None` (`---`);
  - `MEM_READ`: `r--`;
  - `MEM_READ | MEM_EXECUTE`: `r-x`;
  - `MEM_WRITE` presente (com ou sem execução): `rw-` — o bit de escrita **vence** o de execução, rebaixando seções write+exec para read-write.
- `mprotect` aplica as permissões por página, respeitando o princípio do menor privilégio.

## Contrato de falha

| Condição | Status |
|---|---|
| `SizeOfImage` inválido (0) ou maior que o espaço de endereço do host | `InvalidImage` |
| Seção fora de `SizeOfImage` ou seções sobrepostas | `InvalidImage` |
| Diretório/alvos de relocations inválidos durante a realocação | `InvalidImage` |
| `mmap`/`mprotect` falham por falta de memória | `OutOfMemory` |

## Testes e validação

- `apply_relocations` é uma função pura exposta em `include/tradutorlinux/loader/image_mapper.hpp`, testada de forma determinística em buffer de heap (`tests/test_image_mapper.cpp`).
- O mapeamento das fixtures `tl_nop.exe`/`tl_hello.exe`/`tl_reloc.exe` é testado (headers e seções copiados, permissões via `/proc/self/maps`, relocations aplicadas e verificadas em memória).
- O preset `sanitize` usa ASan, cuja região de sombra ocupa `0x140000000`; nesse preset o mapeamento cai no fallback e o caminho de realocação é exercitado de verdade.
