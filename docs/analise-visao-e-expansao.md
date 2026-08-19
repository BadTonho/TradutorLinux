# TradutorLinux — Análise de Visão, Suporte a Instaladores e Biblioteca de Aplicativos

Este documento consolida a análise de aderência do projeto à visão de compatibilidade ampla com aplicativos Windows, os requisitos para suportar instalação de aplicativos (setups) e a especificação da biblioteca/launcher para gerenciar e executar aplicativos cadastrados.

---

## 1. Diagnóstico Geral de Alinhamento Técnico

O projeto **está seguindo a linha correta da ideia**.

A espinha dorsal do **TradutorLinux** implementa uma **camada de compatibilidade em espaço de usuário** para sistemas Linux x86-64 executando nativamente binários PE32+ (x86-64) do Windows:

- **Execução Nativa de CPU:** Sem emuladores de CPU nem máquinas virtuais. As instruções de máquina x86-64 do aplicativo rodam diretamente no processador hospedeiro.
- **Ponte de ABI e Loader Seguro:** O parser de PE ([pe_reader.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/pe/pe_reader.cpp)), o mapeador de memória ([image_mapper.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/loader/image_mapper.cpp)) e os trampolins de ABI tratam as convenções de chamada Microsoft x64 ↔ System V AMD64 com validação estrita de limites.
- **Isolamento de Processo Convidado:** O convidado roda isolado em processo filho via `fork`/`waitpid` ([isolate.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/process/isolate.cpp)), garantindo que falhas do convidado (como `SIGSEGV`) não derrubem o runtime.
- **Emulação Modular de DLLs:** Implementações limpas de `KERNEL32`, `MSVCRT`, `USER32`, `GDI32`, `ADVAPI32` e `WS2_32`.

---

## 2. Estratégia para o "Máximo de Aplicativos"

Para tornar o runtime útil para uma ampla gama de softwares comerciais e utilitários sem gerar código instável ou inseguro:

1. **Avanço por Classes de Aplicações:**
   - **Utilitários de Console:** Concluído (ex: `tl_hello`, `tl_echo`, `xxd`, `bzip2`, `dos2unix`).
   - **Manipulação de Arquivos e Concorrência:** Concluído (ex: `tl_files_wide`, `tl_sync`, `tl_thread`).
   - **GUI Básica com Controles Win32:** Concluído (ex: `tl_win`, `tl_gui`, `simple_todo`).
   - **Rede e Registro:** Base iniciada (ex: `tl_network_loopback`, `tl_registry_unicode`).
   - **Instaladores e Aplicativos Multimídia:** Próxima fronteira estratégica.

2. **Decisões Arquiteturais Necessárias:**
   - Refatoração do arquivo monolítico [winapi.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/runtime/winapi.cpp) em subsistemas isolados (`runtime/file_system`, `runtime/threading`, `runtime/memory`, `runtime/gui`).
   - Substituição gradual de limites fixos globais por um contexto de processo dinâmico (`GuestContext`).

---

## 3. Requisitos para Suportar Instalação de Aplicativos (Setups / Installers)

Instaladores como **Inno Setup**, **NSIS**, **InstallShield** ou instaladores proprietários (ex: instaladores web como o analisado no estudo de caso `RobloxPlayerInstaller.exe`) exigem um ecossistema de ambiente Windows simulado:

```text
Instalador Windows (.exe)
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│                 Ambiente Virtual / Prefixo                  │
│  - Drive C:\ virtualizado em ~/.tradutorlinux/drive_c/      │
│  - Pastas padrão: Program Files, AppData, Windows, Temp     │
│  - Variáveis de ambiente: %PROGRAMFILES%, %APPDATA%, %TEMP% │
└─────────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│                Subsistemas de Instalação                    │
│  ├─ Registro (ADVAPI32): HKLM\Software, HKCU\Software       │
│  ├─ Processos (CreateProcessW): Extração e sub-instaladores │
│  ├─ Filesystem Unicode: Extração em massa de arquivos       │
│  └─ Shell (SHELL32): Criação de atalhos e ícones            │
└─────────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│                 Integração com Desktop                      │
│  - Geração de arquivos .desktop em ~/.local/share/applications│
│  - Extração de ícones PE para ~/.local/share/icons/         │
│  - Cadastro automático na Biblioteca do TradutorLinux       │
└─────────────────────────────────────────────────────────────┘
```

### Componentes Chave:
1. **Drives Virtuais e Prefixos (Prefix Management):**
   - Criação de uma estrutura padrão em `~/.tradutorlinux/drive_c/` contendo:
     - `Program Files/`
     - `Program Files (x86)/`
     - `users/$USER/AppData/Local` e `Roaming`
     - `windows/system32/`
     - `windows/temp/`
   - Normalização de caminhos no runtime para redirecionar `C:\...` para a pasta do prefixo.
2. **Expansão do Registro (`ADVAPI32`):**
   - O armazenamento já implementado em [advapi.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/runtime/advapi.cpp) deve suportar chaves padrão (`HKEY_LOCAL_MACHINE\Software`, `HKEY_CURRENT_USER\Software`, `Uninstall` keys).
3. **Execução de Processos Filhos (`CreateProcessW`):**
   - Garantir que instaladores consigam invocar descompactadores internos e instaladores auxiliares, compartilhando o mesmo contexto de prefixo.

---

## 4. Arquitetura da Biblioteca de Aplicativos (Launcher / App Manager)

Para permitir que o usuário cadastre, organize e abra aplicativos Windows instalados:

### 4.1. Catálogo de Aplicativos (`library.json`)
Armazenamento local em `~/.config/tradutorlinux/library.json`:
```json
{
  "version": 1,
  "default_prefix": "~/.tradutorlinux/drive_c",
  "apps": [
    {
      "id": "notepad_plus_plus",
      "name": "Notepad++",
      "executable": "C:\\Program Files\\Notepad++\\notepad++.exe",
      "real_path": "/home/user/.tradutorlinux/drive_c/Program Files/Notepad++/notepad++.exe",
      "icon_path": "/home/user/.local/share/icons/notepad++.png",
      "args": [],
      "env": {},
      "working_directory": "/home/user/.tradutorlinux/drive_c/Program Files/Notepad++",
      "installed_at": "2026-08-19T12:00:00Z"
    }
  ]
}
```

### 4.2. Comandos CLI do TradutorLinux
Expansão dos comandos do CLI:

| Comando | Função |
|---|---|
| `tradutorlinux <arquivo.exe>` | Execução direta de um executável (comportamento atual). |
| `tradutorlinux install <setup.exe>` | Executa o instalador dentro do prefixo e detecta o binário principal para cadastro. |
| `tradutorlinux app list` | Lista todos os aplicativos cadastrados na biblioteca. |
| `tradutorlinux app run <id_ou_nome>` | Executa o aplicativo cadastrado com seu ambiente e configurações salvas. |
| `tradutorlinux app add <caminho.exe> --name "Nome"` | Cadastra manualmente um aplicativo existente na biblioteca. |
| `tradutorlinux app remove <id>` | Remove o aplicativo do catálogo da biblioteca. |

### 4.3. Interface Gráfica da Biblioteca (Launcher GUI)
- Uma interface visual leve (podendo ser em C++ com backend X11/Wayland ou toolkit nativo) que exibe a grade de aplicativos instalados, banners/ícones, botão "Instalar Novo Aplicativo", configurações de inicialização e logs de diagnóstico por aplicativo.

---

## 5. Estrutura em Camadas do Sistema

```text
┌────────────────────────────────────────────────────────┐
│            Interface do Usuário / Launcher             │
│    (CLI de biblioteca: `app list/run` + GUI do Launcher)│
├────────────────────────────────────────────────────────┤
│          Gerenciador de Catálogo & Prefixos            │
│  (Cadastro de apps, drives virtuais C:\, atalhos .desktop) │
├────────────────────────────────────────────────────────┤
│           TradutorLinux Runtime (Core Atual)           │
│   ├─ PE Loader & ABI Bridge (Microsoft x64 ↔ SysV)     │
│   ├─ Win32 APIs (Kernel32, User32, Gdi32, Advapi, etc.)│
│   └─ Isolamento de Processos (fork/waitpid)            │
└────────────────────────────────────────────────────────┘
```

---

## 6. Próximos Passos Recomendados

1. **Subdivisão do Runtime:** Refatorar [winapi.cpp](file:///c:/Users/Admin/Desktop/ProjetosCode/Linux/TradutorLinux/src/runtime/winapi.cpp) para permitir crescimento limpo das APIs.
2. **Definição do Sistema de Prefixos:** Implementar a estrutura de diretórios e o mapeamento de unidades Windows (`C:\`).
3. **Módulo de Biblioteca CLI:** Criar o gerenciador `app_catalog` (leitura/escrita de `library.json` e comandos `app list`, `app run`, `app add`).
4. **Alvos de Teste de Instalação:** Adicionar fixtures ou alvos de teste com instaladores de código aberto para validar o fluxo `install -> register -> launch`.
