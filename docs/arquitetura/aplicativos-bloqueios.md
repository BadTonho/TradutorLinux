# Triagem dos bloqueios do corpus de aplicativos

Data: 2026-09-07

Este documento registra a triagem C1 da matriz em
`feitos/ROADMAP-LEGADO.md`, na seção legada de aplicativos. Os resultados foram obtidos com Rust ON
(`build/debug-rust`) e C++ OFF (`build/debug`), usando os arquivos temporários
`/tmp/tl-matrix-b1`, `/tmp/tl-matrix-b2` e `/tmp/tl-matrix-b3`.

## Decisões

| Aplicativo | Primeiro bloqueio observado | Evidência mínima | Decisão |
| --- | --- | --- | --- |
| `Rufus_x64.exe` | loader: `entry point fora de uma página executável` | `UPX1` tem `rwx`; `map-failed` exit `4` em ambos os backends; fixtures `ImageMapperTest.DowngradesWritableExecutableSectionToReadWrite` e `ImageMapperTest.RejectsEntryPointOutsideExecutablePage` | manter W^X e a rejeição; não criar página `RWX` nem desempacotar em runtime nesta etapa |
| `HWiNFO64.exe` | parser PE: export RVA sem intervalo file-backed | `PeReaderTest.RejectsExportDirectoryWithoutFileBackedSection`; `malformed` exit `4` em ambos | manter rejeição até existir fase explícita de desempacotamento |
| `Rockstar-Games-Launcher.exe` | convidado: `ExitProcess(3)` explícito | loader, imports, TLS e contexto inicial registrados como sucesso antes do término; stdout vazio e exit `3` em ambos | não converter código do convidado em sucesso e não alterar o loader por enquanto |
| `PuTTY` | GUI inicia `PuTTYTimerWindow`, mas permanece em execução até o timeout controlado | `/tmp/tl-matrix-c2-x11/*/6/stderr`; `xdpyinfo` confirmou Xvfb `:99` antes dos testes; sem `x11/connect-failed` | não promover como suporte concluído; tratar como cenário interativo ainda sem critério de encerramento |
| `Notepad++` | histórico C2: corrupção de heap após `startup-info` wide, observada como SIGSEGV ou SIGABRT controlado, mesmo com X11 válido | `/tmp/tl-matrix-c2-x11/*/5/stderr`, `/tmp/tl-matrix-c4/run/*/5.stderr`; ASan D1 identificou `tl_lstrcpyW` escrevendo 4 bytes em região guest de 2 bytes | corrigir a ABI das APIs `lstr*W` para UTF-16 de 16 bits; manter o aplicativo sem suporte funcional até haver interação/encerramento GUI reproduzíveis |

## Invariantes preservados

- Rust ON e C++ OFF tiveram os mesmos exit codes e stdout nos casos B2.
- Falhas de parsing e de mapeamento terminaram antes de execução do entry
  point quando essa era a causa do resultado.
- A política W^X não foi relaxada.
- Nenhum fallback Rust→C++ foi introduzido.
- Prefixos e catálogos temporários permaneceram vazios nos casos B3.

## Reprodução

Para uma nova triagem, repetir primeiro B1 e B2 com os binários já construídos.
Para casos GUI, o teste só é válido quando `xdpyinfo` confirma um Xvfb próprio
antes de iniciar o convidado. A rodada C2 confirmou esse requisito: PuTTY e
Notepad++ foram executados com X11 funcional, preservando os mesmos resultados
em Rust ON e C++ OFF. Portanto, o timeout do PuTTY e o SIGSEGV do Notepad++ não
devem ser classificados como falhas de conexão X11.

Nenhuma API nova ou mudança de loader foi justificada por esta triagem. A C2
confirmou o comportamento sob display válido; a fixture D1 e o ASan localizaram
a causa na largura host incorreta das APIs `lstr*W`, não em
`SHGetFolderPathW`. A correção usa `std::uint16_t`, preserva W^X, isolamento e
limites, e foi repetida em Rust ON/C++ OFF e Sanitizer. O timeout restante é
uma limitação de interação GUI, não uma promoção de suporte.

## Evidência E28 — probe SSH local do PuTTY

O teste `putty_ssh_local_probe` usa `tests/apps/putty/putty_ssh_smoke.cpp` para
separar a configuração GUI do primeiro passo de rede. Ele inicia um servidor
TCP somente em `127.0.0.1`, em porta efêmera, envia host e porta à janela
`PuTTY Configuration` por eventos `KeyPress`/`KeyRelease` X11 e aceita no
máximo uma linha de banner de 256 bytes. O critério de handshake exige
`SSH-*` seguido por `\r\n`; nenhuma conexão externa é permitida.

Em 2026-09-07, o alvo passou nos builds Rust ON (`build/debug-rust`) e C++ OFF
(`build/debug`). Nos dois casos a janela foi configurada, o listener recebeu
zero bytes e o processo terminou de forma controlada com `guest-timeout 72`;
o teste aceitou esse resultado somente como limitação reproduzível. Não houve
fallback, alteração de loader, API nova, DLL específica ou shim. A matriz deve
continuar distinguindo configuração GUI validada, conexão/handshake não
alcançado e SSH completo fora do suporte declarado.

## Evidência E29 — WinSock carregado dinamicamente

A fixture genérica `tests/samples/src/tl_dynamic_ws2.c` cobre o caso que o
relatório estático do PuTTY não revela: o convidado importa apenas
`LoadLibraryA`/`GetProcAddress` de `KERNEL32.dll`, carrega `ws2_32.dll`, resolve
`WSAStartup`, `WSACleanup`, `socket` e `closesocket`, abre um socket IPv4 TCP e
o fecha. Ela não contém nome, regra ou export específico do PuTTY.

`fixture_tl_dynamic_ws2_metadata`, `runtime_tl_dynamic_ws2_matches_readobj`,
`app_run_tl_dynamic_ws2` e `report_tl_dynamic_ws2_support` passaram em Rust ON
e C++ OFF em 2026-09-07. A execução inicial dentro do sandbox foi rejeitada no
socket com código WinSock `13`; a repetição fora do sandbox passou nos dois
builds. O trace confirmou a seleção do provedor builtin quatro vezes e a
saída `dynamic-ws2\n`, demonstrando que o carregamento dinâmico já funciona.

Essa evidência não promove o PuTTY nem altera o diagnóstico E28: como o
listener SSH ainda recebe zero bytes, o próximo bloqueio é determinar se a
ação `Open` da configuração chega ao diálogo genérico. Só depois de uma
fixture mínima para esse evento será considerada qualquer alteração em
`src/runtime/`.

## Evidência E30 — contratos genéricos da configuração GUI

A investigação isolou um abort causado por `GetDlgItem` não encontrar controles
criados dinamicamente depois que o TreeView recebeu itens. A correção mantém
esses controles no índice lógico do diálogo até o fim de `WM_DESTROY` e adiciona
um modelo genérico limitado para `SysTreeView32`: itens, hierarquia, seleção,
expansão, navegação, texto, `lParam` e notificação básica ao parent. Também há
foco inicial, Tab e botão padrão para janelas regulares; diálogos modais seguem
exclusivamente `IsDialogMessageW` para não processar a mesma tecla duas vezes.

Os testes `CommonControls.*` (14 casos), `runtime_gui_smoke`, os fluxos GUI de
7-Zip e PuTTY e os fixtures de WinSock dinâmico passaram no Rust ON. O baseline
C++ OFF recompilou os alvos afetados e manteve a matriz de execução existente.
O probe SSH local do PuTTY agora alcança a configuração sem o abort, mas ainda
recebe zero bytes e termina no `guest-timeout 72`; isso é uma limitação de
interação, não suporte ao SSH. Toda a implementação permanece genérica e não
contém nome, ID, DLL ou regra de seleção do PuTTY.

## Evidência E31 — matriz recursiva e ação `Open` do PuTTY

`popular_apps_recursive_report_matrix` foi adicionada ao catálogo CTest para
analisar todos os 64 arquivos PE/DLL/MSIX encontrados recursivamente em
`Aplicativos_Windows_Populares/`. Ela cobre também as cópias extraídas de
7-Zip e Notepad++, mas não inicia DLLs, pacotes ou instaladores. Rust ON e C++
OFF passaram 64/64 com a mesma distribuição: 25 análises concluídas, 2
formatos rejeitados e 37 arquiteturas/mecanismos não suportados.

O smoke `putty_ssh_local_probe` passou a preencher host/porta e enviar um
clique controlado ao botão `Open`, usando somente o harness em
`tests/apps/putty/`. Nos dois builds, o comando é aceito e a janela principal
`PuTTY` é criada. O listener de loopback ainda recebe zero bytes e o processo
termina no `guest-timeout 72`; uma observação com `strace` confirmou que não
há `socket`/`connect` do convidado depois dessa ação. Portanto a etapa valida
configuração e ativação da janela, não o handshake SSH.

## Evidência E32 — contrato não bloqueante de `PeekMessageA/W` (2026-09-15)

A auditoria da GUI encontrou uma lacuna reproduzível: `PeekMessageA/W` estava
resolvível, mas consultava apenas filas internas e cross-thread. Eventos nativos,
timers expirados e pinturas pendentes não entravam no caminho não bloqueante.
Isso afetava aplicações que usam polling em vez de `GetMessage`.

A fixture genérica `tl_peek.exe` foi criada para isolar o contrato: sob Xvfb,
ela cria uma janela, recebe `WM_KEYDOWN('Q')`, observa a mensagem com
`PM_NOREMOVE`, observa a mesma mensagem novamente com `PM_REMOVE` e termina com
exit `7`. O cenário `runtime_gui_smoke` passou com o trace das duas chamadas,
sem alterar regras específicas de aplicativo. O suporte continua limitado: os
filtros de faixa de mensagem não são aplicados.

Essa correção não muda o resultado do PuTTY. A investigação local ainda mostra
que, após a configuração e a janela de sessão, o convidado não chama
`socket`/`connect` nem envia bytes ao listener. O próximo bloqueio é o contrato
de inicialização da sessão que precede a rede, não uma ausência comprovada de
API WinSock.

## Evidência E33 — fronteira WinSock observável no PuTTY (2026-09-15)

O runtime passou a registrar a entrada das APIs WinSock relevantes com
`phase="call"`. A fixture `tl_dynamic_ws2.exe` confirma o caminho positivo de
`WSAStartup` → `socket` → `closesocket` → `WSACleanup` quando o ambiente permite
syscalls de socket.

No `putty_ssh_local_probe`, o trace registra `WSAStartup`, mas não registra
`getaddrinfo`, `socket`, `connect`, `send` ou `recv` depois de `Open`. A captura
de rede do processo confirma que os únicos `AF_INET` pertencem ao listener do
harness; o convidado não chegou à fase de abertura de conexão. Isso fecha a
ambiguidade entre “WinSock ausente” e “fluxo da aplicação não chegou à rede”.

O resultado continua sendo limitação controlada (`guest-timeout 72`). O próximo
trabalho deve localizar a transição interna da sessão antes do `GetMessageA`
ocioso, sem introduzir chamadas sintéticas nem regras específicas do PuTTY.

## Evidência E34 — transição GUI observável antes do message loop ocioso

O runtime passou a registrar uma única entrada em estado ocioso por chamada
bloqueante de `GetMessageA`, depois de esgotar filas internas, eventos nativos,
timers e pintura pendente. Também registra o início e o retorno de
`DispatchMessageA`. Assim, o trace permite localizar a última chamada GUI
observável antes de o thread voltar ao polling nativo, sem publicar ponteiros
do convidado nem alterar o comportamento da fila.

O `runtime_gui_smoke` passou com essa regressão genérica, e o
`putty_ssh_local_probe` passou exigindo um `DispatchMessageA` anterior à marca
`GetMessageA status="idle" mechanism="native-poll"`. O PuTTY ainda alcança a
janela de sessão, não chama `getaddrinfo`/`socket`/`connect`/`send`/`recv`, não
envia bytes ao listener local e termina com `guest-timeout 72`. A evidência
localiza o bloqueio depois do trabalho GUI observável e antes da rede; não
justifica implementar um shim específico ou declarar SSH suportado.

## Evidência E35 — bloqueio ativo do convidado após a janela de sessão

Uma captura ordenada do `putty_ssh_local_probe` mostrou que o primeiro
`GetMessageA status="idle"` ocorre antes de `CreateWindowExA` criar a janela
`PuTTY`. Depois dessa criação, o último diagnóstico genérico é
`locale operation="oemcp"`; não aparecem novo `DispatchMessageA`, novo estado
ocioso do `GetMessageA` nem chamadas de rede antes de
`terminated category="guest-timeout"`.

Durante a mesma reprodução, o processo filho que executa o código convidado
ficou em estado `R` e consumiu aproximadamente 99% de CPU, enquanto o
processo-pai do runtime aguardava o resultado. Isso distingue um loop ativo do
convidado de uma espera no backend X11 ou no message loop. Nenhum callback
Win32 genérico reproduzível foi localizado após a janela; a limitação continua
publicada e não há base para adicionar API ou shim específico do PuTTY.

## Evidência E36 — RIP do timeout e separação de espaços de endereços

O isolamento passou a tentar, antes do `SIGKILL`, um snapshot dos registradores
da thread principal do filho. O evento textual continua controlado quando o
kernel nega `ptrace`, mas, quando a captura está disponível, publica
`timeout-rip`; `rva`, `section` e `nearest-import` só aparecem se o endereço
estiver dentro da imagem PE. A fixture `tl_hang.exe` confirmou
`timeout-rip=0x140001012`, `rva=0x1012` e `section=.text`.

No `putty_ssh_local_probe`, uma execução fora do sandbox correlacionou o RIP
capturado com os mapas do mesmo processo: o endereço estava fora da imagem PE
em `0x140000000` e dentro do segmento executável do binário do runtime. O
offset ELF `0x3d25ce` resolve para `__cyg_profile_func_enter`, em
`src/runtime/function_trace.cpp:30`. Essa amostra identifica código do
hospedeiro no instante do timeout, não uma API Win32 responsável; por isso a
matriz continua registrando `guest-timeout 72` e não promove SSH. A causa do
loop hospedeiro ainda requer uma comparação específica do caminho de
instrumentação/trace; nenhum shim ou regra do PuTTY foi criado.

## Evidência E37 — reentrância do gravador JSON (2026-09-15)

Uma captura com `gdb` reproduziu o travamento de `--trace-json --report` no
PuTTY. A thread do gravador segurava `g_trace_json_queue_mutex` durante
`deque::pop_front`; a destruição de uma `std::string` instrumentada entrou em
`__cyg_profile_func_enter`, que tentou reenfileirar o próprio evento pela mesma
mutex. A thread principal ficou bloqueada no mesmo caminho durante
`PeParser::parse_unwind_info`. O problema era do diagnóstico do hospedeiro,
não do código convidado nem de WinSock.

O gravador agora marca sua thread interna e o hook ignora somente callbacks
originados nela. A regressão
`TraceTest.JsonWriterDoesNotDeadlockOnInstrumentedQueueDestruction` passou, e
`--trace-json --report putty_x64.exe` concluiu com exit `0`, gerando
`events-<pid>.jsonl` com 831237 bytes. O `--report` normal também concluiu com
exit `0` e os mesmos 348/348 imports resolvidos. Nenhuma API, shim ou regra
específica do PuTTY foi adicionada.

As matrizes nativas e de instalação controlada continuam separadas: 5/5
execuções e 4/4 instalações passaram nos dois builds. PE32/x86, imagens
empacotadas e pacotes incompatíveis continuam sendo rejeitados antes de
execução, e não há fallback, DLL ou tratamento específico de aplicativo no
runtime.

## Evidência E38 — custo da instrumentação no timeout do PuTTY

Em 2026-09-15, o mesmo `putty_ssh_local_probe` foi repetido com o trace textual
e com `TL_PUTTY_TRACE_JSON`. O modo textual terminou em `10,15s` (`user 8,17s`)
e o modo JSON em `10,86s` (`user 11,57s`). É uma comparação diagnóstica de uma
execução por modo, não um benchmark; ela mostra custo adicional mensurável da
instrumentação de funções e da fila JSON.

O resultado funcional foi idêntico: a configuração e a janela `PuTTY` foram
alcançadas, o listener recebeu zero bytes e o processo terminou com a
limitação controlada `guest-timeout 72`. No trace textual, depois de
`CreateWindowExA class="PuTTY"`, a última chamada genérica foi
`locale operation="oemcp"`; não houve nova chamada Win32 observável nem
`getaddrinfo`/`socket`/`connect`/`send`/`recv` antes do timeout. O snapshot textual
ficou em `__cyg_profile_func_exit`; no JSON, os snapshots ficaram em funções do
próprio gravador, variando entre `is_trace_json_enabled` e
`enqueue_function_json_trace`.

Assim, `--trace-json` é uma observabilidade intrusiva em custo e pode mudar o
local exato de uma amostra, mas não é causa suficiente do bloqueio: o mesmo
timeout ocorre sem JSON. Nenhuma API Win32, DLL, shim ou regra específica do
PuTTY foi adicionada. A investigação seguinte deve capturar o contexto de
retorno/stack e correlacioná-lo com o código convidado após a criação da janela.

## Evidência E39 — notificações assíncronas de WinSock no PuTTY

A implementação genérica de `WSAAsyncSelect` passou a associar a janela e o
evento solicitados ao token do socket, habilitar o modo não bloqueante e
integrar a consulta de prontidão ao `GetMessageA`, `PeekMessageA` e
`MsgWaitForMultipleObjectsEx`. A notificação usa `wParam` para o socket e
`lParam` para evento/erro; `recv`, `send`, `accept` e `connect` rearma a
notificação correspondente. O mapeamento de `EINPROGRESS`/`EALREADY` para
`WSAEWOULDBLOCK` também foi corrigido.

A regressão `Win32GuiTest.WsaAsyncSelectPostsReadableSocketMessage` passou fora
do sandbox com um servidor TCP de loopback, confirmou duas chegadas
`FD_READ` separadas por `recv` e foi skip controlado quando o sandbox bloqueou
o socket. No `putty_ssh_local_probe` em `build/debug`, o trace registra
`FD_CONNECT`, `FD_WRITE`, `FD_READ`, `send` e `recv`; o listener valida o banner
do cliente e envia uma resposta mínima. O processo ainda termina com
`guest-timeout 72` porque o servidor do probe não implementa o restante do
protocolo SSH. Essa evidência promove somente o transporte inicial genérico,
não o PuTTY nem o SSH completo.

## Evidência D2 — cenários GUI

O smoke externo do 7-Zip File Manager passou nos builds C++ OFF e Rust ON. Ele
abre a janela, seleciona `input.txt`, aciona `Copy`, verifica o arquivo em
`output/`, valida o evento de operação no trace e fecha a janela; os dois
processos terminaram com exit `0`. O harness agora usa `Xvfb -displayfd`, para
que a escolha do display não dependa do lock fixo `:99`.

O cenário PuTTY foi isolado em `tests/apps/putty/putty_smoke.cpp`. Sob Xvfb iniciado
com `-displayfd`, o trace registra `CreateDialogParamA`, `About PuTTY` e
`PuTTY Configuration`; o harness localiza a janela configurável e envia apenas
`WM_DELETE_WINDOW` por X11. O processo termina com exit `0` em `build/debug` e
`build/debug-rust`, sem depender da janela temporária `PuTTY: hidden timing
window`.

A correção necessária ficou restrita ao parsing de templates padrão/customizados
de diálogo, à criação de diálogos modeless com controles genéricos e à
invalidação da geração de alocações guest após `Heap/Global/LocalAlloc` e
liberações. `DestroyWindow` mantém o slot lógico válido durante `WM_DESTROY` e
somente depois o libera, evitando callback posterior com `GWLP` inválido.
Isso valida somente a abertura/fechamento da configuração; o fluxo SSH e a
compatibilidade GUI geral do PuTTY continuam fora da declaração de suporte.

## Evidência D3 — instaladores e pacote x64

Em 2026-09-07, a instalação seletiva foi repetida com Rust ON
(`build/debug-rust`) e C++ OFF (`build/debug`), sempre com prefixo e `APPDATA`
temporários, `--timeout 4`, `--cpu 3` e `--memory 512`.

- `RobloxPlayerInstaller.exe`: exit `3` nos dois builds, `RBXCRASH` seguido de
  `ExitProcess(3)` e `failed stage="setup"`; nenhum arquivo foi materializado.
- `Logitech_GHUB_x64.exe`: exit `1` nos dois builds, `ExitProcess(1)` e
  `failed stage="setup"`; nenhum catálogo ou arquivo foi criado.
- `Affinity x64.msix`: exit `4` nos dois builds antes da extração; Rust
  registrou `package-parse` `malformed` com `code=19`, `phase=3`, e o C++ OFF
  manteve a falha de `package-parse` sem campos Rust.

Os três pares tiveram stdout igual, zero arquivos no prefixo e no APPDATA e
nenhum evento `extracted` ou `registered`. O resultado confirma diagnóstico
controlado, não suporte funcional dos instaladores nem do conteúdo .NET/Mono do
Affinity. As capturas ficam em `/tmp/tl-d3-*`.

## Evidência D4 — regressão ON/OFF

Após D3, o `--report` foi repetido nos 27 arquivos do corpus. Rust ON e C++
OFF coincidiram em exit code e stdout em todos os casos: 13 sucessos, 12
rejeições PE32/x86 e 2 rejeições estruturais. O modo report não registrou
loader, runtime ou processo.

A matriz nativa repetiu oito PE32+ sob Xvfb próprio, com limites de CPU/memória
e timeout interno. Os exits e stdout coincidiram em todos, assim como os
campos semânticos dos traces. O cenário sem interação de WinRAR e das GUIs
terminou com `guest-timeout` `72`; isso é uma limitação controlada do cenário,
não uma promoção de suporte. Os diretórios APPDATA isolados permaneceram
vazios e os Xvfb foram encerrados ao final.

As capturas reproduzíveis estão em `/tmp/tl-d4-report.qUWazS` e
`/tmp/tl-d4-run-valid.7XWPLv`. A verificação de símbolos exatos não encontrou
adaptadores Rust no binário C++ OFF.

## Evidência E1 — WinRAR SFX sob Xvfb

O alvo separado `tests/apps/winrar/winrar_sfx_smoke.cpp` inicia o runtime com
limites de CPU/memória e Xvfb próprio, localiza a janela
`WinRAR self-extracting archive` por X11 e envia somente `WM_DELETE_WINDOW`.
Os builds `build/debug-rust` e `build/debug` passaram com o mesmo resultado:
`EndDialog(result=2)`, `ExitProcess(0)` e nenhum `guest-timeout`. O cenário não
escreve DLL, shim ou regra no runtime geral.

O mesmo harness também foi usado como investigação com `Return`/`IDOK`. O
trace confirmou `IsDialogMessageW action="enter"`, expansão do ambiente e
enumeração do arquivo SFX, mas a extração não terminou dentro dos limites
externos. Por isso, a etapa registra apenas o cancelamento controlado; não há
promoção da extração interativa nem do uso diário do WinRAR.

## Evidência E2 — bloqueio C++/SEH do Notepad++

O alvo separado `tests/apps/notepadpp/notepadpp_smoke.cpp` confirma primeiro a
janela `Configurator` e o diálogo `Load stylers.xml failed`, fechando ambos por
`WM_DELETE_WINDOW` quando aparecem. No caminho direto atual, os builds Rust ON
e C++ OFF produzem o mesmo trace: o import de `WinVerifyTrust` é resolvido,
mas a exceção `0xE06D7363` ocorre antes da chamada, três handlers não
compatíveis com `FuncInfo` v3 são ignorados e o processo termina em
`ExitProcess(3)`, sem `guest-signal` ou `guest-timeout`; cada rejeição agora
inclui no trace o índice da função e os RVAs do handler e de seus dados. O
resultado é uma falha controlada do convidado, não uma alegação de
compatibilidade.

O código `0xE06D7363` é a exceção C++ do aplicativo. O suporte atual cobre o
subconjunto SEH explicitamente documentado, não despacho geral de exceções C++;
por isso nenhum tratamento específico do Notepad++ foi adicionado ao runtime.

## Evidência E3 — backing store OLE genérico

`CreateStreamOnHGlobal` agora aceita um bloco válido produzido por `GlobalAlloc`
e mantém a propriedade indicada por `delete-on-release`. Os testes unitários
`OleStreamTest.*` passaram nos builds Rust ON e C++ OFF, incluindo leitura do
conteúdo, preservação do bloco e rejeição de handles desconhecidos. O smoke
SFX do WinRAR confirmou no trace a criação e liberação do stream (`status="success"`),
mas a sondagem de `IDOK` ainda não concluiu a extração dentro dos limites
externos. A mudança é uma capacidade OLE genérica; não há código específico de
WinRAR no runtime, nem promoção do fluxo de extração.

## Evidência E4 — rejeição segura de CryptQueryObject

O contrato anterior de `CryptQueryObject` retornava sucesso com tokens fixos para
loja/mensagem e contexto nulo. Isso expunha handles que não pertenciam ao
runtime e fazia o convidado avançar sobre um `CERT_CONTEXT` inexistente. O
subconjunto agora valida os ponteiros de saída, zera as saídas válidas e retorna
`ERROR_NOT_SUPPORTED` até que haja um contrato real para blob, CMS ou
Authenticode. O teste `Crypt32Test.CryptQueryObjectRejectsUnsupportedInputWithoutFabricatedHandles`
passa nos builds ON/OFF; o smoke do Notepad++ continua reproduzindo o bloqueio
controlado já documentado, sem regressão nem suporte falso a certificados.

## Evidência E5 — posição de arquivo após I/O

O `FileSlot` agora sincroniza sua posição com o descritor Linux depois de cada
`ReadFile` ou `WriteFile` bem-sucedido. Antes, um `SetFilePointer` relativo ao
deslocamento atual podia reutilizar uma posição obsoleta mantida pelo runtime;
isso fazia um consumidor genérico reler ou reescrever uma região incorreta.

Os testes de leitura, escrita, seek e metadados wide passaram sequencialmente
nos builds Rust ON e C++ OFF. A sondagem de `IDOK` do WinRAR deixou de repetir
leituras no mesmo deslocamento e avançou até enumeração, criação de janela e
chamadas de atributos; o bloqueio posterior (`set-attributes` fora do
subconjunto e exceção C++ ignorada no worker) continua sendo uma limitação
controlada. Nenhum código específico de WinRAR, DLL ou shim foi adicionado ao
runtime.

## Evidência E6 — atributo consultivo sem equivalente POSIX

`SetFileAttributesW` agora trata o valor zero como o estado normal e aceita
`FILE_ATTRIBUTE_NOT_CONTENT_INDEXED` sem criar uma representação falsa no
filesystem Linux. O bit é removido antes da aplicação dos atributos que têm
semântica POSIX; bits desconhecidos e atributos ainda não implementados
continuam retornando falha controlada. O teste de metadados passou em Rust ON
e C++ OFF, e `GetFileAttributesW` não anuncia um bit que o runtime não
persiste.

## Evidência E7 — criação de thread host sem abortar o processo

O `CreateThread` usava `std::thread` dentro de uma função `noexcept`. Quando o
host recusava uma nova thread por falta de recurso, a exceção
`std::system_error` escapava e terminava o processo com `SIGABRT`. A fronteira
agora captura a falha, libera TEB, stack e slot, retorna
`ERROR_NOT_ENOUGH_MEMORY` e registra `api-failure`; nenhum handle parcial é
publicado.

O teste unitário cobre rejeição sem estado parcial, e a sondagem `IDOK` do
WinRAR deixou de produzir `std::system_error`/`SIGABRT`, avançando para o
bloqueio posterior do convidado (`cxx-throw`/`SIGTRAP`). A mudança permanece
genérica e não adiciona código específico de aplicativo.
