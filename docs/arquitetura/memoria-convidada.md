# Memória convidada e fronteira host (R3/E11)

O ponteiro recebido de uma API Win32 pertence ao espaço de endereço do
programa convidado, mesmo quando o convidado roda no mesmo processo Linux que o
runtime. Ele deve ser tratado como entrada hostil: o mapa pode mudar entre
qualquer duas instruções, a faixa pode atravessar páginas com permissões
diferentes e um endereço aritmeticamente válido pode não estar mapeado.

## Inventário da fronteira

| Categoria | Exemplos no runtime | Risco | Tratamento atual |
|---|---|---|---|
| Strings de entrada | `validate_mapped_cstring`, `validate_mapped_wstring`, caminhos, `WININET` e `MPR` | Leitura após snapshot ou string sem terminador | Validação por blocos copiados com `read_guest_memory` nos caminhos migrados |
| Estruturas de entrada | `NETRESOURCEW`, parâmetros de arquivos, rede, janela e segurança | Tamanho/layout inválido e ponteiros internos inválidos | Validar faixa, copiar para objeto host e validar campos apontados quando o caminho já foi migrado |
| Buffers de saída | `ReadFile`/`WriteFile`, `FindFirstFileA/W`, `GetProcessMemoryInfo`, rede, locale, GUI e handles | Escrita em página desmontada ou somente leitura | `write_guest_memory` nos caminhos migrados; outras APIs ainda usam validação seguida de escrita direta |
| Imagem PE e contexto ABI | IAT, entry point, TEB, callbacks e registros de exceção | Endereços controlados pelo convidado e conversão de ABI | Imagem/contextos pertencem a regiões controladas pelo loader; callbacks e campos ABI têm validações próprias |
| Handles opacos | Arquivos, threads, janelas, rede e side-tables | Token arbitrário confundido com ponteiro host | Tabelas de ownership e validação de tipo; não são cópia de memória |

O inventário completo deve ser mantido por busca de `memcpy`, atribuições por
ponteiro, conversões `wide_to_utf8` e chamadas de callback nos módulos
`src/runtime/`. Código que opera exclusivamente sobre imagem mapeada pelo
loader ou sobre uma estrutura já copiada para o host não é um acesso de API
convidada e deve ser distinguido dos demais casos.

## Primitivas protegidas

`read_guest_memory(source, destination, size)` e
`write_guest_memory(destination, source, size)` fazem a transferência através
do kernel Linux. A primeira tentativa usa `process_vm_readv` ou
`process_vm_writev` para que a verificação de acessibilidade e a transferência
sejam uma operação do kernel. Em ambientes que negam essas syscalls para o
próprio processo, o runtime usa `/proc/thread-self/mem`; para escrita, ele
atualiza a fotografia de permissões antes do fallback e recusa uma faixa que
não esteja gravável.

O contrato do resultado é:

| Estado | Significado |
|---|---|
| `Success` | Todos os `size` bytes foram transferidos |
| `Partial` | O kernel transferiu apenas `transferred` bytes |
| `Unmapped` | A faixa não está acessível ou foi desmontada |
| `PermissionDenied` | A operação foi recusada por permissão |
| `InvalidArgument` | Ponteiro nulo, tamanho zero ou overflow de endereço |
| `SystemError` | Falha do mecanismo host sem classificação Win32 específica |

O host nunca transforma `Partial` em sucesso. Cada chamador deve rejeitar
qualquer estado diferente de `Success` antes de interpretar a cópia ou publicar
uma saída ao convidado.

## Limites residuais

`validate_mapped_range` permanece deliberadamente como predicado advisory:
consulta `/proc/self/maps` e pode ficar desatualizado imediatamente depois.
Ainda existem módulos que fazem `memcpy`, atribuição escalar ou conversão
`wide_to_utf8` diretamente depois dessa validação. A R3 só poderá ser marcada
como concluída quando esses caminhos forem catalogados, migrados para cópia
protegida ou tiverem um contrato separado que prove que o endereço é uma região
controlada pelo runtime.

O fallback de `/proc/thread-self/mem` é uma compatibilidade de ambiente, não
uma sandbox. Ele não transforma a execução de um `.exe` em operação segura e
não amplia o escopo de arquiteturas suportadas.
