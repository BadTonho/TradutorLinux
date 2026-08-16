# Diagnóstico e códigos de saída

## Saída de trace

O trace é habilitado por `--trace`, vai exclusivamente para `stderr` e ocupa uma linha por evento:

```text
[tl][<componente>][<nível>] <evento> chave="valor"
```

Componentes iniciais: `cli`, `pe`, `loader`, `imports`, `runtime`, `process` e `gui`.

Níveis iniciais: `debug`, `info`, `warning` e `error`.

Valores sempre usam aspas duplas. Dentro deles, barra invertida, aspas, quebra de linha, retorno de carro e tabulação são escapados como `\\`, `\"`, `\n`, `\r` e `\t`. A forma é legível por humanos e estável para ferramentas simples de parsing.

Exemplo atual:

```text
[tl][cli][info] input path="tests/samples/generated/tl_hello.exe"
[tl][pe][info] image format="PE32+" arch="x86-64" entry="0x1000" image-base="0x140000000" size-of-image="0x4000" sections="3"
[tl][pe][info] section index="0" name=".text" virtual-address="0x1000" virtual-size="0x90" raw-pointer="0x400" raw-size="0x200" characteristics="0x60000020"
[tl][pe][info] import dll="KERNEL32.dll" symbols="ExitProcess,GetStdHandle,WriteFile"
[tl][pe][info] relocations blocks="0" entries="0"
[tl][loader][info] mapped preferred-base="0x140000000" base="0x140000000" delta="0x0" size="0x4000" at-preferred="sim" relocations-applied="0"
[tl][loader][info] region name=".text" rva="0x1000" size="0x200" permissions="r-x"
[tl][loader][info] region name=".rdata" rva="0x2000" size="0x200" permissions="r--"
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="ExitProcess" address="0x... "
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="GetStdHandle" address="0x..."
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="WriteFile" address="0x..."
[tl][loader][info] unmap base="0x140000000"
```

## Eventos do componente `imports`

O resolvedor da Fase 3 emite um evento `resolved` por importação resolvida (`dll`, `symbol` — nome ou `ordinal(N)` — e `address`):

```text
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="WriteFile" address="0x..."
```

As chamadas de console são registradas pelo componente `runtime` com os
tamanhos e resultados relevantes. O componente `process` registra o retorno
do código convidado:

```text
[tl][process][info] exit exit-code="0" explicit="sim"
```

A partir do diagnóstico de falhas, o convidado executa em um processo filho
isolado (`fork`/`waitpid`). O processo hospedeiro prepara o PE, o mapeamento e
os imports, inicia o convidado no filho, espera o término e distingue saída
normal de término por sinal. Os handlers de sinais fatais do filho são
restaurados para o padrão antes do entry point, para que um `SIGSEGV` do
convidado não seja interceptado pelo runtime do hospedeiro (ex.: AddressSanitizer).

Quando o convidado termina por um sinal Linux, o componente `process` emite um
evento `terminated` de nível `error` com a categoria `guest-signal`, o nome do
sinal e uma descrição:

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória"
```

Falha ao criar o processo filho emite o mesmo evento com `category="internal-error"`.
O exit code bruto do convidado é transmitido por um pipe interno e propagado
como status do processo Linux; quando o convidado morre por sinal, o hospedeiro
retorna `71` (`GuestFault`).

O modo `--report` produz um relatório textual em stdout sem executar o entry
point. Cada import aparece com seu estado, seguido de `result: supported` ou
`result: unsupported` e `execution: not-attempted`.

Quando uma importação não pode ser resolvida, emite um evento `unresolved` com os campos `dll`, `symbol`, `status` e `detail`:

```text
[tl][imports][error] unresolved dll="USER32.dll" symbol="MessageBoxA" status="unknown-dll" detail="módulo não registrado"
```

Os valores possíveis de `status` são:

| `status` | Significado |
|---|---|
| `unknown-dll` | DLL não registrada no runtime. |
| `unknown-symbol` | DLL conhecida, símbolo não exportado. |
| `unknown-ordinal` | DLL conhecida, ordinal não exportado. |
| `not-implemented` | Símbolo conhecido, sem implementação no runtime. |
| `unsupported-mechanism` | Mecanismo ainda não suportado (ex.: delay imports, IAT fora das seções). |

Quando qualquer importação falha, a resolução inteira falha e o processo não tem entry point executado; o runtime retorna `5` (`Unsupported`). Mesmo na falha, todas as entradas são reportadas para que o diagnóstico seja completo.

## Categorias de falha

Eventos de erro podem incluir o campo `category`:

| Categoria | Significado |
|---|---|
| `exit-process` | O convidado encerrou explicitamente com `ExitProcess`. |
| `unsupported` | Dependência ou operação fora do subconjunto suportado. |
| `invalid-image` | PE malformado ou imagem não mapeável. |
| `guest-memory` | Ponteiro ou faixa fornecida pelo convidado não é válida. |
| `linux-error` | Operação Linux falhou; pode incluir `operation`, `errno` e `win32-error`. |
| `guest-signal` | Futuramente, término do convidado por sinal Linux. |
| `internal-error` | Falha inesperada do runtime. |

Exemplo de falha Linux em uma API:

```text
[tl][runtime][error] linux-failure category="linux-error" symbol="CreateFileA" operation="open" errno="2" win32-error="2"
```

O guest executa em processo filho isolado. O hospedeiro não instala um handler
geral de sinais C++; o pai observa o status do filho com `waitpid` e publica
`guest-signal` quando o entry point termina por sinal.

## Componente `gui`

O protótipo X11 usa um subconjunto de `USER32.dll`: `MessageBoxA` (somente com
`hWnd == NULL` e `type == 0`) e as APIs de janela/eventos documentadas em
[`gui-x11.md`](arquitetura/gui-x11.md). Falha ao abrir o display, fechar a janela
sem confirmação ou criar uma janela retorna o comportamento documentado ao
programa convidado; a chamada não lança exceções nem compromete o diagnóstico
do loader.

As APIs de janela emitem eventos do componente `runtime`:

```text
[tl][runtime][info] RegisterClassExA symbol="RegisterClassExA" class="tlwin" atom="1" status="success"
[tl][runtime][info] CreateWindowExA symbol="CreateWindowExA" class="tlwin" window="Ola do Windows no Linux!" status="success"
[tl][runtime][info] TranslateMessage symbol="TranslateMessage" message="WM_CHAR" wparam="113" status="translated"
[tl][runtime][info] SetTimer symbol="SetTimer" id="1" elapsed-ms="200" status="success"
[tl][runtime][info] GetMessageA symbol="GetMessageA" message="WM_TIMER" id="1" status="delivered"
[tl][runtime][info] KillTimer symbol="KillTimer" id="1" status="success" result="killed"
[tl][runtime][info] GetStockObject symbol="GetStockObject" object="0" status="success" mechanism="token"
[tl][runtime][info] BeginPaint symbol="BeginPaint" status="success" mechanism="hdc=hwnd" result="painting"
[tl][runtime][info] TextOut symbol="TextOut" x="10" y="10" length="17"
[tl][runtime][info] EndPaint symbol="EndPaint" status="success" result="painted" mechanism="hdc=hwnd"
[tl][runtime][info] GetMessageA symbol="GetMessageA" message="WM_QUIT" exit-code="0" result="quit"
```

`GetMessageA` registra somente eventos notáveis: o fim do loop (`WM_QUIT`),
confirmando que `PostQuitMessage` foi acionado, e cada timer expirado entregue
(`WM_TIMER` com `id` e `status` `delivered`). As mensagens ordinárias não são
registradas para não poluir o trace. `TranslateMessage` registra a conversão de
um `WM_KEYDOWN` em `WM_CHAR` com o `wparam` (código do caractere) e `status`
(`translated` quando houve conversão; o evento só é emitido nesse caso).

As APIs do GDI mínimo emitem eventos do mesmo componente: `SetTimer`/`KillTimer`
confirmam criação e remoção de timers; `GetStockObject` registra o objeto e o
mecanismo `token`; `BeginPaint`/`EndPaint` registram o par de pintura (o `HDC`
é o próprio `HWND`); `TextOut` registra as coordenadas e o comprimento do texto
desenhado.

## Componente `crt`

As funções da fronteira do `msvcrt.dll` mínimo emitem eventos do componente
`crt` no nível `info` para o startup e o término, e no nível `error` para
falhas que encerram o convidado:

```text
[tl][crt][info] getmainargs argc="2" argv0="xxd.exe"
[tl][crt][error] amsg-exit code="1"
[tl][crt][error] abort
[tl][crt][error] seh-stub
[tl][crt][info] exit code="0"
```

As demais APIs de CRT não registram evento (são chamadas em volume e o detalhe
relevante está na saída do convidado). O `__getmainargs` reporta o `argc` final
e o `argv[0]`, que são os argumentos do convidado encaminhados pelo CLI depois
do executável.

As APIs de `KERNEL32.dll` do subconjunto de console/CRT (por exemplo
`VirtualQuery`, `VirtualProtect`, `MultiByteToWideChar`, `GetConsoleMode`,
`SetUnhandledExceptionFilter`, `Sleep`) emitem eventos do componente `runtime`
com os parâmetros relevantes:

```text
[tl][runtime][info] VirtualQuery symbol="VirtualQuery" address="1400080000" region-size="4096" status="success"
[tl][runtime][info] SetUnhandledExceptionFilter symbol="SetUnhandledExceptionFilter" handler="5368718352" previous="0" mechanism="registrado-sem-invocacao"
```

## Processo convidado: TEB e segmento GS

O `__mingw_CRTStartup` (crt2.o) lê o TEB x64 via `%gs:[0x30]` para obter o
`StackBase` (offset `0x8`) e marcar a inicialização. Em Linux x86-64 o segmento
`GS` é livre, então a fronteira aloca um TEB de uma página (`mmap` anônimo),
aponta o `GS` para ele com `arch_prctl(ARCH_SET_GS)` imediatamente antes de
chamar o entry point do convidado e restaura o `GS` do hospedeiro (e libera o
TEB) no retorno — tanto na saída normal quanto no `longjmp` do `ExitProcess`
capturado. Campos preenchidos: `Self`, `StackBase` (= topo da pilha convidada)
e `StackLimit` (= `StackBase - kGuestStackSize`). Um convidado que não lê o
TEB (fixtures sem CRT) não é afetado.

## Eventos do componente `pe`

O leitor da Fase 1 emite um evento `image` com os campos `format`, `arch`, `entry`, `image-base`, `size-of-image` e `sections`, seguido de um evento `section` por seção (`index`, `name`, `virtual-address`, `virtual-size`, `raw-pointer`, `raw-size`, `characteristics`), um evento `import` por DLL (`dll`, `symbols`) e um evento `relocations` (`blocks`, `entries`).

Em nível `debug`, cada bloco de base relocation é registrado com `page-rva` e `entries`.

Quando o arquivo não é um PE32+ aceitável, o leitor emite:

```text
[tl][pe][error] parse-failed status="truncated" detail="arquivo menor que o cabeçalho DOS (64 bytes)"
```

Os valores possíveis de `status` são `truncated`, `malformed`, `unsupported-architecture` e `unsupported-format`. O campo `detail` informa a condição específica rejeitada. A partir da Fase 1, um arquivo de entrada regular que não seja PE válido retorna o código `4` (`MalformedPe`); o código `5` (`Unsupported`) fica reservado para arquivos PE válidos mas incompatíveis (arquitetura ou formato).

## Eventos do componente `loader`

O mapeador da Fase 2 emite um evento `mapped` com os campos `preferred-base`, `base`, `delta`, `size`, `at-preferred` (`sim` ou `não`) e `relocations-applied`, seguido de um evento `region` por região mapeada (`name`, `rva`, `size`, `permissions` em `r-x`, `r--`, `rw-` ou `---`) e um evento `unmap` (`base`) ao liberar a imagem.

Quando o mapeamento falha:

```text
[tl][loader][error] map-failed status="invalid-image" detail="seções se sobrepõem na imagem"
```

Os valores possíveis de `status` são `invalid-image` e `out-of-memory`. Falhas de imagem malformada retornam `4` (`MalformedPe`); falta de memória retorna `70` (`InternalError`).

Quando a imagem é mapeada fora do endereço preferencial e não possui diretório de relocations, o runtime registra o aviso honesto:

```text
[tl][loader][warning] cannot-relocate reason="imagem sem diretório de relocations" delta="0x... "
```

## Códigos de saída do host

| Código | Nome | Significado |
|---:|---|---|
| 0 | `Success` | A operação solicitada terminou corretamente. |
| 2 | `Usage` | Argumentos inválidos, ausentes ou incompatíveis. |
| 3 | `InputUnavailable` | O arquivo informado não existe, não é regular ou não pode ser acessado. |
| 4 | `MalformedPe` | O arquivo é reconhecido como PE malformado ou truncado. |
| 5 | `Unsupported` | PE válido de arquitetura ou formato ainda não suportado (ex.: PE32/x86), ou etapa futura do runtime não disponível. |
| 70 | `InternalError` | Erro interno inesperado do runtime. |
| 71 | `GuestFault` | O programa convidado terminou por um sinal Linux (`guest-signal`). |

Na Fase 1, um arquivo regular que não é PE válido retorna `4`, e um PE válido porém incompatível (arquitetura ou formato não suportado) retorna `5`. A partir da Fase 2, uma imagem válida porém não mapeável por inconsistência estrutural retorna `4`, e uma falha de mapeamento por memória insuficiente retorna `70`. A partir da Fase 3, um PE válido com dependências não suportadas (DLL, símbolo, ordinal ou mecanismo desconhecidos) também retorna `5`, com diagnóstico completo no trace e o entry point nunca executado. Na Fase 4, `ExitProcess` gera `[tl][runtime][info]` com o código bruto e `[tl][process][info] exit`; esse código é propagado como status do processo Linux. Com o isolamento em processo filho, o convidado que termina por sinal (`SIGSEGV`, `SIGILL`, `SIGBUS`, etc.) não derruba o hospedeiro: o pai observa o sinal via `waitpid`, emite `terminated category="guest-signal"` e retorna `71`.
