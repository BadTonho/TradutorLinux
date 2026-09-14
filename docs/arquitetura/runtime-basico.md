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

As APIs que recebem ponteiros do programa convidado devem usar as primitivas
`read_guest_memory` e `write_guest_memory` para copiar dados entre os espaços
convidado e host. Elas tentam a interface do kernel
`process_vm_readv`/`process_vm_writev` e possuem fallback para
`/proc/thread-self/mem` em ambientes que bloqueiam essas syscalls. O resultado
classifica sucesso, cópia parcial, faixa desmontada, permissão insuficiente,
argumento inválido ou erro do sistema; a cópia não desreferencia diretamente o
endereço convidado no código do host.

`validate_mapped_range` continua sendo uma verificação de permissão baseada em
uma fotografia de `/proc/self/maps`, útil apenas como predicado advisory. Ela
não é uma garantia de posse da página entre a validação e um acesso posterior.
As APIs do runtime usam `read_guest_memory`/`write_guest_memory` para transferir
buffers e strings; estruturas mantidas pelo próprio runtime e dados da imagem
PE seguem os contratos internos descritos em
[`memoria-convidada.md`](memoria-convidada.md).
