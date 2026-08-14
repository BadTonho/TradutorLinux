# Runtime básico (Fase 5)

## Erros

`GetLastError` e `SetLastError` mantêm um `DWORD` por thread hospedeira. O
subconjunto atual usa `ERROR_SUCCESS` (0), `ERROR_FILE_NOT_FOUND` (2),
`ERROR_ACCESS_DENIED` (5), `ERROR_INVALID_HANDLE` (6),
`ERROR_NOT_ENOUGH_MEMORY` (8) e `ERROR_INVALID_PARAMETER` (87).

As APIs atualizam o erro em falhas e zeram o erro em operações bem-sucedidas.
Não há ainda tradução completa de `errno` nem suporte a mensagens de erro.

## Memória

`VirtualAlloc` aceita somente `lpAddress == NULL`, tamanho maior que zero,
`MEM_COMMIT | MEM_RESERVE` e `PAGE_READONLY` ou `PAGE_READWRITE`. O tamanho é
arredondado à página Linux e a memória é anônima, privada e não executável.

`VirtualFree` aceita somente `MEM_RELEASE`, `dwSize == 0` e o endereço exato
retornado por `VirtualAlloc`. O runtime mantém um registro limitado de 64
alocações. Reserva separada, commit parcial, endereço sugerido e proteção
executável estão fora do escopo.

## Arquivos e caminhos

`CreateFileA` aceita apenas caminhos relativos sem `:` e sem separador inicial.
As barras invertidas são convertidas para `/`. São aceitos `GENERIC_READ`,
`GENERIC_WRITE` e a combinação dos dois; `CREATE_ALWAYS` e `OPEN_EXISTING` são
as únicas disposições. Compartilhamento, atributos, template e I/O overlapped
precisam estar zerados ou nulos.

Os handles de arquivo são tokens internos limitados a 64 slots. Eles podem ser
usados por `ReadFile`, `WriteFile` e `CloseHandle`; não são compatíveis com os
tokens dos handles padrão nem com APIs futuras sem conversão explícita.

As APIs que recebem ponteiros do programa convidado verificam o mapeamento e as
permissões da faixa em `/proc/self/maps` antes de ler ou escrever. Strings ANSI
também precisam estar terminadas dentro do limite suportado; entradas inválidas
retornam erro Win32 em vez de serem desreferenciadas pelo host.
