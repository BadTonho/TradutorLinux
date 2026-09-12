# Teste visual das aplicações Win32

O `runtime_gui_smoke` valida automaticamente a GUI em um `Xvfb`, mas esse
teste não substitui uma sessão visual. Para abrir as janelas na sua área de
trabalho Linux, use uma sessão X11 real ou uma sessão Wayland com XWayland
ativo (`DISPLAY` definido).

## Preparar as fixtures

```bash
cmake --preset debug
cmake --build --preset debug
```

As aplicações ficam em `build/debug/tests/samples/generated/`.

## Abrir a GUI principal

```bash
./build/debug/src/tradutorlinux_gui
```

A tela principal é o launcher Qt6. Ela aceita o caminho do executável pelo
teclado ou pela biblioteca `library.json`; também permite filtrar e selecionar
aplicativos cadastrados. Digite o caminho completo ou relativo, clique em
**Analisar** para executar `--report`, ou em
**Executar** para iniciar o programa convidado. O painel inferior mostra o
trace, imports, erros e código de saída. Aplicativos selecionados usam os
limites de CPU/memória salvos no catálogo. **Limpar** apaga o formulário e
**Sair** fecha a aplicação.

O contrato da interface, o fluxo assíncrono e os limites do launcher estão em
[`docs/arquitetura/guia-ui-qt6.md`](arquitetura/guia-ui-qt6.md).

## Abrir a caixa de diálogo

```bash
./build/debug/src/tradutorlinux --trace \
  build/debug/tests/samples/generated/tl_gui.exe
```

`tl_gui.exe` abre uma caixa X11 com o botão `OK`. Fechar a janela ou clicar no
botão encerra a fixture.

## Abrir o 7-Zip real no modo genérico

```bash
DISPLAY=:0.0 XDG_SESSION_TYPE=x11 \
./build/debug/src/tradutorlinux \
  "/caminho/para/Aplicativos_Windows_Populares/7-Zip/7zFM.exe"
```

Essa execução direta não seleciona nenhuma extensão de aplicativo. Para usar o
shell visual host-side do 7-Zip, o aplicativo precisa ser cadastrado e executado
com `app run`, além de possuir um `compat/profile.json` schema 4 com
`"extension": "7zip"`. O smoke reproduzível abaixo prepara esses arquivos
temporários automaticamente.

No fluxo com a extensão selecionada, a lista mostra a pasta do executável.
Clique ou use as setas para selecionar; `Enter` ou duplo clique abre diretórios.
A árvore lateral volta à raiz ou seleciona pastas conhecidas do usuário. Também
é possível clicar em `Address`, editar um caminho `Z:\...` e confirmar com
`Enter`; somente diretórios existentes dentro da raiz visual são aceitos. Passe
o mouse sobre um botão da toolbar, uma linha ou um item da árvore lateral para
ver o hover; mantenha o clique pressionado para ver o botão ativo e solte fora
dele para cancelar. Para repetir o fluxo restrito de cópia em Xvfb, use a
amostra exata registrada no inventário:

```bash
./build/debug/tests/seven_zip_smoke \
  ./build/debug/src/tradutorlinux \
  "/caminho/para/Aplicativos_Windows_Populares/7-Zip/7zFM.exe"
```

O smoke usa uma cópia temporária do executável e de `7z.dll`, cadastra o
aplicativo com ID `7zip`, cria o perfil explícito, cria `input.txt`, seleciona o
arquivo e aciona `Copy` (`idCommand=546`). O destino é removido ao final; a
operação não sobrescreve arquivos.

## Abrir uma janela com message loop

```bash
TL_GUI_AUTOCLOSE_MS=0 \
./build/debug/src/tradutorlinux --trace \
  build/debug/tests/samples/generated/tl_win.exe
```

A janela permanece aberta até ser fechada pelo gerenciador de janelas ou até
pressionar `q`, que gera `WM_KEYDOWN` e `WM_CHAR`. O valor `TL_GUI_AUTOCLOSE_MS=1`
ativa o fechamento automático usado nos testes.

## Outras demonstrações

```bash
./build/debug/src/tradutorlinux --trace build/debug/tests/samples/generated/tl_win2.exe
./build/debug/src/tradutorlinux --trace build/debug/tests/samples/generated/tl_key.exe
./build/debug/src/tradutorlinux --trace build/debug/tests/samples/generated/tl_timer.exe
./build/debug/src/tradutorlinux --trace build/debug/tests/samples/generated/tl_gdi.exe
./build/debug/src/tradutorlinux --trace build/debug/tests/samples/generated/tl_paint.exe
```

Essas fixtures demonstram múltiplas janelas, teclado, timers, pintura/texto e
entrada de mouse. Os comportamentos suportados estão em
[`docs/compatibilidade.md`](compatibilidade.md) e
[`docs/arquitetura/gui-x11.md`](arquitetura/gui-x11.md).

## Diagnóstico de ambiente

Verifique se o processo possui um display acessível:

```bash
echo "DISPLAY=$DISPLAY"
xdpyinfo >/dev/null
```

Se `DISPLAY` estiver vazio, inicie uma sessão gráfica ou configure XWayland.
`Xvfb` é adequado para CI e testes automáticos, mas não exibe uma janela
visível.

O runtime não é um sandbox: o executável convidado roda com os privilégios do
usuário atual. A GUI continua experimental e limitada à matriz publicada.
