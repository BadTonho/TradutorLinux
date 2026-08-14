# Console Win32 mínimo (Fase 4)

O primeiro marco público implementa somente console anônimo no processo Linux
atual. O código convidado continua sendo PE32+ x86-64 e chama as funções
exportadas com a ABI Microsoft x64 (`TL_MSABI`).

## Handles

`GetStdHandle` aceita apenas `STD_INPUT_HANDLE` (`-10`), `STD_OUTPUT_HANDLE`
(`-11`) e `STD_ERROR_HANDLE` (`-12`). O runtime devolve tokens opacos internos,
não os valores numéricos dos descritores Linux. Eles são válidos apenas nas
APIs de console desta fase:

| Handle Win32 | Descritor Linux |
|---|---:|
| `STD_INPUT_HANDLE` | `STDIN_FILENO` |
| `STD_OUTPUT_HANDLE` | `STDOUT_FILENO` |
| `STD_ERROR_HANDLE` | `STDERR_FILENO` |

Qualquer outro valor retorna `NULL`.

## `WriteFile` e `ReadFile`

As funções operam bytes sem conversão de encoding. O tamanho é limitado ao
`DWORD` da assinatura. `lpOverlapped` precisa ser `NULL`; I/O sobreposto,
handles de arquivo e `GetLastError` não fazem parte do contrato da Fase 4.

`WriteFile` escreve stdout ou stderr até completar o buffer, repetindo uma
operação interrompida por `EINTR`. `ReadFile` lê stdin uma vez; EOF é sucesso
com zero bytes lidos. Em falha, a função retorna `FALSE` e zera o contador de
bytes quando o ponteiro foi fornecido.

## `ExitProcess` e execução

O runner chama o entry point como `TL_MSABI void (*)()` depois do mapeamento e
da resolução da IAT. `ExitProcess` registra o código no trace e usa uma saída
não local controlada para voltar ao runner sem chamar `exit()` no processo
hospedeiro. Se o entry point retornar sem chamar `ExitProcess`, o código usado
é zero.

O processo Linux não é um sandbox. O entry point executa nativamente com os
privilégios do usuário atual.
