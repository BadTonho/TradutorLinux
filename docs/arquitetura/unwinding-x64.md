# Unwinding e despacho SEH x64

Este contrato cobre o desempilhamento AMD64 e o despacho SEH explícito de uma
imagem PE32+ já mapeada. O subconjunto executa `__try/__except` com
`__C_specific_handler` e implementa parcelas validadas de exceções C++ por
`__CxxFrameHandler3` e pelo formato comprimido FH4; não implementa `__finally`,
sinais Linux nem epílogos V2.

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
| `RtlVirtualUnwind` | Aplica os códigos do prólogo/corpo e suas cadeias a um `CONTEXT`, restaura RIP/RSP e registradores/XMM e informa handler/dados quando solicitados. Leituras da stack são limitadas à faixa ativa entre `TEB.StackLimit` e `TEB.StackBase`. No prólogo não expõe handler; em epílogo V2 preserva contexto e parâmetros de saída, retorna sem handler e emite diagnóstico controlado. |
| `RaiseException` | Entrada assembly Microsoft x64: fotografa o chamador antes de prólogo do hospedeiro, valida até 15 parâmetros e inicia a busca SEH. Não retorna: continua o contexto convidado, entra no bloco selecionado ou encerra controladamente. |
| `RtlUnwind` / `RtlUnwindEx` | Capturam o chamador na fronteira assembly, percorrem e chamam cada `UHANDLER` até o frame alvo, independentemente do código do registro, e usam um trampolim sem retorno para restaurar `CONTEXT`, incluindo GPRs, XMM, RSP, RIP e RAX. O subconjunto de `RtlUnwindEx` também interpreta `STATUS_UNWIND_CONSOLIDATE` (`0x80000029`): chama o callback indicado pelo primeiro parâmetro e usa o RIP devolvido. `RtlUnwind` delega ao mesmo núcleo. |
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
busca ou selecionar o bloco `__except`. No unwind explícito, os `UHANDLER`
recebem o registro com `EXCEPTION_UNWINDING` e, no frame-alvo,
`EXCEPTION_TARGET_UNWIND`; o registro de consolidação chama o callback convidado
validado e continua no RIP retornado. Ponteiros, tabelas, destinos, frames e
disposições inválidos encerram o convidado com trace `seh`, sem tentar executar
código fora da imagem ativa.

## Limites explícitos

Não há tradução de `SIGSEGV`/`SIGFPE`, `__finally`, function tables dinâmicas ou
VEH em DLLs externas. O suporte C++ x64 inclui o `__CxxFrameHandler3` com
`FuncInfo` v3 relativo à imagem e o despacho host-side do formato FH4
comprimido, além de um cleanup de término do frame-alvo emitido por
`stateUnwindMap`, `catch(...)` sem tipo, captura por `type descriptor` exato e
transferência para funclets LLVM. No FH4, o runtime decodifica `FuncInfo4`,
`UnwindMap`, `TryBlockMap`, `IPMap` e `HandlerMap` com limites checked; captura
por referência aplica o `PMD` validado e cópia por valor só é aceita para tipo
simples sem copy constructor e até 4096 bytes. O wrapper guest
`__GSHandlerCheck_EH4` não é executado como se fosse um handler v3.
A cadeia de ações do frame-alvo v3 é percorrida com limite de 64 estados,
rejeição de ciclos e `RVA=0`/`0xffffffff` como fim sem ação; cada funclet
retorna pelo trampoline para o próximo cleanup ou para o catch. Cleanups FH4 de
frames intermediários ou do frame-alvo ainda são registrados como
`fh4-cleanup-not-supported`; copy constructors arbitrários, rethrow e
`__CxxFrameHandler` legado seguem a busca controlada e não são declarados
suportados. Uma nova exceção C++ durante um `catch` ou cleanup ativo é rejeitada
com `nested-cxx-exception-unsupported`; isso evita redirecionar a exceção ao
mesmo handler indefinidamente. Uma exceção C++ sem handler termina com o
diagnóstico controlado `exceção não tratada`. Se o PC estiver num epílogo V2, o
despacho falha como mecanismo ainda não interpretado, preservando o contexto.
Leituras de endereços de retorno, registradores salvos e slots de funclet só
são válidas dentro da stack convidada ativa (`TEB.StackLimit` inclusive até
`TEB.StackBase` exclusivo); uma faixa hospedeira que apenas pareça mapeada não
é aceita.
Quando `handler-data` não representa um `FuncInfo` v3 ou FH4 validável — por exemplo,
uma tabela estática de `__C_specific_handler` — o dispatcher não chama esse
handler como C++ nem prepara um cleanup/catch transferido; registra
`unsupported-cxx-handler-during-search` ou
`unsupported-cxx-handler-during-unwind` e segue a rejeição controlada.

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
`__C_specific_handler` e imprimem `seh\n`. `tl_seh.exe` também valida a
chamada de `UHANDLER` e o callback de consolidação de `RtlUnwindEx`; a segunda
promove deterministicamente o frame que lança para `UNWIND_INFO` V2.

## Contrato mínimo de MSVC C++ EH

O projeto mantém `tests/samples/src/tl_cxx_eh.ll` como fixture genérica de
contrato. Ela é gerada pelo backend WinEH do LLVM com a personalidade
`__CxxFrameHandler3`, `catchswitch`, `catchpad` e `catchret`, e ligada como
PE32+ com uma importação explícita de `msvcrt.dll!__CxxFrameHandler3`. O teste
de metadados confirma `.pdata`, `.xdata`, os flags de exceção/terminação e as
importações nos builds Rust ON e C++ OFF.

O contrato aceito é estrito: magic `0x19930522`, `MaxState` interpretado como
a quantidade de entradas do `stateUnwindMap`, contagens limitadas, offsets RVA
relativos à imagem ativa, `tryLow <= tryHigh`, mapas de IP ordenados, handlers
dentro da imagem e handler catch-all com descritor de tipo nulo. Handlers
tipados só são aceitos quando o `type descriptor` coincide exatamente com um
tipo do `CatchableTypeArray` da exceção; nenhuma conversão é tentada. Cada entrada
de unwind valida `toState`; RVA zero e `0xffffffff` significam ausência de
ação, e qualquer outra ação precisa estar dentro da imagem. O leitor usa
`memcpy` e aritmética checked; versões, ponteiros, contagens ou ranges
desconhecidos retornam `ContinueSearch` e deixam a decisão no dispatcher
controlado. Nenhum `FuncInfo` ou ponteiro do convidado é mantido fora da
chamada. Antes de tratar `0xE06D7363` como exceção C++, o dispatcher valida o
primeiro RVA de `handler-data` como `FuncInfo` v3 ou FH4 completo; formatos
estáticos de SEH não passam por essa ponte.

Durante a transferência, o contexto Microsoft x64 é salvo por thread, o frame
é passado no `RDX` exigido pelo funclet e o retorno de `cleanupret` passa por um
trampoline que restaura o contexto original do frame. O catch FH4 recebe um
endereço de retorno sintético em `RSP-8`, mantendo o alinhamento esperado pelo
prólogo MS x64; após o `RET`, a continuação restaura o RSP original do ponto que
lançou a exceção. O endereço de retorno do catch é revalidado e reescrito depois
da chamada host, pois a ponte pode usar temporariamente a pilha convidada. O
retorno do `catchret` passa por outro trampoline que valida o destino na imagem
antes de restaurar o contexto. Isso evita saltar diretamente para um endereço
devolvido pelo funclet ou executar um ponteiro externo à imagem.

A fixture `tl_cxx_eh` cobre a busca e a captura de `catch(...)`; `tl_cxx_eh_typed`
cobre uma captura por `type descriptor` exato usando
`ThrowInfo`/`CatchableTypeArray`; conversões de herança e outros ajustes de
objeto não são aplicados. A fixture `tl_cxx_eh_cleanup` adiciona
`stateUnwindMap`, um destrutor executado durante o unwind e `cleanupret`;
`tl_cxx_eh_cleanup_chain` adiciona dois funclets em cadeia e só permite a
continuação quando ambos os marcadores foram executados. Os testes de metadata
e execução terminam em `ExitProcess(0)` nos builds Rust ON e C++ OFF.
`tl_cxx_eh_nested` comprova a rejeição determinística de reentrada durante um
catch, sem loop; `tl_cxx_eh_unhandled` comprova o caminho sem handler. O rethrow
nativo da ABI MSVC, cleanups de frames intermediários e conversões de tipo
continuam fora do contrato antes de repetir o cenário de extração do WinRAR.

O teste real `notepadpp_fh4_headless_smoke` executa o `notepad++.exe` do corpus
sem servidor gráfico, confirma os catches FH4 tipados, ausência de
`guest-signal`/`guest-timeout` e saída convidada `ExitProcess(0)`. O smoke GUI
`notepadpp_real_gui_smoke` permanece separado e pode ser ignorado quando o
Xvfb não consegue abrir um display.
