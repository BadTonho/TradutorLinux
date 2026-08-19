# Arquitetura: Instaladores e Biblioteca de Aplicativos

Este documento detalha os contratos técnicos do subsistema de **Prefixos de Ambiente** e da **Biblioteca de Aplicativos (Catalog/Launcher)** do TradutorLinux.

---

## 1. Subsistema de Prefixos (`prefix`)

Localização:
- Header: [include/tradutorlinux/prefix/prefix.hpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/include/tradutorlinux/prefix/prefix.hpp)
- Implementação: [src/prefix/prefix.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/prefix/prefix.cpp)

### Estrutura de Diretórios
Por padrão, o prefixo do TradutorLinux reside em `~/.tradutorlinux/` e contém o drive virtual `drive_c/`:
```text
~/.tradutorlinux/
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

### Resolução de Caminhos Windows
- `resolve_windows_path("C:\\Program Files\\App\\app.exe")`: Resolve para `<prefix>/drive_c/Program Files/App/app.exe`.
- Normaliza barras (`\\` → `/`) e drives virtuais (`C:`).
- `to_windows_path(linux_path)`: Converte caminhos Linux dentro do prefixo de volta para o formato `C:\...`.

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
| `tradutorlinux install <setup.exe> [--name <Nome>] [--prefix <dir>]` | Inicializa o ambiente de prefixo, cadastra o aplicativo no catálogo e executa o instalador. |
| `tradutorlinux app list` | Lista todos os aplicativos cadastrados na biblioteca. |
| `tradutorlinux app run <id_ou_nome> [args...]` | Executa um aplicativo cadastrado na biblioteca. |
| `tradutorlinux app add <app.exe> [--name <Nome>] [--prefix <dir>]` | Cadastra manualmente um executável na biblioteca. |
| `tradutorlinux app remove <id>` | Remove o aplicativo do catálogo da biblioteca. |

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
- **Cadastrar**: salva ou atualiza a entrada no `library.json`;
- **Limpar**: restaura o formulário sem apagar a biblioteca.

A execução usa `QProcess`, procura o binário `tradutorlinux` no mesmo diretório
do launcher e separa stdout de stderr no console visual. O contrato completo,
os limites e os testes estão em
[`guia-ui-qt6.md`](guia-ui-qt6.md).
