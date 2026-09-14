# Memória convidada e fronteira host (R3/E11)

O ponteiro recebido de uma API Win32 pertence ao espaço de endereço do
programa convidado, mesmo quando o convidado roda no mesmo processo Linux que o
runtime. Ele deve ser tratado como entrada hostil: o mapa pode mudar entre
qualquer duas instruções, a faixa pode atravessar páginas com permissões
diferentes e um endereço aritmeticamente válido pode não estar mapeado.

## Inventário da fronteira

| Categoria | Exemplos no runtime | Risco | Tratamento atual |
|---|---|---|---|
| Strings de entrada | `validate_mapped_cstring`, `validate_mapped_wstring`, caminhos, `WININET`, `MPR`, `IPHLPAPI`, `GetPrivateProfile*` e `WS2_32` | Leitura após snapshot ou string sem terminador | Validação por blocos copiados com `read_guest_memory` nos caminhos migrados |
| Estruturas de entrada | `NETRESOURCEW`, arrays de handles, `URL_COMPONENTS`, `sockaddr`, parâmetros de arquivos, rede, janela, thread e segurança | Tamanho/layout inválido e ponteiros internos inválidos | Validar faixa, copiar para objeto host e validar campos apontados quando o caminho já foi migrado |
| Buffers de saída | `ReadFile`/`WriteFile`, `FindFirstFileA/W`, caminhos, `GetProcessMemoryInfo`, console/tempo, memória virtual, sincronização, `IPHLPAPI`, `GetPrivateProfile*`, thread, `WS2_32`, WININET, locale, GUI e handles | Escrita em página desmontada ou somente leitura | `write_guest_memory` nos caminhos migrados; outras APIs ainda usam validação seguida de escrita direta |
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

O lote `SHELL32` segue esse contrato para suas estruturas de entrada e strings:
`NOTIFYICONDATA`, `GUID`, `SHELLEXECUTEINFO`, `SHFILEOPSTRUCT`,
`CommandLineToArgvW` e as entradas de `ShellExecuteA/W` são primeiro copiados
para objetos host. As saídas de `SHELLEXECUTEINFO` e `SHFILEOPSTRUCT` são
publicadas de volta com `write_guest_memory`; nenhum desses caminhos
desreferencia o ponteiro convidado após uma validação por snapshot.

O lote `USER32/misc` aplica o mesmo contrato a `PAINTSTRUCT`, `RECT`,
`DrawTextA/W`, `LoadStringA/W`, `CharUpperW`/`CharLowerW`, estruturas de
display e `wsprintfW`. Os cálculos são feitos em cópias host e somente os
resultados finais são publicados pela primitiva protegida.

O lote `USER32/window` aplica o contrato a saídas de geometria, texto e
classe, a `FindWindowA/W`, `GetClassInfoW`, `MapWindowPoints` e às saídas de
região. Os nomes são copiados antes da busca e as estruturas/arrays são
transformados em objetos host antes da publicação.

O sublote `USER32/window-registration` aplica o contrato a
`RegisterClass(A/W)`, `RegisterClassEx(A/W)` e às strings de
`CreateWindowExA/W`. As estruturas e nomes são copiados para objetos locais
antes de conversão, busca de classe ou montagem de `CREATESTRUCT`; o endereço do
procedimento de janela é validado como callback convidado, sem desreferência
direta de campos da estrutura original.

O sublote `USER32/menu` aplica o contrato a `MENUITEMINFO`, aos textos de
`AppendMenuA/W`, ao nome de recurso de `LoadMenuW` e às saídas de
`GetMenuBarInfo`/`GetMenuStringW`. Estruturas aninhadas são lidas para objetos
host, e buffers de texto e estruturas de saída são publicados somente por
`write_guest_memory`.

O sublote `USER32/dialog` aplica o contrato aos nomes de template de diálogos,
às entradas de `MessageBoxA/W`, `SetDlgItemTextW` e `IsDialogMessageW`. Os
templates, strings e mensagens são copiados para objetos host antes de buscar
recursos, abrir a GUI ou despachar comandos.

O sublote `USER32/message-core` aplica o contrato às estruturas de mensagem de
`GetMessageA/W`, `PeekMessageA/W`, `TranslateMessage` e `DispatchMessageA/W`,
aos arrays de handles de `MsgWaitForMultipleObjectsEx` e às saídas de teclado e
timeout. Mensagens e arrays são lidos para objetos locais, e cada publicação
usa a primitiva protegida.

O sublote `USER32/message-tree` aplica o contrato às estruturas e textos do
`SysTreeView32`. Os pedidos de inserção, alteração e consulta são copiados para
buffers host; textos aninhados são lidos com cópias limitadas, e as respostas
são publicadas por `write_guest_memory`.

O sublote `USER32/message-controls` aplica o contrato às estruturas de
`TB_ADDBUTTONS` e `LVM_*`, ao array de `SB_SETTEXT`/partes e às strings de
status bar, edit e combo box em variantes ANSI/Wide. Estruturas e strings são
copiadas para objetos host; saídas de item e texto do list-view são publicadas
por `write_guest_memory`, sem desreferenciar ponteiros convidados depois de
uma validação por fotografia.

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
