# GUI mínima com X11 (Fase 7)

Este é um protótipo isolado para avaliar GUI, não uma promessa de compatibilidade
Win32 gráfica geral. A implementação usa `libX11` diretamente e mantém o
runtime de console independente.

## `MessageBoxA`

`USER32.dll!MessageBoxA` aceita `hWnd == NULL`, texto e título ANSI, e somente
`uType == 0`. Cria uma janela modal simples com texto e botão `OK`; clicar no
botão retorna `1`. Fechar a janela ou não conseguir abrir o display retorna
`0`. O runtime atualiza `GetLastError` em caso de falha.

## Validação

A fixture `tl_gui.exe` é validada automaticamente pelo parser, metadata e
`--report`, que confirma os imports sem executar o entry point. Para o smoke
test manual:

```bash
TL_GUI_AUTOCLOSE_MS=0 ./build/debug/src/tradutorlinux \
  tests/samples/generated/tl_gui.exe
```

Com `TL_GUI_AUTOCLOSE_MS` diferente de `0`, a janela fecha automaticamente
após aproximadamente 100 ms, permitindo um teste de integração em uma sessão
X11 sem interação humana. O ambiente precisa fornecer `DISPLAY` acessível.

Wayland nativo, GDI, recursos, eventos Win32 completos, múltiplas janelas e
toolkits não fazem parte deste protótipo.
