# GUI mínima com X11 (Fase 7)

Este é um protótipo isolado para avaliar GUI, não uma promessa de compatibilidade
Win32 gráfica geral. A implementação usa `libX11` diretamente e mantém o
runtime de console independente.

## Camada X11

`src/gui/x11.cpp` abre um único display (lazy) compartilhado por todas as
janelas do processo. Cada janela é um token opaco num pool fixo (`NativeWindow`).
Os eventos X11 são drenados pelo runtime via `next_window_event` e traduzidos
para o subconjunto Win32: `Expose` → `WM_PAINT`, `ButtonPress` → `WM_LBUTTONDOWN`
e `WM_DELETE_WINDOW` (protocolo de janela) → `WM_CLOSE`. Eventos não relevantes
para a janela consultada são descartados.

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
| `GetMessageA` | Drena os eventos X11 da janela, traduz e preenche o `MSG` do convidado; retorna `0` quando `PostQuitMessage` foi chamado (preenche `WM_QUIT`). Os filtros `wMsgFilterMin`/`wMsgFilterMax` e `hWnd` (quando `NULL` não filtra) são ignorados; `hWnd != NULL` filtra por janela. |
| `TranslateMessage` | No-op; valida apenas o ponteiro do `MSG`. |
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

As fixtures `tl_gui.exe` e `tl_win.exe` são validadas automaticamente pelo
parser, metadata e `--report`, que confirmam os imports sem executar o entry
point. Além disso, `tl_win.exe` é executado de ponta a ponta no teste
`runtime_gui_smoke` (`tests/gui/runtime_gui_smoke.cpp`), que sobe um `Xvfb`
próprio e cobre dois cenários:

1. **autoclose** — `TL_GUI_AUTOCLOSE_MS != 0`: o message loop encerra sozinho
   via `WM_QUIT`, sem interação.
2. **fechar** — `TL_GUI_AUTOCLOSE_MS == 0`: o driver localiza a janela pelo
   título e envia `WM_DELETE_WINDOW` (o mesmo `ClientMessage` que o botão de
   fechar de um window manager envia), exercitando
   `WM_CLOSE → DefWindowProcA → DestroyWindow → WM_DESTROY →
   PostQuitMessage(0) → GetMessageA/WM_QUIT → ExitProcess(0)`.

Ambos exigem exit-code `1` — a fixture `tl_win.c` marca uma flag no `WM_CREATE`
e faz `PostQuitMessage(flag)` no `WM_DESTROY`, então o exit code prova que o
`WM_CREATE` foi despachado — `stdout` vazio (trace só em `stderr`) e os eventos
`RegisterClassExA`, `CreateWindowExA`, `GetMessageA message="WM_QUIT"`,
`ExitProcess` e `exit explicit="sim"` no trace. O teste é configurado pelo CMake
somente quando `xvfb` está disponível (CI instala `xvfb`). Para o smoke test
manual interativo de `tl_win.exe`:

```bash
TL_GUI_AUTOCLOSE_MS=0 ./build/debug/src/tradutorlinux \
  build/debug/tests/samples/generated/tl_win.exe
```

A janela "Ola do Windows no Linux!" abre; fechá-la pelo botão do gerenciador de
janelas percorre o mesmo caminho de encerramento e termina com código `0`.

Wayland nativo, GDI, recursos, ícones, menus, múltiplas janelas simultâneas e
toolkits não fazem parte deste protótipo.