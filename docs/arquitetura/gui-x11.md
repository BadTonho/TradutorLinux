# GUI mínima com X11 (Fase 7)

Este é um protótipo isolado para avaliar GUI, não uma promessa de compatibilidade
Win32 gráfica geral. A implementação usa `libX11` diretamente e mantém o
runtime de console independente.

## Camada X11

`src/gui/x11.cpp` abre um único display (lazy) compartilhado por todas as
janelas do processo. Cada janela é um token opaco num pool fixo (`NativeWindow`).
Os eventos X11 são drenados pelo runtime via `next_window_event` e traduzidos
para o subconjunto Win32: `Expose` → `WM_PAINT`, `ButtonPress` → `WM_LBUTTONDOWN`,
`KeyPress` → `WM_KEYDOWN`/`WM_CHAR` (via `TranslateMessage`) e `WM_DELETE_WINDOW`
(protocolo de janela) → `WM_CLOSE`.

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
| `CreateWindowExA` | Procura a classe, cria a janela X11 e despacha `WM_CREATE` ao `WNDPROC` do convidado antes de devolver o `HWND` token opaco; se o `WNDPROC` retornar `-1`, destrói a janela e devolve `NULL` (`lParam` do `WM_CREATE` é `0`; não há `CREATESTRUCT`). Aceita largura/altura `<= 0` (usa 480×180). Parent, menu, instância e parâmetro são ignorados. |
| `ShowWindow` | Mapeia/desmapeia a janela X11; `cmdShow != 0` mostra, `0` esconde. |
| `UpdateWindow` | Despacha `WM_PAINT` diretamente ao `WNDPROC` do convidado. |
| `GetMessageA` | Drena os eventos X11 da janela, traduz e preenche o `MSG` do convidado; retorna `0` quando `PostQuitMessage` foi chamado (preenche `WM_QUIT`). Uma mensagem traduzida em espera (`WM_CHAR` gerado por `TranslateMessage`) é entregue antes dos próximos eventos X11. `KeyPress` vira `WM_KEYDOWN` com a virtual key (letras viram maiúsculas) e guarda o caractere para o `TranslateMessage` subsequente. Os filtros `wMsgFilterMin`/`wMsgFilterMax` e `hWnd` (quando `NULL` não filtra) são ignorados; `hWnd != NULL` filtra por janela. |
| `TranslateMessage` | Converte o `WM_KEYDOWN` mais recente de cada janela em `WM_CHAR` (com o caractere real) enfileirado para o próximo `GetMessageA`; retorna `1` quando traduziu e `0` caso contrário. |
| `DispatchMessageA` | Lê o `MSG`, localiza o `HWND` e invoca o `WNDPROC` do convidado. |
| `DefWindowProcA` | `WM_CLOSE` → `DestroyWindow`; demais mensagens retornam `0`. |
| `DestroyWindow` | Destrói a janela X11 e despacha `WM_DESTROY` ao `WNDPROC` do convidado. |
| `PostQuitMessage` | Sinaliza `WM_QUIT` com o código informado; `GetMessageA` passa a retornar `0`. |

`MSG` é tratado com o layout Microsoft x64 (48 bytes). O `WNDPROC` do convidado
é invocado pela convenção Microsoft x64 (`TL_MSABI`) a partir do endereço lido
na classe; como o hospedeiro já executa sobre a pilha convidada quando o
convidado chama as APIs hospedeiras, essa fronteira host→convidado não precisa
de trampolim de pilha (`call_wndproc` em `src/runtime/winapi.cpp`).

O texto desenhado no `Expose` é o título da janela, gravado pelo próprio
runtime; desenho arbitrário via GDI (como `TextOut`/`BeginPaint`) fica fora de
escopo.

## Validação

As fixtures `tl_gui.exe`, `tl_win.exe` e `tl_win2.exe` são validadas
automaticamente pelo parser, metadata e `--report`, que confirmam os imports
sem executar o entry point. Além disso, `tl_win.exe` e `tl_win2.exe` são
executados de ponta a ponta no teste `runtime_gui_smoke`
(`tests/gui/runtime_gui_smoke.cpp`), que sobe sempre um `Xvfb` próprio — sem
window manager, para que a janela seja filha direta da root e os eventos
sintéticos cheguem ao cliente — e cobre quatro cenários:

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

Cada cenário exige o exit-code esperado — `tl_win.c` marca uma flag no
`WM_CREATE` e outra ao receber `WM_CHAR('q')`, e faz `PostQuitMessage(flag)` no
`WM_DESTROY` (autoclose e fechar exigem `1`; teclado exige `3`). `tl_win2.c`
acumula flags de cada janela (criada A=1, `'q'` A=2, criada B=4, `'k'` B=8) e
faz `PostQuitMessage` quando a última janela é destruída (exit-code `15` com
tudo funcionando). O teste também exige
`stdout` vazio (trace só em `stderr`), os eventos `RegisterClassExA`,
`CreateWindowExA`, `GetMessageA message="WM_QUIT"`, `ExitProcess` e
`exit explicit="sim"` no trace, além dos `TranslateMessage message="WM_CHAR"`
por janela. O teste é configurado pelo CMake somente quando `xvfb` está
disponível (CI instala `xvfb`). Para o smoke test manual interativo de
`tl_win.exe`:

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

Wayland nativo, GDI, recursos, ícones, menus, múltiplas janelas simultâneas e
toolkits não fazem parte deste protótipo.