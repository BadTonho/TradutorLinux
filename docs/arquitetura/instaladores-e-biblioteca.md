# Arquitetura: Instaladores e Biblioteca de Aplicativos

Este documento detalha os contratos técnicos do subsistema de **Prefixos de Ambiente** e da **Biblioteca de Aplicativos (Catalog/Launcher)** do TradutorLinux.

---

## 1. Subsistema de Prefixos (`prefix`)

Localização:
- Header: [include/tradutorlinux/prefix/prefix.hpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/include/tradutorlinux/prefix/prefix.hpp)
- Implementação: [src/prefix/prefix.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/prefix/prefix.cpp)

### Estrutura de Diretórios
Execuções diretas legadas usam `~/.tradutorlinux/`. Cada nova instalação e cada
novo cadastro persistente usam, por padrão, um prefixo exclusivo em
`~/.tradutorlinux/prefixes/<id>/`:
```text
~/.tradutorlinux/prefixes/<id>/
└── drive_c/
    ├── Program Files/
    ├── Program Files (x86)/
    ├── users/
    │   └── guest/
    │       └── AppData/
    │           ├── Local/
    │           └── Roaming/
    └── windows/
        ├── system32/
        └── temp/
```

Entradas antigas cujo `prefix_path` ainda é o prefixo compartilhado preservam
o `working_directory` externo já salvo. Entradas novas instaladas no prefixo
usam o diretório do executável dentro do `drive_c`; cadastros de executáveis
externos preservam o diretório original para localizar seus arquivos auxiliares.

### Resolução de Caminhos Windows
- `resolve_windows_path("C:\\Program Files\\App\\app.exe")`: Resolve para `<prefix>/drive_c/Program Files/App/app.exe`.
- Normaliza barras (`\\` → `/`) e drives virtuais (`C:`).
- `to_windows_path(linux_path)`: Converte caminhos dentro do prefixo para
  `C:\...`; um arquivo externo, como o setup selecionado, é exposto como
  `Z:\...`.
- O runtime fixa o prefixo ativo antes de iniciar o convidado. Caminhos Win32,
  CRT, diretório atual, `TEMP`/`TMP`, `APPDATA`, `LOCALAPPDATA` e
  `USERPROFILE` usam esse contexto. Isso é isolamento funcional de dados, não
  uma sandbox: `Z:` ainda representa o sistema de arquivos do hospedeiro.

Em execuções diretas e em aplicativos cadastrados fora do `drive_c`, o processo
filho usa o diretório do executável como diretório de trabalho. Assim, recursos
auxiliares relativos (`Lang`, DLLs, ícones e configurações) são procurados ao
lado do programa. Caminhos `C:` permanecem confinados ao `drive_c`; tentativas
com `..` que escapem do prefixo são rejeitadas.

---

## 2. Subsistema de Biblioteca / Catálogo (`catalog`)

Localização:
- Header: [include/tradutorlinux/catalog/app_catalog.hpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/include/tradutorlinux/catalog/app_catalog.hpp)
- Implementação: [src/catalog/app_catalog.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/catalog/app_catalog.cpp)

### Armazenamento
O catálogo é persistido em formato JSON em `~/.config/tradutorlinux/library.json`.

```json
{
  "version": 1,
  "apps": [
    {
      "id": "notepad_plus_plus",
      "name": "Notepad++",
      "executable_path": "/home/user/.tradutorlinux/drive_c/Program Files/Notepad++/notepad++.exe",
      "prefix_path": "/home/user/.tradutorlinux",
      "icon_path": "",
      "working_directory": "/home/user/.tradutorlinux/drive_c/Program Files/Notepad++",
      "created_at": "2026-08-19T12:00:00Z",
      "args": []
    }
  ]
}
```

---

## 3. Interface de Linha de Comando (CLI)

| Comando | Descrição |
|---|---|
| `tradutorlinux <app.exe>` | Execução direta de um executável PE32+. |
| `tradutorlinux install <setup.exe> [--name <Nome>] [--prefix <dir>] [--app-exe <caminho>]` | Executa o setup no prefixo exclusivo. Só cadastra após o exit `0`; `--app-exe` escolhe explicitamente o executável final dentro do `drive_c`. |
| `tradutorlinux app list` | Lista todos os aplicativos cadastrados na biblioteca. |
| `tradutorlinux app run <id_ou_nome> [args...]` | Executa um aplicativo cadastrado na biblioteca. |
| `tradutorlinux app add <app.exe> [--name <Nome>] [--prefix <dir>] [--id <id>]` | Cadastra manualmente um executável na biblioteca. Sem `--prefix`, cria prefixo exclusivo. |
| `tradutorlinux app remove <id>` | Remove o aplicativo do catálogo da biblioteca. |

### Resultado de `install`

Antes de executar o setup, o CLI registra os PE32+ AMD64 já presentes em
`drive_c`. Após um exit `0`, ele procura candidatos novos ou alterados:

- um candidato é cadastrado automaticamente;
- `--app-exe` é validado como arquivo PE32+ AMD64 contido no `drive_c`;
- zero ou mais de um candidato retornam `6` (`InstallPending`), preservam o
  prefixo e não criam entrada de catálogo;
- falha, timeout ou import não suportado nunca cadastram o setup nem um
  executável parcial.

Com `--trace`, os eventos `install` em `stderr` publicam `prepared`,
`candidate`, `registered`, `pending` ou `failed`, sempre com prefixo e ID.

---

## 4. Interface Gráfica do Launcher (`tradutorlinux_gui`)

A interface usa Qt6 e está implementada em
[`src/gui/main_window.cpp`](../../src/gui/main_window.cpp), com entrada em
[`src/gui/qt_main.cpp`](../../src/gui/qt_main.cpp). O alvo continua sendo
`tradutorlinux_gui`.

O launcher exibe a biblioteca, filtra aplicativos, permite selecionar um
executável pelo `QFileDialog` ou pelo campo de caminho e disponibiliza ações
assíncronas:

- **Analisar**: executa o relatório estático (`--report`) e captura o
  diagnóstico;
- **Executar**: inicia o programa com `--trace` sem bloquear a janela;
- **Instalar**: chama `install` com o nome opcional informado. Ao receber
  `registered`, atualiza a biblioteca; com vários `candidate`, pede a escolha
  e finaliza o cadastro sem executar novamente o setup;
- **Cadastrar**: salva ou atualiza a entrada no `library.json`;
- **Limpar**: restaura o formulário sem apagar a biblioteca.

A execução usa `QProcess`, procura o binário `tradutorlinux` no mesmo diretório
do launcher e separa stdout de stderr no console visual. O contrato completo,
os limites e os testes estão em
[`guia-ui-qt6.md`](guia-ui-qt6.md).

## 5. Pacote Debian

O CMake instala `tradutorlinux` e `tradutorlinux_gui` em `/usr/bin`, o launcher
`tradutorlinux.desktop` em `/usr/share/applications` e o ícone SVG no tema
Hicolor. O pacote é gerado por CPack com o gerador `DEB`.
