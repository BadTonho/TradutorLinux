# Diagnóstico e códigos de saída

## Saída de trace

O trace é habilitado por `--trace` (todos os canais) ou `--trace=canal1,canal2` (filtrado, inspirado em `WINEDEBUG`), vai exclusivamente para `stderr` e ocupa uma linha por evento:

```text
[tl][<componente>][<nível>] <evento> chave="valor"
```

Componentes iniciais: `cli`, `pe`, `loader`, `imports`, `runtime`, `process`,
`gui`, `crt` e `install` (ver `src/diagnostics/trace.cpp`).

Filtragem: `--trace=pe,loader` emite apenas `pe` e `loader`; canal desconhecido retorna `canal de trace desconhecido` e exit `2` (`Usage`). Sem filtro, todos os canais são emitidos; a filtragem é feita em `diagnostics::is_trace_enabled` antes de `write_trace`.

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

O resolvedor emite um evento `resolved` por importação resolvida (`dll`,
`symbol` — nome ou `ordinal(N)` —, `address` e `mechanism`). O mecanismo é
`import` para a tabela estática e `delay-import` para a tabela atrasada:

```text
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="WriteFile" address="0x..." mechanism="import"
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

O CLI aceita `--timeout <segundos>` para limitar a duração do convidado (padrão
`0`, sem limite). Quando o limite expira, o hospedeiro envia `SIGKILL` ao
filho, aguarda o término, emite o evento `terminated` com `category="guest-timeout"`
e `timeout-ms`, e retorna `72` (`GuestTimeout`):

```text
[tl][process][error] terminated category="guest-timeout" timeout-ms="1000"
```

O filho ignora `SIGPIPE` antes do entry point: uma escrita do convidado em um
pipe sem leitor (ex.: `tradutorlinux prog.exe | head -c 0`) falha com
`errno=32` e `win32-error="109"` (`ERROR_BROKEN_PIPE`) no evento
`linux-failure` do `WriteFile`, em vez de matar o convidado pelo sinal.

O modo `--report` produz um relatório textual em stdout sem executar o entry
point. Cada import aparece com seu estado, seguido de `result: supported` ou
`result: unsupported` e `execution: not-attempted`.

Quando uma importação não pode ser resolvida, emite um evento `unresolved` com os campos `dll`, `symbol`, `status`, `detail` e `mechanism`:

```text
[tl][imports][error] unresolved dll="USER32.dll" symbol="MessageBoxA" status="unknown-dll" detail="módulo não registrado" mechanism="delay-import"
```

Os valores possíveis de `status` são:

| `status` | Significado |
|---|---|
| `unknown-dll` | DLL não registrada no runtime. |
| `unknown-symbol` | DLL conhecida, símbolo não exportado. |
| `unknown-ordinal` | DLL conhecida, ordinal não exportado. |
| `not-implemented` | Símbolo conhecido, sem implementação no runtime. |
| `unsupported-mechanism` | Mecanismo ainda não suportado (ex.: slot da IAT fora das seções). |

Quando qualquer importação falha, a resolução inteira falha e o processo não tem entry point executado; o runtime retorna `5` (`Unsupported`). Mesmo na falha, todas as entradas são reportadas para que o diagnóstico seja completo.

## Componente `install`

O comando `install` emite seus eventos neste componente, sempre em `stderr`.
Eles são a interface que o launcher usa para acompanhar a instalação; a saída
em `stdout` pertence exclusivamente ao programa convidado. Os estados são:

| Evento | Campos principais | Significado |
|---|---|---|
| `prepared` | `prefix`, `app-id`, `setup` | Prefixo exclusivo preparado antes de iniciar o setup. |
| `candidate` | `prefix`, `app-id`, `path` | PE32+ AMD64 novo ou alterado em `drive_c` após o setup. |
| `registered` | `prefix`, `app-id`, `path` | Executável escolhido e entrada salva no catálogo. |
| `pending` | `reason`, `prefix`, `app-id` | Setup terminou, mas o cadastro precisa de escolha ou não há candidato válido. |
| `failed` | `stage`, `prefix`, `app-id` | Entrada, parse, imports, preparação, setup, timeout/sinal ou persistência do catálogo falhou; o prefixo é preservado. |

`pending reason="selection-required"`, `pending reason="no-candidate"` e
`pending reason="invalid-app-exe"` retornam `6` (`InstallPending`). Um setup
que retorna código diferente de zero não cria entrada no catálogo; seu código
de saída é preservado e há um evento `failed stage="setup"`.

## Categorias de falha

Eventos de erro podem incluir o campo `category`:

| Categoria | Significado |
|---|---|
| `exit-process` | O convidado encerrou explicitamente com `ExitProcess`. |
| `unsupported` | Dependência ou operação fora do subconjunto suportado. |
| `invalid-image` | PE malformado ou imagem não mapeável. |
| `guest-memory` | Ponteiro ou faixa fornecida pelo convidado não é válida. |
| `linux-error` | Operação Linux falhou; pode incluir `operation`, `errno` e `win32-error`. |
| `guest-signal` | Término do convidado por sinal Linux. |
| `guest-timeout` | O convidado não terminou dentro do limite de `--timeout` e foi morto pelo hospedeiro. |
| `internal-error` | Falha inesperada do runtime. |

Exemplo de falha Linux em uma API:

```text
[tl][runtime][error] linux-failure category="linux-error" symbol="CreateFileA" operation="open" errno="2" win32-error="2"
```

O guest executa em processo filho isolado. O hospedeiro não instala um handler
geral de sinais C++; o pai observa o status do filho com `waitpid` e publica
`guest-signal` quando o entry point termina por sinal.

### Evento `terminated category="guest-signal"`

Quando o convidado morre por sinal fatal, o filho captura o endereço reportado
pelo kernel (`si_addr`) em um handler mínimo async-signal-safe e o entrega ao
pai por um pipe dedicado antes de reentregar o sinal com disposição padrão —
o status observado pelo `waitpid` não muda. O evento ganha então:

| Campo | Condição | Significado |
|---|---|---|
| `fault-address` | Sempre que o filho alcançou o handler | Endereço virtual da falta em hex (`si_addr`). Para sinais sem endereço associado (ex.: `SIGABRT`) pode ser `0x0`. |
| `rva` | Endereço dentro da imagem mapeada | `fault-address − base`, em hex. |
| `section` | RVA cai dentro de uma região nomeada | Nome da seção PE que contém o RVA. |
| `nearest-import` | Existe slot de IAT resolvido ≤ RVA | `DLL!símbolo` do slot de IAT mais próximo abaixo da falta — a melhor pista da importação em jogo. |

Campos são omitidos quando a condição não se aplica: desreferência de nulo
(`fault-address="0x0"`) está fora da imagem e não produz `rva`/`section`;
término por `SIGKILL` externo (ex.: timeout) não passa pelo handler e não
produz `fault-address`.

Exemplo com contexto completo (falta dentro de `.text`):

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória" fault-address="0x140001050" rva="0x1050" section=".text" nearest-import="KERNEL32.dll!ExitProcess"
```

Exemplo real da fixture `tl_crash.exe` (desreferência de nulo):

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória" fault-address="0x0"
```

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

O leitor emite um evento `image` com os campos `format`, `arch`, `entry`, `image-base`, `size-of-image` e `sections`, seguido de um evento `section` por seção (`index`, `name`, `virtual-address`, `virtual-size`, `raw-pointer`, `raw-size`, `characteristics`), um evento `import` por DLL estática, um evento `delay-import` por DLL atrasada (ambos com `dll`, `symbols`), `unwind` (`functions`, `handlers`, `chained`) e `relocations` (`blocks`, `entries`).

Em nível `debug`, cada bloco de base relocation é registrado com `page-rva` e `entries`.

Exemplo de metadados de desempilhamento aceitos:

```text
[tl][pe][info] unwind functions="2" handlers="0" chained="0"
```

Falhas de argumento ou de pilha nas APIs `Rtl*` são registradas no componente
`runtime` com `symbol`, `category="unwind"` e `detail`; elas nunca provocam a
execução de um handler SEH.

Quando o arquivo não é um PE32+ aceitável, o leitor emite:

```text
[tl][pe][error] parse-failed status="truncated" detail="arquivo menor que o cabeçalho DOS (64 bytes)"
```

Os valores possíveis de `status` são `truncated`, `malformed`, `unsupported-architecture`, `unsupported-format` e `unsupported-mechanism`. O campo `detail` informa a condição específica rejeitada. Um arquivo de entrada regular que não seja PE válido retorna o código `4` (`MalformedPe`); o código `5` (`Unsupported`) fica reservado para PE válido incompatível ou mecanismo válido ainda não suportado, como delay-import fora do formato RVA adotado.

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
| 6 | `InstallPending` | O setup terminou, mas o runtime não pôde escolher com segurança um executável final; o prefixo é preservado e nada é cadastrado. |
| 70 | `InternalError` | Erro interno inesperado do runtime. |
| 71 | `GuestFault` | O programa convidado terminou por um sinal Linux (`guest-signal`). |
| 72 | `GuestTimeout` | O programa convidado não terminou dentro do limite informado em `--timeout` e foi morto pelo hospedeiro (`guest-timeout`). |

Na Fase 1, um arquivo regular que não é PE válido retorna `4`, e um PE válido porém incompatível (arquitetura ou formato não suportado) retorna `5`. A partir da Fase 2, uma imagem válida porém não mapeável por inconsistência estrutural retorna `4`, e uma falha de mapeamento por memória insuficiente retorna `70`. A partir da Fase 3, um PE válido com dependências não suportadas (DLL, símbolo, ordinal ou mecanismo desconhecidos) também retorna `5`, com diagnóstico completo no trace e o entry point nunca executado. Na Fase 4, `ExitProcess` gera `[tl][runtime][info]` com o código bruto e `[tl][process][info] exit`; esse código é propagado como status do processo Linux. Com o isolamento em processo filho, o convidado que termina por sinal (`SIGSEGV`, `SIGILL`, `SIGBUS`, etc.) não derruba o hospedeiro: o pai observa o sinal via `waitpid`, emite `terminated category="guest-signal"` e retorna `71`.
