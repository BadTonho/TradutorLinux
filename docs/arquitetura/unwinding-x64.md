# Núcleo de unwinding x64

Este contrato cobre somente o desempilhamento AMD64 de uma imagem PE32+ já
mapeada. Ele não implementa despacho de exceções nem executa handlers.

## Metadados PE

`pe::parse_pe` lê `IMAGE_DIRECTORY_ENTRY_EXCEPTION` (diretório 3) em
`PeInfo::runtime_functions`. Cada entrada `RUNTIME_FUNCTION` tem 12 bytes e
contém o intervalo `[BeginAddress, EndAddress)` e o RVA de `UNWIND_INFO`.

- A tabela deve ter tamanho múltiplo de 12, caber na imagem e estar ordenada,
  sem intervalos sobrepostos.
- `UNWIND_INFO` deve estar alinhado a quatro bytes e completamente mapeável.
  As versões 1 e 2 são aceitas. A versão 3, um opcode futuro ou flags de
  mecanismo ainda não cobertas retornam `UnsupportedMechanism` (saída `5`);
  tamanho, RVA, ordem, padding, descritor ou cadeia inválidos retornam
  `Malformed`.
- São decodificados `PUSH_NONVOL`, `ALLOC_LARGE`, `ALLOC_SMALL`, `SET_FPREG`,
  `SAVE_NONVOL`, `SAVE_NONVOL_FAR`, `SAVE_XMM128`, `SAVE_XMM128_FAR` e
  `PUSH_MACHFRAME`, incluindo os operandos expandidos em bytes.
- `EHANDLER`, `UHANDLER` e `CHAININFO` são preservados. O alvo de cada cadeia
  precisa existir na mesma tabela e nenhuma cadeia pode formar ciclo.

Em V2, o leitor reconhece o `UOP_Epilog` inicial, seu tamanho comum e os
descritores seguintes. Cada offset é interpretado relativamente ao fim da
`RUNTIME_FUNCTION`; o resultado é normalizado em `UnwindInfo::epilogs` como
intervalos não sobrepostos `[begin_rva, end_rva)`. O bit epílogo-no-fim e o
descritor de padding nulo são aceitos. Flags reservadas, descritor
ausente/truncado, tamanho zero, offset fora da função ou epílogos sobrepostos
são rejeitados.

`UWOP_SET_FPREG` canônico usa `OpInfo == 0`. A extensão observada em imagens
reais é aceita somente quando `OpInfo == FrameOffset` do cabeçalho; ela é
marcada em `has_extended_set_fpreg` para trace e relatório e conserva a
semântica já usada para o frame pointer. Qualquer outra combinação falha como
mecanismo não suportado.

## CONTEXT e APIs

`runtime::ContextAmd64` reproduz o layout Windows AMD64 de 1232 bytes,
alinhado a 16 bytes. As quatro APIs abaixo são exports `KERNEL32.dll` com ABI
Microsoft x64, ligação C e `noexcept`:

| API | Contrato desta etapa |
|---|---|
| `RtlCaptureContext` | Trecho assembly sem prólogo captura RIP/RSP, registradores gerais, flags, MXCSR e XMM0–XMM15 do chamador. |
| `RtlLookupFunctionEntry` | Pesquisa somente a tabela `.pdata` da imagem PE ativa; devolve o ponteiro para a entrada mapeada e a base da imagem. |
| `RtlPcToFileHeader` | Devolve a base somente se o PC pertencer à imagem PE ativa. |
| `RtlVirtualUnwind` | Aplica os códigos do prólogo/corpo e suas cadeias a um `CONTEXT`, restaura RIP/RSP e registradores/XMM e informa handler/dados quando solicitados. Nunca chama o handler. Em epílogo V2, preserva todos os argumentos de saída e o `CONTEXT`, retorna sem handler e emite diagnóstico controlado, pois não interpreta ainda as instruções do epílogo. |

O contexto de metadados é local à thread de execução e é instalado antes do
entry point tanto no processo principal quanto em filhos de `CreateProcess`.
Ele é removido antes de destruir a imagem mapeada.

## Limites explícitos

Esta etapa não fornece `RtlUnwind`, `RtlUnwindEx`, `RaiseException`, VEH,
`UnhandledExceptionFilter`, `__C_specific_handler`, transferência de controle
para handler, nem `try/catch` do convidado. Também não decodifica as
instruções de um epílogo V2. Uma aplicação que precise de despacho SEH continua
sem suporte, mesmo que seu `.pdata` possa ser lido e um frame isolado possa
ser desempilhado.

## Diagnóstico e validação

Com `--trace`, o leitor emite:

```text
[tl][pe][info] unwind functions="2" v1="1" v2="1" epilogs="1" extended-set-fpreg="0" handlers="0" chained="0"
```

`--report` lista `mechanism: x64-unwind (...)` sem reprovar a imagem apenas
por possuir `.pdata`. A fixture `tl_unwind.exe` cobre V1. A fixture
`tl_unwind_v2.exe` é promovida deterministicamente de uma imagem-semente e
contém um descritor V2 real; ela chama `RtlVirtualUnwind` fora do epílogo,
imprime `unwind-v2\n` e verifica trace e relatório.
