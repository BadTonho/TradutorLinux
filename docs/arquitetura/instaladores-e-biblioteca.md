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

Localização:
- [src/gui/launcher.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/gui/launcher.cpp)

A interface gráfica X11 do launcher exibe a contagem de aplicativos cadastrados na biblioteca, permite selecionar executáveis no disco via seletor nativo ou caixa de texto, e disponibiliza ações diretas:
- **Analisar**: Executa relatório estático (`--report`).
- **Executar**: Inicia a aplicação com diagnóstico.
- **Cadastrar**: Salva o executável atual na biblioteca `library.json`.
