# OLE32: streams em memória

A primeira entrega de automação OLE cobre `ole32.dll!CreateStreamOnHGlobal`
para fixtures que precisam de um `IStream` temporário sem depender de COM
registrado, `HGLOBAL` do Windows ou armazenamento do host.

## Contrato

`CreateStreamOnHGlobal(NULL, delete-on-release, ppStream)` cria um objeto
`IStream` em memória. A fronteira Microsoft x64 é declarada em
`include/tradutorlinux/runtime/ole32.hpp`; os 14 slots da vtable têm
assinaturas `ms_abi` explícitas. O subconjunto exercitado por
`tests/samples/tl_stream.c` implementa:

- `QueryInterface` para `IUnknown` e `IStream`, `AddRef` e `Release`;
- `Read`/`Write` com contagem parcial e `S_FALSE` no fim do fluxo;
- `Seek` (`SET`, `CUR` e `END`), `SetSize`, `Stat`, `Commit` e `Revert`.

O buffer cresce em páginas de 4096 bytes, é inicializado com zero quando
expandido e é liberado quando a última referência é descartada. O argumento
`HGLOBAL` precisa ser `NULL`; cópia de streams, clonagem e regiões bloqueadas
retornam `E_NOTIMPL` ou `STG_E_INVALIDFUNCTION`, conforme o método. Não há
`IStorage`, `IDispatch`, `OLEAUT32`, persistência, marshal ou execução do
Rockstar.

## Evidência

`tl_stream.exe` valida a vtable a partir do PE convidado, round-trip de dados,
posicionamento relativo ao fim, `STATSTG`, redimensionamento, referências e
liberação. O teste de metadados, o `--report` e o runtime são registrados no
CTest; o evento `ole-stream` mantém a operação e o resultado no `stderr`, sem
misturar diagnósticos com `stdout` do convidado.
