# Runtime básico (Fase 5)

## Erros

`GetLastError` e `SetLastError` mantêm um `DWORD` por thread hospedeira. O
subconjunto atual usa `ERROR_SUCCESS` (0), `ERROR_FILE_NOT_FOUND` (2),
`ERROR_ACCESS_DENIED` (5), `ERROR_INVALID_HANDLE` (6),
`ERROR_NOT_ENOUGH_MEMORY` (8) e `ERROR_INVALID_PARAMETER` (87).

As APIs atualizam o erro em falhas e zeram o erro em operações bem-sucedidas.
Não há ainda tradução completa de `errno` nem suporte a mensagens de erro.

## Memória

`VirtualAlloc` aceita uma nova reserva com `lpAddress == NULL` e tamanho maior
que zero. O subconjunto também permite `MEM_RESERVE` seguido de `MEM_COMMIT`
no endereço exato retornado, além de `MEM_COMMIT | MEM_RESERVE` em uma única
chamada. O tamanho é arredondado à página Linux; a memória é anônima e privada.
As proteções `PAGE_*` são registradas por região, com W^X aplicado no
hospedeiro para páginas graváveis.

`VirtualFree` aceita somente `MEM_RELEASE`, `dwSize == 0` e o endereço exato
retornado por `VirtualAlloc`. O runtime mantém um registro limitado de 64
alocações. `VirtualQuery` informa `MEM_RESERVE` com `Protect == 0` antes do
commit e separa regiões quando `VirtualProtect` altera apenas parte de uma
alocação. Após `VirtualFree`, a faixa é consultável como `MEM_FREE` enquanto o
registro puder ser reutilizado; consultas a mapeamentos externos continuam
usando `/proc/self/maps`. Commit parcial, endereço sugerido e execução efetiva
de páginas graváveis continuam fora do escopo.

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
