# Fronteira do msvcrt mínimo (Fase 9)

Este documento descreve o contrato da fronteira entre o convidado PE32+ x86-64
e o subconjunto de `msvcrt.dll` implementado pelo runtime. A motivação e o
catálogo dos aplicativos-alvo estão em `docs/compatibilidade.md`; o trace está
em `docs/diagnostico.md`.

## Regras gerais

- Todas as funções usam a convenção Microsoft x64 (`TL_CRT_MSABI`), não
  propagam exceções C++ e tratam o convidado como entrada hostil (validação de
  ponteiros e faixas via `/proc/self/maps`).
- O subconjunto existe para executar o núcleo do CRT do mingw-w64 e os fluxos
  dos aplicativos-alvo; nenhuma API entra sem aplicativo-alvo e teste de
  regressão.
- `stdout` do processo Linux pertence à saída do convidado; o runtime só
  escreve em `stderr`.

## Abi

### `GuestFile` (`_iobuf`)

Layout Microsoft x64 de 48 bytes, verificado por `static_assert`:

| Offset | Campo | Tipo |
|---|---|---|
| 0x00 | `_ptr` | `char*` |
| 0x08 | `_cnt` | `int` |
| 0x10 | `_base` | `char*` |
| 0x18 | `_flag` | `int` |
| 0x1c | `_file` | `int` |
| 0x20 | `_charbuf` | `int` |
| 0x24 | `_bufsiz` | `int` |
| 0x28 | `_tmpfname` | `char*` |

O convidado lê `_flag` (ex.: para `fflush`) e `_file` (`_fileno`). O pool de
arquivos tem capacidade fixa; `__iob_func()` devolve o ponteiro do array, com
stdin/stdout/stderr nas posições 0/1/2.

### `va_list` Microsoft x64

`va_list` é um `char*` apontando para o primeiro slot de argumento; os slots são
consecutivos e têm 8 bytes. A fronteira lê `int`/ponteiros com
`__builtin_ms_va_start`/`__builtin_ms_va_end` (sem `__builtin_ms_va_arg`, que o
GCC não implementa). Isso vale para `vfprintf` e para o argumento extra de
`_open` com `O_CREAT`.

### `GuestFnPtr`

Ponteiro de função do convidado invocado pela fronteira (callbacks de
`__initterm`, handlers de `atexit` e `signal`). Usa `TL_CRT_MSABI` e nunca deixa
exceção C++ atravessar.

## Dados importados por `msvcrt.dll`

Três células graváveis são exportadas e seus endereços gravados nos slots de
imports-dados: `__initenv` (`char**`), `_commode` (`int`) e `_fmode` (`int`).
`__initenv` é preenchido por `__getmainargs` com o ambiente do processo.

## Linha de comando do convidado

O CLI define `argv[0..]` via `msvcrt_set_guest_command_line`; `__getmainargs`
constrói `argc`, `argv` (terminado em `NULL`, armazenado em memória alocada) e
`envp`. `argv[0]` é o caminho do executável informado na linha de comando. Sem
argumentos, `argc == 1` com `argv[0]` vazio.

## Estado por fd

`_setmode`/`_open` mantêm um modo por descritor (`_O_TEXT`/`_O_BINARY`),
refletido na flag binária do `GuestFile` correspondente. O I/O das funções de
stdio é unbuffered e usa `write`/`read` diretos com loop `EINTR`.

## Limitações conhecidas

- O subconjunto cobre exatamente os símbolos que os aplicativos-alvo importam;
  símbolos ausentes são diagnosticados como `unknown-symbol` no `--report`.
  Os símbolos adicionais para `bzip2.exe` (`strncpy`, `strstr`, `ungetc`,
  `fgetc`, `fread`, `isspace`, `memmove`, `strcat`, `remove`, `_stat64`) foram
  implementados; `_stat64` preenche o `struct _stat64` do MinGW (pack 8,
  `st_mode` em `0x06`) a partir do `stat()` do host.
- Locale fixo C: code page `1252`, `mb_cur_max == 1`, `lconv` estático.
- `signal` apenas registra; nenhuma entrega real ao convidado.
- `wcs*` e o caminho `W` ficam para os alvos que os exigirem (`dos2unix`).
