# Console Win32 e contexto de processo (Fases 4 e 13.8)

O código convidado PE32+ x86-64 chama as funções exportadas com a ABI Microsoft
x64 (`TL_MSABI`). A Fase 13.8 preserva o console byte da Fase 4 e acrescenta
estado por processo, operações UTF-16 e o contexto mínimo compartilhado pelos
benchmarks x64.

## Handles

`GetStdHandle` aceita apenas `STD_INPUT_HANDLE` (`-10`), `STD_OUTPUT_HANDLE`
(`-11`) e `STD_ERROR_HANDLE` (`-12`). O runtime inicia cada processo com tokens
opacos para os descritores Linux, nunca com os números dos descritores:

| Handle Win32 | Descritor Linux |
|---|---:|
| `STD_INPUT_HANDLE` | `STDIN_FILENO` |
| `STD_OUTPUT_HANDLE` | `STDOUT_FILENO` |
| `STD_ERROR_HANDLE` | `STDERR_FILENO` |

`SetStdHandle` troca um dos três valores no contexto Win32 do processo. Threads
compartilham a alteração; uma nova execução reinicia os três valores. O handle
novo precisa ser `NULL` ou um handle já conhecido pelo runtime.
`GetStartupInfoW` devolve o snapshot atual em uma estrutura AMD64 de 104 bytes
com `STARTF_USESTDHANDLES`.

`GetFileType` classifica handles conhecidos como `FILE_TYPE_DISK`,
`FILE_TYPE_CHAR` ou `FILE_TYPE_PIPE` por `fstat`; handle inválido retorna
`FILE_TYPE_UNKNOWN` e `ERROR_INVALID_HANDLE`.

## `WriteFile` e `ReadFile`

As funções operam bytes sem conversão de encoding. O tamanho é limitado ao
`DWORD` da assinatura e `lpOverlapped` precisa ser `NULL`.

`WriteFile` escreve em handles de arquivo e nos tokens de stdout/stderr;
`ReadFile` lê handles de arquivo ou stdin uma vez e trata EOF como sucesso com
zero bytes. Em falha, a função retorna `FALSE` e zera o contador de bytes
quando o ponteiro foi fornecido.

## Console UTF-16

`WriteConsoleW` aceita os tokens originais de stdout/stderr, converte o número
explícito de unidades UTF-16 para UTF-8 e escreve todo o resultado. Um handle
redirecionado para arquivo não é tratado como console. `ReadConsoleW` aceita o
token original de stdin, lê até `4 * nNumberOfCharsToRead` bytes UTF-8 e devolve
as unidades UTF-16 produzidas. `CONSOLE_READCONSOLE_CONTROL`, I/O sobreposto,
edição de linha, buffer de eventos e code page configurável permanecem fora do
subconjunto.

Buffers, contadores e campos reservados são validados; falhas usam
`ERROR_INVALID_PARAMETER` ou `ERROR_INVALID_HANDLE`. As operações emitem o
evento `runtime console` no trace.

## Contexto determinístico

- `GetSystemDirectoryW` expõe o caminho lógico `C:\Windows\System32`, com
  consulta de tamanho e `ERROR_INSUFFICIENT_BUFFER`.
- `IsDebuggerPresent` retorna `FALSE`: não há depurador Win32 associado ao
  processo convidado e o estado de depuração do host não é exposto.
- `IsProcessorFeaturePresent` publica o núcleo garantido/aceito no alvo AMD64:
  CMPXCHG8B, MMX, SSE, RDTSC, PAE, SSE2 e NX; índices desconhecidos retornam
  `FALSE`.
- `EncodePointer` e `DecodePointer` usam um cookie opaco por processo e são
  reversíveis somente dentro desse processo.
- `InitializeSListHead` zera um `SLIST_HEADER` AMD64 de 16 bytes, exigindo
  alinhamento de 16 bytes. As operações push/pop/interlocked continuam fora da
  fase.

Essas APIs emitem `runtime process-context`. `AllocConsole`, `AttachConsole`,
`FreeConsole`, herança explícita de handles em `CreateProcess` e um novo PTY
Linux não são implementados nesta entrega.

## `ExitProcess` e execução

O runner chama o entry point como `TL_MSABI void (*)()` depois do mapeamento e
da resolução da IAT. `ExitProcess` registra o código no trace e usa uma saída
não local controlada para voltar ao runner sem chamar `exit()` no processo
hospedeiro. Se o entry point retornar sem chamar `ExitProcess`, o código usado
é zero.

O processo Linux não é um sandbox. O entry point executa nativamente com os
privilégios do usuário atual.
