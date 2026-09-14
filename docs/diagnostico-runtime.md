# Diagnóstico — runtime e execução

Este documento contém os eventos de runtime, processo convidado, APIs, falhas, GUI, CRT, PE, SEH e loader.

## Eventos de ambiente, FLS e locale

O componente `runtime` registra `environment`, `fls` e `locale` para o núcleo
determinístico das Fases 13.6 e 13.7. Eles ficam em `stderr` e nunca misturam a
saída do convidado:

```text
[tl][runtime][info] environment operation="set" name="APPDATA" action="define" status="success"
[tl][runtime][info] fls operation="callback" detail="thread-exit" thread="2" status="success"
[tl][runtime][info] locale operation="cpinfo" code-page="437" status="success" max-char-size="1"
```

`environment` identifica alteração, expansão ou criação de bloco UTF-16;
`fls` identifica alocação, valor, callback e liberação; `locale` identifica
ACP/OEMCP, `CPINFO`, consulta de informação (inclusive `info-ex`), validação,
enumeração, tipo de caractere, formatação de data/hora e mapeamento. Erros de
ponteiro, índice, callback, flags ou buffer continuam a usar o retorno Win32 e
`GetLastError` sem executar código convidado inesperado.

## Eventos de processo e console

A Fase 13.8 acrescenta os eventos `process-context` e `console` no componente
`runtime`. O primeiro registra startup, handles padrão, tipo de arquivo,
diretório do sistema, recursos do processador, codificação de ponteiro e
inicialização de SList; o segundo registra as quantidades UTF-16 lidas ou
escritas por `ReadConsoleW`/`WriteConsoleW`:

```text
[tl][runtime][info] process-context operation="startup-info" detail="wide" thread="1" status="success"
[tl][runtime][info] console operation="write-wide" detail="18" thread="1" status="success"
```

Erros continuam expressos pelo retorno Win32 e `GetLastError`; a saída do
convidado permanece em stdout/stderr conforme o handle solicitado.

## Eventos de arquivos

A Fase 13.9 usa `filesystem` no componente `runtime` para enumeração,
alteração de atributos, metadados por handle e exclusão. O trace nunca inclui
o caminho Linux do hospedeiro:

```text
[tl][runtime][info] filesystem operation="find-first-ex" status="success" detail="basic" scope="prefix"
[tl][runtime][info] filesystem operation="set-information" status="success" detail="delete-on-close" scope="prefix"
```

O shell visual do 7-Zip registra a cópia opt-in com `SevenZipOperation`. O
evento informa a operação, o estado e somente os nomes dos arquivos; nunca
expõe o caminho Linux do hospedeiro:

```text
[tl][runtime][info] SevenZipOperation operation="copy" status="success" source-name="selected.txt" destination-name="selected.txt"
```

`destination-outside-prefix`, `destination-not-directory`, `path-outside-root`,
`no-selection` e `failed` são estados controlados; o comando só escreve quando
`TL_7ZFM_COPY_DESTINATION` aponta para um diretório existente dentro da raiz
visual e não sobrescreve um arquivo já existente.

## Eventos de segurança

A Fase 13.10 usa o evento `security` no componente `runtime` para token,
descritor e merge de ACL. Ele nunca inclui caminho Linux, SID do host ou outra
identidade do hospedeiro:

```text
[tl][runtime][info] security operation="token-information" status="success" detail="user" scope="prefix"
[tl][runtime][info] security operation="set-entries" status="success" detail="dacl" scope="prefix"
[tl][runtime][info] security operation="named-set" status="success" detail="dacl" scope="prefix"
```

Falhas de ponteiro, SID, ACL, objeto fora de `C:\` ou recurso fora do
subconjunto seguem o retorno/`GetLastError` Win32. SACL, herança complexa e
trustee não-SID retornam `ERROR_NOT_SUPPORTED`; nenhum desses eventos aplica
permissões Linux.

## Eventos WinINet

O subconjunto HTTPS de loopback da Fase 13.12 usa o evento `wininet` no
componente `runtime`. Ele registra somente operação, estado, porta, bytes
quando aplicável, esquema, destino e escopo:

```text
[tl][runtime][info] wininet operation="open" status="success" scheme="https" scope="prefix" destination="loopback" port="0"
[tl][runtime][info] wininet operation="send" status="success" scheme="https" scope="prefix" destination="loopback" bytes="17"
```

O trace nunca inclui URL completa, cabeçalhos, corpo, certificado ou caminho do
CA. Falha de biblioteca, CA, certificado ou transporte usa o mesmo evento com
estado controlado; o resultado detalhado continua em retorno WinINet e
`GetLastError`.

## Eventos OLE streams

`CreateStreamOnHGlobal` e os métodos do `IStream` em memória usam o componente
`runtime` e o evento `ole-stream`. O trace registra somente a operação, o
estado e o mecanismo de armazenamento:

```text
[tl][runtime][info] ole-stream operation="write" status="success" mechanism="memory"
```

O subconjunto não inclui conteúdo, endereços ou caminhos do host. Operações
fora do contrato (`clone`, `CopyTo` e lock de região) retornam HRESULT
controlado e não são apresentadas como compatíveis.

## Eventos WinTrust

`WinVerifyTrust` emite o evento `wintrust` no componente `runtime`, registrando
somente a operação, o resultado, a política e o mecanismo:

```text
[tl][runtime][info] wintrust operation="verify" status="success" policy="explicit-chain" mechanism="openssl"
```

Falhas de política ou de cadeia usam `status="invalid"` ou
`status="untrusted"`. O trace não inclui certificados, DER, nomes ou caminhos;
`WTD_CHOICE_FILE`, revogação e a loja do sistema não são aceitos.

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
| `guest-resource-limit` | O convidado excedeu um limite configurado de CPU ou memória. |
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

Quando a falta recai na guard page da pilha alocada para o processo convidado,
o diagnóstico identifica o estouro de pilha e adiciona `fault-type="stack-overflow"`:

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="estouro de pilha do convidado (stack overflow)" fault-type="stack-overflow" fault-address="0x7ffcf000"
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
[tl][crt][info] seh-handler disposition="4"
[tl][crt][info] exit code="0"
```

As demais APIs de CRT não registram evento (são chamadas em volume e o detalhe
relevante está na saída do convidado). O `__getmainargs` reporta o `argc` final
e o `argv[0]`, que são os argumentos do convidado encaminhados pelo CLI depois
do executável.

As APIs de `KERNEL32.dll` do subconjunto de console/CRT (por exemplo
`VirtualQuery`, `VirtualProtect`, `MultiByteToWideChar`, `GetConsoleMode`,
`Sleep`) emitem eventos do componente `runtime` com os parâmetros relevantes:

```text
[tl][runtime][info] VirtualQuery symbol="VirtualQuery" address="1400080000" region-size="4096" status="success"
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

O leitor emite um evento `image` com os campos `format`, `arch`, `entry`, `image-base`, `size-of-image` e `sections`, seguido de um evento `section` por seção (`index`, `name`, `virtual-address`, `virtual-size`, `raw-pointer`, `raw-size`, `characteristics`), um evento `import` por DLL estática, um evento `delay-import` por DLL atrasada (ambos com `dll`, `symbols`), `unwind` (`functions`, `v1`, `v2`, `epilogs`, `extended-set-fpreg`, `handlers`, `chained`) e `relocations` (`blocks`, `entries`).

Em nível `debug`, cada bloco de base relocation é registrado com `page-rva` e `entries`.

Exemplo de metadados de desempilhamento aceitos:

```text
[tl][pe][info] unwind functions="2" v1="1" v2="1" epilogs="1" extended-set-fpreg="0" handlers="0" chained="0"
```

Falhas de argumento ou de pilha nas APIs `Rtl*` são registradas no componente
`runtime` como `api-failure`, com `symbol`, `operation="unwind"` e `detail`.
Se o `ControlPc` cai em um epílogo V2, o mesmo diagnóstico é emitido e
`RtlVirtualUnwind` preserva contexto e parâmetros de saída, sem acessar a
pilha nem interpretar instruções do epílogo.

## Eventos SEH

O despachante de exceções explícitas usa eventos `runtime` com
`mechanism="x64-seh"`. Os estados são `raised`, `veh`, `frame`, `handler`,
`unwind`, `continued`, `skipped` e `failed`; o campo `code` traz o código da exceção e
`detail` identifica a etapa. Nos eventos `handler`, o trace também informa
`function-index`, `handler-rva` e `handler-data-rva`, relativos à imagem PE;
`outside` indica um ponteiro fora da imagem e `0` representa referência nula.
Esses campos são diagnósticos, não autorizam a execução de metadados não
validados. Por exemplo:

```text
[tl][runtime][info] seh state="raised" code="3762438722" detail="RaiseException" mechanism="x64-seh"
[tl][runtime][info] seh state="handler" code="3762438722" detail="search" mechanism="x64-seh" function-index="5" handler-rva="4944" handler-data-rva="16444"
[tl][runtime][info] seh state="unwind" code="3762438722" detail="target" mechanism="x64-seh"
```

Uma falha SEH controlada não executa o entry point seguinte nem código fora da
imagem; o convidado termina com o código da exceção. O subconjunto C++ x64
aceito cobre `__CxxFrameHandler3` e o formato FH4 comprimido, catch-all, tipo
exato e cleanups de término v3. Para `0xE06D7363`, o dispatcher só encaminha
handlers cujo `handler-data` passa pela validação de `FuncInfo` v3 ou FH4; tabelas estáticas de
`__C_specific_handler` e outros formatos não reconhecidos são registrados como
`seh state="skipped"` com `detail="unsupported-cxx-handler-during-search"`
ou `unsupported-cxx-handler-during-unwind`, sem usar a ponte de cleanup C++.
Uma exceção C++ sem handler termina com `detail="exceção não tratada"`; uma
nova exceção durante um funclet ativo termina com
`detail="nested-cxx-exception-unsupported"`, sem fallback ou repetição. O
rethrow nativo completo, `__finally`, sinais Linux e epílogos V2 continuam fora
desse mecanismo.

No FH4, a seleção host-side registra `detail="fh4-catch-typed"` ou
`detail="fh4-catch-all"`. O objeto de uma captura por referência é convertido
com o `PMD` validado e publicado no slot do frame; cópias por valor fora do
subconjunto simples são rejeitadas com `unsupported-fh4-catch-copy`. Durante o
unwind FH4 ainda não executado, o diagnóstico é
`detail="fh4-cleanup-not-supported"`. A transferência usa um retorno sintético
em `RSP-8` e restaura o RSP original na continuação, para preservar o
alinhamento MS x64 e o frame da função convidada.

Exemplo de captura FH4 validada:

```text
[tl][runtime][info] cxx-eh state="matched" detail="fh4-catch-typed" mechanism="msvc-x64"
[tl][runtime][info] cxx-eh state="search" detail="fh4-cleanup-not-supported" mechanism="msvc-x64" control-rva="455738" current-state="11"
[tl][runtime][info] ExitProcess symbol="ExitProcess" exit-code="0" status="success" mechanism="guest-transfer"
```

Exemplos de rejeição controlada:

```text
[tl][runtime][info] seh state="failed" code="3765269347" detail="nested-cxx-exception-unsupported" mechanism="x64-seh"
[tl][runtime][info] seh state="failed" code="3765269347" detail="exceção não tratada" mechanism="x64-seh"
```

Quando o arquivo não é um PE32+ aceitável, o leitor emite:

```text
[tl][pe][error] parse-failed status="truncated" detail="arquivo menor que o cabeçalho DOS (64 bytes)"
```

Os valores possíveis de `status` são `truncated`, `malformed`, `unsupported-architecture`, `unsupported-format` e `unsupported-mechanism`. O campo `detail` informa a condição específica rejeitada. Um arquivo de entrada regular que não seja PE válido retorna o código `4` (`MalformedPe`); o código `5` (`Unsupported`) fica reservado para PE válido incompatível ou mecanismo válido ainda não suportado, como delay-import fora do formato RVA adotado.

O subsistema de diálogos acrescenta eventos sem caminhos do hospedeiro:

```text
[tl][runtime][info] DialogBoxParamW symbol="DialogBoxParamW" template="101" controls="4" status="created"
[tl][runtime][info] IsDialogMessageW symbol="IsDialogMessageW" action="tab" status="handled" control="100"
[tl][runtime][info] IsDialogMessageW symbol="IsDialogMessageW" action="enter" status="handled" control="1"
[tl][runtime][info] EndDialog symbol="EndDialog" result="42" status="success" modal="closed"
[tl][runtime][info] DialogBoxParamW symbol="DialogBoxParamW" result="42" status="returned" modal="complete"
```

Os campos identificam somente o ordinal do template, a contagem lógica, o
comando e o resultado modal. Templates rejeitados, ponteiros inválidos,
callbacks fora da imagem convidada e tentativas aninhadas retornam erro Win32
controlado e não expõem a identidade ou os caminhos Linux.

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
| 73 | `GuestResourceLimit` | O programa convidado excedeu um limite de CPU ou memória configurado (`guest-resource-limit`). |

Na Fase 1, um arquivo regular que não é PE válido retorna `4`, e um PE válido porém incompatível (arquitetura ou formato não suportado) retorna `5`. A partir da Fase 2, uma imagem válida porém não mapeável por inconsistência estrutural retorna `4`, e uma falha de mapeamento por memória insuficiente retorna `70`. A partir da Fase 3, um PE válido com dependências não suportadas (DLL, símbolo, ordinal ou mecanismo desconhecidos) também retorna `5`, com diagnóstico completo no trace e o entry point nunca executado. Na Fase 4, `ExitProcess` gera `[tl][runtime][info]` com o código bruto e `[tl][process][info] exit`; esse código é propagado como status do processo Linux. Com o isolamento em processo filho, o convidado que termina por sinal (`SIGSEGV`, `SIGILL`, `SIGBUS`, etc.) não derruba o hospedeiro: o pai observa o sinal via `waitpid`, emite `terminated category="guest-signal"` e retorna `71`.
