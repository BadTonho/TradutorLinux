# GUI plain com X11 e Wayland

Este é um protótipo isolado para avaliar GUI, não uma promessa de compatibilidade
Win32 gráfica geral. As janelas convidadas usam `libX11` ou `libwayland-client`
diretamente, sem Qt, GTK, SDL ou Wine; o launcher Qt permanece separado como
catálogo e interface de diagnóstico.

## Seleção do backend plain

`TL_GUI_BACKEND=auto` (padrão) tenta Wayland quando `WAYLAND_DISPLAY` está
disponível e depois X11. `TL_GUI_BACKEND=wayland` e `TL_GUI_BACKEND=x11` tornam
a exigência explícita. Falhas de conexão, globals ausentes e buffers inválidos
são registradas em `stderr`, sem substituir silenciosamente o backend pedido.

O dispatcher comum está em `src/gui/platform.cpp`; os backends ficam em
`src/gui/x11.cpp` e `src/gui/wayland.cpp`. O Wayland usa `xdg-shell`, `wl_shm`
e `xkbcommon`, com o protocolo gerado pelo CMake.

## Camada X11

`src/gui/x11.cpp` abre um único display (lazy) compartilhado por todas as
janelas do processo. Cada janela é um token opaco num pool fixo (`NativeWindow`).
Os eventos X11 são drenados pelo runtime via `next_window_event` e traduzidos
para o subconjunto Win32: `Expose` → `WM_PAINT`, `ButtonPress` → `WM_LBUTTONDOWN`,
`KeyPress` → `WM_KEYDOWN`, `KeyRelease` → `WM_KEYUP`, `KeyPress` com caractere →
`WM_CHAR` (via `TranslateMessage`) e `WM_DELETE_WINDOW` (protocolo de janela) →
`WM_CLOSE`. O pump também despacha timers expirados como `WM_TIMER`.

O pump mantém uma **fila de eventos por janela**: a cada consulta, todos os
eventos X11 pendentes do display são demultiplexados para a fila da janela-alvo
(de acordo com `event.xany.window`) e a próxima entrada da janela consultada é
devolvida. Eventos de janelas desconhecidas são descartados; nada é perdido
entre janelas conhecidas, independentemente da ordem de consulta. Assim, duas
janelas simultâneas recebem eventos independentes mesmo quando o message loop
consulta `GetMessageA` sem filtro de `hWnd`.

Quando `TL_GUI_AUTOCLOSE_MS` é diferente de `0`, o pump gera `CloseRequested`
(→ `WM_CLOSE`) após aproximadamente 100 ms, permitindo testes de integração
numa sessão X11 sem interação humana. O ambiente precisa fornecer `DISPLAY`
acessível. O display X11 do processo é fechado no teardown (`DisplayCloser` em
`src/gui/x11.cpp`), de modo que o runtime não deixa conexão aberta ao sair.

Menus popup são modais, mas não ficam bloqueados indefinidamente: o backend
usa `TL_GUI_POPUP_TIMEOUT_MS` para definir o limite, com padrão de 30 segundos
e máximo de 10 minutos. Valor ausente, zero ou inválido usa o padrão. Escape,
clique fora do menu, destruição da janela e timeout liberam os grabs e fecham o
popup; o timeout é registrado no trace `gui`.

O teste `x11_popup_smoke` executa esses caminhos sob um Xvfb próprio: Escape,
clique externo, destruição externa e timeout. O cenário de destruição externa
também garante que o cleanup não tente destruir a mesma janela duas vezes.
Em builds com `TL_ENABLE_SANITIZERS`, o CTest configura esse smoke com
`ASAN_OPTIONS`/`LSAN_OPTIONS` em `detect_leaks=1`. A regressão também repete o
desenho de 512 cores, protegendo a liberação de cores, grabs, janelas e
displays. O teste deve rodar fora de ambientes que coloquem LeakSanitizer sob
`ptrace`; nesses ambientes ele pode ser explicitamente skipado por limitação da
ferramenta.

## Afinidade de thread da GUI

No escopo atual, o estado de USER32 e o display X11 pertencem ao thread
convidado principal, identificado por `GetCurrentThreadId() == 1`. As APIs
stateful de janela, fila, menu, foco, captura, timer, diálogo e pintura só
podem ser chamadas por esse thread. `PostMessageA/W` é a exceção deliberada:
uma thread secundária pode postar para um `HWND` registrado, e o thread
principal entrega a mensagem pela sua fila. A fila cross-thread é limitada a
4096 mensagens, valida o handle e mantém `lParam` como valor opaco; o runtime
não copia o payload apontado. Uma chamada de qualquer outra API stateful de
outro thread não acessa as tabelas globais nem o display: falha com
`ERROR_NOT_SUPPORTED` e registra o diagnóstico `thread-affinity`.

Esse contrato é uma restrição explícita do runtime, não uma equivalência
completa com o modelo Win32. A expansão futura para janelas associadas a
threads diferentes exigirá filas thread-safe, ciclo de vida de handles e
despacho cross-thread testados em conjunto.

## `MessageBoxA`

`USER32.dll!MessageBoxA` aceita `hWnd == NULL`, texto e título ANSI, e somente
`uType == 0`. Cria uma janela modal simples com texto e botão `OK`; clicar no
botão retorna `1`. Fechar a janela ou não conseguir abrir o display retorna
`0`. O runtime atualiza `GetLastError` em caso de falha.

## Subsistema de janela e eventos

Além de `MessageBoxA`, `USER32.dll` exporta um subconjunto mínimo de janela:

| API | Comportamento suportado |
|---|---|
| `RegisterClassExA` | Lê a `WNDCLASSEXA` do convidado (layout Microsoft x64, 80 bytes), valida `cbSize >= 80`, `lpfnWndProc` e `lpszClassName`, registra por nome (comparação sem diferenciar maiúsculas) e retorna um atom `>= 1`. |
| `CreateWindowExA` | Procura a classe, cria a janela X11 e despacha `WM_CREATE` ao `WNDPROC` do convidado antes de devolver o `HWND` token opaco; se o `WNDPROC` retornar `-1`, destrói a janela e devolve `NULL` (`lParam` do `WM_CREATE` é `0`; não há `CREATESTRUCT`). Aceita largura/altura `<= 0` (usa 480×180). Parent, menu, instância e parâmetro são ignorados; classes próprias usadas como filhos ficam em uma side-table lógica e entram no hit-test de mouse. |
| `ShowWindow` | Mapeia/desmapeia a janela X11; `cmdShow != 0` mostra, `0` esconde. |
| `UpdateWindow` | Despacha `WM_PAINT` diretamente ao `WNDPROC` do convidado. |
| `InvalidateRect` | Valida o `RECT` opcional e enfileira um `WM_PAINT` no `HWND` até a entrega; chamadas repetidas antes do consumo não duplicam a pintura. Para filhos lógicos, faz flush da superfície X11 projetada pelos offsets dos pais. |
| `GetMessageA` | Drena os eventos X11 da janela, traduz e preenche o `MSG` do convidado; retorna `0` quando `PostQuitMessage` foi chamado (preenche `WM_QUIT`). Uma mensagem traduzida em espera (`WM_CHAR` gerado por `TranslateMessage`) é entregue antes dos próximos eventos X11. `KeyPress` vira `WM_KEYDOWN` e `KeyRelease` vira `WM_KEYUP`, ambos com a virtual key; o caractere da tecla é guardado para o `TranslateMessage` subsequente. `ButtonPress`/`ButtonRelease`/movimento fazem hit-test dos filhos lógicos; quando o filho tem `WNDPROC`, a mensagem preserva seu `HWND` e usa coordenadas locais, e controles comuns sem `WNDPROC` continuam gerando notificações no parent. Timers expirados são entregues como `WM_TIMER` entre as consultas X11. Os filtros `wMsgFilterMin`/`wMsgFilterMax` e `hWnd` (quando `NULL` não filtra) são ignorados; `hWnd != NULL` filtra por janela. |
| `TranslateMessage` | Converte o `WM_KEYDOWN` mais recente de cada janela em `WM_CHAR` (com o caractere real) enfileirado para o próximo `GetMessageA`; retorna `1` quando traduziu e `0` caso contrário. |
| `SetTimer` | Cria/atualiza um timer periódico por janela (`WM_TIMER`), exigindo `lpTimerFunc == NULL` e `uElapse != 0`; devolve o id informado ou `0` em falha. |
| `KillTimer` | Remove um timer ativo; devolve `1` quando existia, `0` caso contrário. |
| `DispatchMessageA` | Lê o `MSG`, localiza o `HWND` e invoca o `WNDPROC` do convidado. |
| `DefWindowProcA` | `WM_CLOSE` → `DestroyWindow`; demais mensagens retornam `0`. |
| `DestroyWindow` | Destrói a janela X11 e despacha `WM_DESTROY` ao `WNDPROC` do convidado. |
| `PostQuitMessage` | Sinaliza `WM_QUIT` com o código informado; `GetMessageA` passa a retornar `0`. |
| `GetDC` / `ReleaseDC` | Devolvem um `HDC` que é o próprio handle de janela (token opaco) e validam o par `hwnd`/`dc`; servem de base para o desenho com GDI mínimo. Para controles lógicos, o destino é a superfície X11 da janela principal com o deslocamento acumulado dos pais. |

### Virtual keys e `TranslateMessage`

O mapeamento tecla→virtual key usa o `keysym` X11 entregue pelo pump. Teclas
especiais têm tabela fixa (`XK_Return`→`VK_RETURN`, `XK_BackSpace`→`VK_BACK`,
`XK_Tab`→`VK_TAB`, `XK_Escape`→`VK_ESCAPE`, setas `XK_Left/Up/Right/Down`→
`VK_LEFT/UP/RIGHT/DOWN`, `XK_Delete`→`VK_DELETE`); letras usam a maiúscula
(`VK_A`…`VK_Z`, já refletindo o estado de `Shift` via `XLookupString`) e demais
caracteres ASCII imprimíveis usam o próprio valor. Teclas sem caractere (setas,
`Return`, etc.) geram `WM_KEYDOWN`/`WM_KEYUP` normalmente, mas `TranslateMessage`
só emite `WM_CHAR` quando o `WM_KEYDOWN` tinha caractere real.

`MSG` é tratado com o layout Microsoft x64 (48 bytes). O `WNDPROC` do convidado
é invocado pela convenção Microsoft x64 (`TL_MSABI`) a partir do endereço lido
na classe; como o hospedeiro já executa sobre a pilha convidada quando o
convidado chama as APIs hospedeiras, essa fronteira host→convidado não precisa
de trampolim de pilha (`call_wndproc` em `src/runtime/dlls/user32/user32_internal.hpp`).

`Expose` não desenha conteúdo diretamente: o driver X11 apenas enfileira o
evento `Redraw`. O runtime repinta o client area completo antes de entregar o
`WM_PAINT` equivalente ao convidado. O título permanece somente na decoração
da janela X11. Além disso, o GDI mínimo desenha na janela X11:

### GDI mínimo (subconjunto)

`GDI32.dll` exporta `GetStockObject` e `TextOutA`/`TextOut`; `USER32.dll`
exporta `BeginPaint`/`EndPaint` (que são do USER32 no SDK Windows). O `HDC`
devolvido por `GetDC` e `BeginPaint` é o próprio handle de janela: como o
modelo suportado é uma janela por operação de desenho, o `HDC` identifica o
destino sem objeto DC real. `BeginPaint` preenche o `PAINTSTRUCT` convidado
(layout Microsoft x64, 72 bytes; `R`ECT de 16 bytes) com o tamanho armazenado
no `CreateWindowExA` e marca a janela como "pintando" até o `EndPaint`
correspondente. `TextOutA` desenha o texto (com o comprimento explícito) via
`XDrawString` no `HDC`/janela. Quando o `HDC` pertence a um controle lógico,
`TextOut`, `FillRect` e `Rectangle` somam a posição do controle e desenham na
superfície X11 da janela principal; o controle não precisa virar uma janela
X11 individual. `GetStockObject` devolve um token opaco por
objeto (endereço de uma tabela estática; stock objects não são liberados).

O subconjunto atual de `COMCTL32` segue o mesmo modelo: `CreateStatusWindowW`
cria uma status bar lógica no rodapé e `CreateToolbarEx`, ou
`CreateWindowEx` seguido de `TB_ADDBUTTONSW`, cria uma toolbar lógica com os
`idCommand` das entradas `TBBUTTON` validadas. `TB_AUTOSIZE` atualiza a
geometria mínima desse controle. A toolbar pode encaminhar um clique básico ao
parent como `WM_COMMAND`; o botão pressionado é redesenhado no `Press`, a
captura lógica mantém o controle até o `Release` e uma soltura fora do botão
pressionado cancela o comando. No shell visual específico do 7-Zip, o botão sob
o ponteiro recebe hover independente da pressão, e a linha da lista recebe o
mesmo feedback sem alterar a seleção. Bitmaps, image lists, temas e estilos
avançados ainda não fazem parte do contrato.

Pelo mesmo motivo, `SendMessageA/W` trata apenas o ciclo necessário para esse
modelo: dimensionamento, `TB_ADDBUTTONSA/W`, contagem/exclusão e atualização de
texto da status bar. O vetor recebido pelo convidado é validado antes de ser
lido; nenhum ponteiro de bitmap ou image list é executado pelo backend.

## Validação

As fixtures `tl_gui.exe`, `tl_win.exe`, `tl_win2.exe`, `tl_key.exe`,
`tl_timer.exe` e `tl_gdi.exe` são validadas automaticamente pelo parser,
metadata e `--report`, que confirmam os imports sem executar o entry point.
Além disso, `tl_win.exe`, `tl_win2.exe`, `tl_key.exe`, `tl_timer.exe` e
`tl_gdi.exe` são executados de ponta a ponta no teste `runtime_gui_smoke`
(`tests/gui/runtime_gui_smoke.cpp`), que sobe sempre um `Xvfb` próprio — sem
window manager, para que a janela seja filha direta da root e os eventos
sintéticos cheguem ao cliente — e cobre sete cenários:

1. **autoclose** — `TL_GUI_AUTOCLOSE_MS != 0`: o message loop encerra sozinho
   via `WM_QUIT`, sem interação.
2. **fechar** — `TL_GUI_AUTOCLOSE_MS == 0`: o driver localiza a janela pelo
   título e envia `WM_DELETE_WINDOW` (o mesmo `ClientMessage` que o botão de
   fechar de um window manager envia), exercitando
   `WM_CLOSE → DefWindowProcA → DestroyWindow → WM_DESTROY →
   PostQuitMessage(0) → GetMessageA/WM_QUIT → ExitProcess(0)`.
3. **teclado** — `TL_GUI_AUTOCLOSE_MS == 0`: o driver envia um `KeyPress`
   sintético `'q'` (via `XSendEvent`), exercitando
   `WM_KEYDOWN → TranslateMessage → WM_CHAR('q') → DestroyWindow → WM_DESTROY →
   PostQuitMessage(3) → GetMessageA/WM_QUIT → ExitProcess(3)`.
4. **janelas** — com `tl_win2.exe`: duas janelas simultâneas, cada uma com
   classe e `WNDPROC` próprios. O driver envia `'q'` à janela "Janela A" e `'k'`
   à "Janela B" (cada `KeyPress` é roteado para a fila da própria janela pelo
   pump), exercitando as duas cadeias independentes de
   `WM_KEYDOWN → WM_CHAR → DestroyWindow → WM_DESTROY` e provando a
   demultiplexação de eventos entre janelas.
5. **teclado estendido** — com `tl_key.exe`: o driver envia `KeyPress`+`KeyRelease`
   sintéticos de `Shift+q`, `Return` e `Left` (via `XSendEvent`), exercitando
   `WM_KEYDOWN('Q')` com Shift, `WM_CHAR(81)`, `WM_KEYDOWN(VK_RETURN)`,
   `WM_KEYDOWN(VK_LEFT)` e `WM_KEYUP(VK_LEFT)`; a fixture destrói a janela só
   depois de receber os três marcadores (exit-code `7`), provando que o Shift
   produziu maiúscula e que teclas sem caractere geram `WM_KEYUP`.
6. **timer** — com `tl_timer.exe` (sem interação): `SetTimer` cria um timer
   periódico de 200 ms; dois disparos `WM_TIMER` chegam ao `WNDPROC`, o segundo
   faz `KillTimer` + `DestroyWindow` (exit-code `7`), exercitando a entrega de
   `WM_TIMER` pelo pump com periodicidade.
7. **GDI mínimo** — com `tl_gdi.exe` (sem interação): no `WM_PAINT` a fixture
   chama `BeginPaint`, verifica `ps.hdc == hdc` e o `rcPaint` com o tamanho da
   janela, desenha "Ola GDI no Linux!" com `TextOutA`, chama `EndPaint` e
   destrói a janela (exit-code `3`), exercitando o `PAINTSTRUCT`, o `HDC == hwnd`
   e o desenho com comprimento explícito.

Cada cenário exige o exit-code esperado — `tl_win.c` marca uma flag no
`WM_CREATE` e outra ao receber `WM_CHAR('q')`, e faz `PostQuitMessage(flag)` no
`WM_DESTROY` (autoclose e fechar exigem `1`; teclado exige `3`). `tl_win2.c`
acumula flags de cada janela (criada A=1, `'q'` A=2, criada B=4, `'k'` B=8) e
faz `PostQuitMessage` quando a última janela é destruída (exit-code `15` com
tudo funcionando). `tl_key.c` acumula `WM_CHAR('Q')`=1, `WM_KEYDOWN(VK_RETURN)`=2
e `WM_KEYUP(VK_LEFT)`=4 (exit-code `7`). `tl_timer.c` acumula `WM_CREATE`=1 e o
primeiro/segundo `WM_TIMER`=2/4 (exit-code `7`). `tl_gdi.c` acumula `WM_CREATE`=1
e pintura válida com `BeginPaint`/`EndPaint`/`TextOutA`=2 (exit-code `3`). O
teste também exige `stdout` vazio (trace só em `stderr`), os eventos
`RegisterClassExA`, `CreateWindowExA`, `GetMessageA message="WM_QUIT"`,
`ExitProcess` e `exit explicit="sim"` no trace, além dos
`TranslateMessage message="WM_CHAR"` por janela, `SetTimer`/`KillTimer`/
`GetMessageA message="WM_TIMER"` no cenário timer e `GetStockObject`/
`BeginPaint`/`TextOut`/`EndPaint` no cenário GDI. O teste é configurado pelo
CMake somente quando `xvfb` está disponível (CI instala `xvfb`). Para o smoke
test manual interativo de `tl_win.exe`:

```bash
TL_GUI_AUTOCLOSE_MS=0 ./build/debug/src/tradutorlinux \
  build/debug/tests/samples/generated/tl_win.exe
```

A janela "Ola do Windows no Linux!" abre; fechá-la pelo botão do gerenciador de
janelas percorre o mesmo caminho de encerramento e termina com código `0`.

A entrada de teclado no runtime é testada de ponta a ponta pelo cenário
**teclado** (KeyPress sintético), mas num display com window manager o evento
real do teclado também chega como `KeyPress` ao cliente, então digitar na janela
funciona da mesma forma; não há teste automatizado com um WM real.

recursos, ícones, menus, toolkits e a maioria do GDI (regiões,
pincéis, fontes, `BeginPaint` com atualização de região inválida, HDC de
verdade) não fazem parte deste protótipo.

## Controles lógicos e bandeja emulada (Fase 12)

O Simple Todo C usa controles Win32 que não precisam virar janelas X11
individuais. `CreateWindowExA` cria tokens filhos em uma side-table ligada à
janela principal; `MoveWindow`, `ShowWindow`, `EnableWindow`, foco e
`Get/SetWindowTextA` atualizam esse estado. O estado e o renderer dos controles
ficam isolados em `src/runtime/gui_controls.cpp`; os módulos em
`src/runtime/dlls/user32/` mantêm as pontes das APIs Win32 e o despacho de
eventos. O renderer hospedeiro desenha o
subconjunto exercitado pelo alvo: `EDIT`, `BUTTON`, `COMBOBOX`, `STATIC` e
`SysListView32`.

Classes customizadas registradas pelo convidado também entram na side-table e
recebem o ciclo básico de `WM_CREATE`/`WM_PAINT`, mas não são tratadas como se
fossem visualmente suportadas: quando não há renderer para a classe, a área
mostra um diagnóstico explícito com o nome do controle. Isso evita uma tela
branca silenciosa em aplicações genéricas. Há uma exceção deliberada para o
alvo real `7zFM_x64.exe`: quando a classe principal é `7-Zip::FM`, o backend
desenha um shell visual próprio com menu, toolbar, endereço, navegação lateral,
lista e status, sem fingir que os comandos do convidado já funcionam. A lista
mostra somente as entradas imediatas do diretório que contém o executável
aberto, em ordem determinística, sem seguir links simbólicos e limitada a 128
linhas; clicar seleciona uma entrada e `Enter` ou duplo clique abre uma pasta no
diretório Linux correspondente, com `..` retornando ao pai; o caminho visual continua sendo
`Z:\`. O comando `Copy` (`idCommand=546`) tem uma operação verificável no
backend: com `TL_7ZFM_COPY_DESTINATION` configurado para um diretório existente
dentro da raiz visual, copia o arquivo selecionado sem sobrescrever destino e
mostra o resultado no status; sem essa configuração, ou fora da raiz, falha de
forma controlada. As demais operações ainda não são emuladas. A
árvore lateral permite retornar à raiz visual por `Computer`/`Local Disk (Z:)`
e selecionar `Home`, `Desktop` e `Documents` do usuário quando os diretórios
existem; os nomes `Desktop`/`Documents` também reconhecem as variantes
localizadas `Área de trabalho`/`Documentos`. A
barra `Address` pode ser focada com o mouse, editada com caracteres ASCII,
confirmada com `Enter` ou cancelada com `Escape`; o parser aceita somente a
forma `Z:\...`, normaliza separadores, exige um diretório existente e rejeita
qualquer caminho que escape da raiz visual. A
normalização da geometria inválida desse alvo também
fica registrada no trace. A faixa visual usa a ordem de `idCommand` enviada pelo
7-Zip, fornece hover independente da seleção e o hit-test dela pode enfileirar
`WM_COMMAND` no parent; a árvore lateral também fornece hover sem alterar a
pasta selecionada; os rótulos e
ícones continuam sendo uma apresentação específica do shell. A classe principal
também carrega o recurso `RT_MENU` MENUEX do próprio executável e usa seus seis
itens de nível superior para os rótulos; `GetMenuItemInfoW` consulta a hierarquia
carregada. O clique nos rótulos abre o submenu na própria superfície lógica,
itens folha são destacados e sua seleção enfileira `WM_COMMAND` no parent; itens
com submenu abrem o próximo nível ao serem pressionados. A captura impede que a
toolbar sob o popup receba o mesmo clique. As setas `Up`/`Down`, `Right`, `Left`,
`Enter` e `Escape` percorrem a trilha aberta, com limite de profundidade imposto
pelo parser MENUEX. Mutações de menu ainda exigem contratos próprios, e esse
despacho não implica que as operações de arquivo já estejam implementadas.

`SendMessageA` implementa os contratos usados pelo alvo para `WM_SETFONT`,
`CB_ADDSTRING`, `CB_SETCURSEL`, `CB_GETCURSEL` e as mensagens de list view de
colunas, itens, seleção, texto, limpeza e ordenação. O mouse usa hit-testing
dos controles; foco de edição produz `EN_SETFOCUS`, `EN_KILLFOCUS` e
`EN_CHANGE`; botões produzem `BN_CLICKED`; a lista produz `LVN_ITEMCHANGED`.
Essas notificações são enfileiradas no `HWND` pai e atravessam o mesmo
`GetMessageA`/`DispatchMessageA` do aplicativo.

`Shell_NotifyIconA` registra o contrato lógico da bandeja. Um botão secundário
na janela X11 gera `WM_USER + 1` com `WM_RBUTTONUP`; `CreatePopupMenu`,
`AppendMenuA` e `TrackPopupMenu` abrem uma janela X11 popup. A janela principal
fica mapeada enquanto sua visibilidade Win32 é falsa para que o surrogate da
bandeja permaneça acionável. Isso é deliberadamente uma emulação de teste, não
uma integração com o tray do desktop.

`LoadMenuW` reconhece o recurso `RT_MENU` MENUEX v1 do módulo convidado, cria a
hierarquia lógica e `GetMenuItemInfoW` devolve os campos solicitados com
validação do buffer. A implementação atual cobre o menu de classe usado pelo
7-Zip e retorna falha controlada para templates padrão v0 ou recursos ausentes.
As operações de mutação de itens (`SetMenuItemInfoW`, `InsertMenuItemW`, `RemoveMenu`,
`EnableMenuItem`, `CheckMenuItem` e `CheckMenuRadioItem`) e
`TrackPopupMenuEx` também falham explicitamente como stubs; o relatório os
classifica como `stub`.

O alvo Simple Todo recebe um overlay Linux versionado em
`tests/targets/patches/`: a opção de inicialização com Windows é removida e o
fechamento da janela destrói o alvo em vez de apenas ocultá-lo; o fluxo continua
usando somente dados relativos em `APPDATA`. O smoke
`targetapp_simple_todo_gui_smoke` cria um CWD próprio, prepara `appdata` para
os caminhos relativos e verifica o artefato persistente de todos.

## Diálogos modais e controles reutilizáveis (Fase 13.11)

`USER32!DialogBoxParamW` aceita um template numérico `RT_DIALOG` do módulo
atual, no formato padrão documentado em [abi-x64.md](abi-x64.md). O runtime
cria a janela modal X11 e reutiliza o renderer de controles lógicos para os
itens `STATIC`, `EDIT`, `BUTTON` e `COMBOBOX`. O `DLGPROC` recebe
`WM_INITDIALOG`; o loop interno usa `GetMessageW` e `IsDialogMessageW` até que
`EndDialog` defina o resultado. Há no máximo um diálogo ativo: chamadas
aninhadas retornam `ERROR_NOT_SUPPORTED`. Fechar a decoração X11 equivale a
`IDCANCEL`, e `EndDialog` destrói controles/janela, restaura o pai e nunca gera
`WM_QUIT`.

O subconjunto inclui `GetDlgItem`, `SetDlgItemTextW`, `SendDlgItemMessageW`,
`GetNextDlgTabItem` e `IsDialogMessageW`. A tabulação percorre, na ordem do
template, controles visíveis/habilitados com `WS_TABSTOP`; Enter e Escape
enfileiram `WM_COMMAND` para `IDOK`/`IDCANCEL`. `GetWindowRect` usa a geometria
lógica armazenada, e `GetWindowLongW`/`SetWindowLongW` são wrappers de 32 bits
dos `*Ptr` limitados a índices documentados. `CopyImage` e `DestroyIcon`
gerenciam somente tokens de ícone copiados; `LoadImageW`, `DrawIconEx`, captura,
clipboard, redraw amplo e controles COMCTL32 desconhecidos continuam fora do
escopo.

`tl_dialog.exe` é a fixture reproduzível: consulta filhos/texto/geometria,
envia mensagem ao `EDIT`, percorre tabulação, copia um ícone e retorna 42 pelo
modal. O cenário `dialog` de `runtime_gui_smoke` envia Tab e Enter e verifica
stdout `dialog\n`, o retorno, a destruição e os eventos do trace. Em ambientes
sem socket X11 o CTest mantém o cenário como `Skipped`, como os demais smokes.
