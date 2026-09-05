# Launcher desktop com Qt6

Este documento descreve o launcher desktop do TradutorLinux. A interface é
uma aplicação host independente do runtime Win32 convidado: ela usa Qt6 para
apresentação e `QProcess` para iniciar o executável `tradutorlinux`.

O launcher Qt6 substitui o protótipo Xlib anterior e mantém o executável
`tradutorlinux_gui`. A GUI não amplia o conjunto de APIs Win32 suportadas; a
compatibilidade continua definida por [`compatibilidade.md`](../compatibilidade.md)
e pelos contratos do runtime.

## Arquitetura

```text
┌──────────────────────────────────────────────────────────────┐
│ tradutorlinux_gui                                            │
│  MainWindow                                                   │
│  ├─ busca e seleção da biblioteca                            │
│  ├─ seletor de executável e ações                            │
│  ├─ status e console de diagnóstico                          │
│  └─ QProcess assíncrono                                       │
└──────────────────────────────┬───────────────────────────────┘
                               │ subprocesso
                               ▼
┌──────────────────────────────────────────────────────────────┐
│ tradutorlinux                                                 │
│  CLI → AppCatalog/Prefix → PE parser/loader → convidado       │
└──────────────────────────────────────────────────────────────┘
```

O core (`tradutorlinux_core`) não depende de Qt. A janela é implementada em
[`src/gui/main_window.cpp`](../../src/gui/main_window.cpp), declarada em
[`src/gui/main_window.hpp`](../../src/gui/main_window.hpp) e iniciada por
[`src/gui/qt_main.cpp`](../../src/gui/qt_main.cpp).

## Dependências e build

Qt6 Widgets é uma dependência obrigatória do build. Em Debian,
Ubuntu e Pop!_OS:

```bash
sudo apt update
sudo apt install qt6-base-dev
```

O CMake habilita `AUTOMOC`, `AUTOUIC` e `AUTORCC`, localiza Qt6 e vincula o
launcher a `Qt6::Widgets`. O smoke test usa a própria aplicação Qt6 em modo
`offscreen`, sem depender de um toolkit de testes adicional.

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug --output-on-failure
```

O alvo continua sendo gerado no caminho conhecido:

```text
build/debug/src/tradutorlinux_gui
```

O launcher procura o runtime `tradutorlinux` no mesmo diretório do próprio
executável. Essa regra vale para builds normais e evita dependência do diretório
de trabalho ou de `PATH`.

## Interface e comportamento

`MainWindow` expõe os seguintes elementos:

- campo de busca e lista filtrável dos aplicativos registrados em
  `library.json`;
- campo de caminho e `QFileDialog` com filtro para `.exe`;
- campo opcional de nome da instalação e ações **Analisar**, **Executar**,
  **Instalar**, **Cadastrar**, **Limpar** e **Sair**;
- indicador de quantidade de aplicativos cadastrados;
- status textual e console somente leitura para logs.

Ao selecionar um item da biblioteca, o caminho do executável é preenchido e as
ações usam o identificador cadastrado. Assim, a execução chama:

```text
tradutorlinux app run <id> --trace
tradutorlinux app run <id> --trace --report
```

Quando o item possui `cpu_limit_seconds` ou `memory_limit_mib` em
`library.json`, `app run` aplica esses limites automaticamente; a janela mostra
o trace resultante, mas não oferece edição desses campos. Para alterar os
limites, use `app add`/`install` ou `app run --cpu ... --memory ...` pelo CLI.

Um caminho digitado ou escolhido fora da biblioteca usa a forma direta:

```text
tradutorlinux --trace <arquivo.exe>
tradutorlinux --trace --report <arquivo.exe>
```

O catálogo continua sendo lido e salvo por `catalog::AppCatalog`. Um cadastro
manual novo recebe `prefix::default_app_prefix(id)` e inicia em seu `drive_c`;
uma entrada selecionada preserva ID, prefixo, argumentos e demais metadados.

**Instalar** inicia `tradutorlinux install <setup> --trace`. O launcher lê
somente os eventos estruturados do componente `install` em `stderr`: no evento
`registered`, recarrega a biblioteca; se o comando retornar `6` com mais de um
evento `candidate`, mostra uma escolha e chama `app add --id --prefix` para o
arquivo escolhido. Cancelar preserva o prefixo sem criar entrada no catálogo.

## Execução assíncrona

O launcher mantém um único `QProcess` ativo. Enquanto ele está iniciando ou
executando:

- Analisar, Executar, Instalar, Cadastrar, Limpar, busca e seleção ficam desabilitados;
- stdout e stderr são lidos por sinais independentes;
- o status informa que a operação está em andamento;
- fechar a janela tenta terminar o subprocesso e, após um limite curto,
  força seu encerramento.

As mensagens são exibidas no console com os canais `[stdout]`, `[stderr]` e
`[launcher]`. O launcher não mistura esses dados com a saída do próprio
processo Linux.

Ao terminar, o status distingue conclusão normal, código de saída diferente
de zero e término por sinal/crash. Falhas `QProcess::FailedToStart` exibem o
erro de inicialização sem atribuir um código de saída falso ao runtime.

## Testes

O teste [`tests/gui/qt_launcher_test.cpp`](../../tests/gui/qt_launcher_test.cpp)
usa `QT_QPA_PLATFORM=offscreen` e cobre:

1. carregamento, filtragem e seleção de itens do catálogo;
2. cadastro e persistência em um `library.json` temporário;
3. limpeza do formulário sem perder a biblioteca;
4. relatório assíncrono de `tl_hello.exe`, incluindo stdout e stderr;
5. execução real de `tl_hello.exe`, incluindo a saída do convidado e o código
   de saída;
6. instalação, cadastro automático e execução posterior de uma fixture em
   prefixo temporário;
7. seleção assíncrona do primeiro de dois candidatos, seguida de `app add`
   sem repetir o setup;
8. falha controlada quando o caminho do runtime não pode ser iniciado.

O teste usa o runtime e as fixtures produzidos pelo mesmo build. O diretório
de configuração é temporário, portanto os testes não alteram a biblioteca do
usuário.

## Limites desta entrega

- Não há wizard de instalação, gerenciamento de ícones ou integração nativa
  com bandeja nesta versão.
- Qt6 é usado para a interface host. A GUI Win32 do executável convidado
  continua usando o subsistema X11 documentado em
  [`gui-x11.md`](gui-x11.md).
- Executar um `.exe` continua tendo os privilégios do usuário atual; o
  launcher não é sandbox.
