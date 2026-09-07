# Unwinding e despacho SEH x64

Este contrato cobre o desempilhamento AMD64 e o despacho SEH explícito de uma
imagem PE32+ já mapeada. O subconjunto executa `__try/__except` com
`__C_specific_handler`; não implementa exceções C++, `__finally`, sinais Linux
nem epílogos V2.

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
reais é aceita quando `OpInfo` identifica um GPR válido ou quando repete
`FrameOffset` do cabeçalho; esta última forma cobre o valor 4, que é válido
como deslocamento embora RSP não seja um registrador de frame. Ela é marcada
em `has_extended_set_fpreg` para trace e relatório e conserva a semântica já
usada para o frame pointer. Qualquer outra combinação falha como mecanismo não
suportado.

## CONTEXT e APIs

`runtime::ContextAmd64` reproduz o layout Windows AMD64 de 1232 bytes,
alinhado a 16 bytes. As quatro APIs abaixo são exports `KERNEL32.dll` com ABI
Microsoft x64, ligação C e `noexcept`:

| API | Contrato desta etapa |
|---|---|
| `RtlCaptureContext` | Trecho assembly sem prólogo captura RIP/RSP, registradores gerais, flags, MXCSR e XMM0–XMM15 do chamador. |
| `RtlLookupFunctionEntry` | Pesquisa somente a tabela `.pdata` da imagem PE ativa; devolve o ponteiro para a entrada mapeada e a base da imagem. |
| `RtlPcToFileHeader` | Devolve a base somente se o PC pertencer à imagem PE ativa. |
| `RtlVirtualUnwind` | Aplica os códigos do prólogo/corpo e suas cadeias a um `CONTEXT`, restaura RIP/RSP e registradores/XMM e informa handler/dados quando solicitados. No prólogo não expõe handler; em epílogo V2 preserva contexto e parâmetros de saída, retorna sem handler e emite diagnóstico controlado. |
| `RaiseException` | Entrada assembly Microsoft x64: fotografa o chamador antes de prólogo do hospedeiro, valida até 15 parâmetros e inicia a busca SEH. Não retorna: continua o contexto convidado, entra no bloco selecionado ou encerra controladamente. |
| `RtlUnwind` / `RtlUnwindEx` | Percorrem `UHANDLER` até o frame alvo e usam um trampolim sem retorno para restaurar `CONTEXT`, incluindo GPRs, XMM, RSP, RIP e RAX. `RtlUnwind` captura o chamador e delega ao núcleo de `RtlUnwindEx`. |
| `UnhandledExceptionFilter` | Chama o filtro instalado por `SetUnhandledExceptionFilter` somente após a busca falhar; sem continuação válida, a exceção termina o convidado com seu código. |

O contexto de metadados é local à thread de execução e é instalado antes do
entry point tanto no processo principal quanto em filhos de `CreateProcess`.
`CreateThread` copia essa visão e chama o início convidado em uma pilha
convidada real. O contexto é removido antes de destruir a imagem mapeada.

## Despacho SEH

`EXCEPTION_RECORD` (152 bytes), `EXCEPTION_POINTERS` (16 bytes) e
`DISPATCHER_CONTEXT` (80 bytes) seguem o layout Windows AMD64 e são validados
antes do uso. A busca percorre frames `.pdata`, usa `RtlVirtualUnwind` e chama
somente handlers `EHANDLER`; o unwind de término percorre `UHANDLER`.

VEH usa tokens opacos removíveis. Handlers registrados com prioridade `first`
são chamados antes dos demais. Depois deles, `__C_specific_handler` interpreta
apenas `SCOPE_TABLE_AMD64`: um filtro pode continuar a execução, continuar a
busca ou selecionar o bloco `__except`. Ponteiros, tabelas, destinos, frames e
disposições inválidos encerram o convidado com trace `seh`, sem tentar executar
código fora da imagem ativa.

## Limites explícitos

Não há tradução de `SIGSEGV`/`SIGFPE`, exceções C++ (`__CxxFrameHandler*`),
`__finally`/destrutores C++, function tables dinâmicas, VEH em DLLs externas,
nem interpretação de instruções de epílogo V2. Se o PC estiver num epílogo V2,
o despacho falha como mecanismo ainda não interpretado, preservando o contexto.

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

As fixtures `tl_seh.exe` e `tl_seh_v2.exe` registram/removem VEH, lançam uma
exceção explícita numa thread convidada, selecionam um `__except` por
`__C_specific_handler` e imprimem `seh\n`. A segunda promove
deterministicamente o frame que lança para `UNWIND_INFO` V2.
