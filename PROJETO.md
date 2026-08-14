# TradutorLinux — Camada de Compatibilidade para Aplicativos Windows no Linux

## 1. Visão Geral

Projeto pessoal para criar uma camada de compatibilidade que permita **rodar programas Windows no Linux**, sem precisar de máquina virtual. O objetivo é um projeto educativo e funcional no estilo do Wine, escrito do zero em **C/C++**.

Em vez de emular o hardware do Windows (como uma VM faz), nós vamos **traduzir as chamadas de sistema Windows** (Win32 API) para as chamadas equivalentes do Linux. Assim o programa Windows acha que está rodando em Windows, mas por baixo está usando o kernel Linux.

## 2. Por que fazer isso?

| Motivo | Explicação |
|---|---|
| Aprendizado profundo | Entender como um SO funciona por dentro: processos, memória, DLLs, formatos de binário |
| Controle total | Não depender das regras de licenciamento do Wine |
| Desafio técnico | Projeto longo que combina baixo nível (asm, ELF/PE) com alto nível (APIs, UI) |
| Portabilidade | O conhecimento vale para Linux, macOS e outros SOs |

## 3. Objetivos

### 3.1. Objetivos do projeto (Geral)
- Rodar programas Windows simples no Linux.
- Ser um projeto de estudo: código legível e bem comentado (aqui comentários são bem-vindos).
- Funcionar sem máquina virtual, traduzindo chamadas de sistema.

### 3.2. Objetivos da primeira versão (MVP)
- Carregar um executável **PE (Portable Executable)** — o formato de `.exe` do Windows.
- Resolver as **importações** (chamar funções de `kernel32.dll`, `user32.dll`, etc.).
- Implementar um pequeno conjunto de funções Win32 (mensagens de texto, alocação de memória, console).
- Rodar um programa "Hello World" Windows e um programa de console simples.

### 3.3. Fora de escopo (por enquanto)
- Suporte a DirectX / 3D.
- Emulação de GPU/drivers.
- Programas que exigem ActiveX, .NET, drivers de dispositivo.
- 100% de compatibilidade com a Win32 API (isso é trabalho de anos, até para o Wine).

## 4. Como funciona um programa Windows?

1. O arquivo é um **PE**: tem cabeçalhos, seções (`.text` = código, `.data` = dados, `.rdata`, `.rsrc` = recursos) e uma tabela de **importações**.
2. Quando o Windows abre o `.exe`, ele:
   - Mapeia as seções na memória (cada seção em um endereço, com permissões).
   - Carrega as DLLs importadas (`kernel32.dll`, `user32.dll`, ...).
   - Liga as funções importadas aos endereços reais.
   - Chama o ponto de entrada (normalmente `mainCRTStartup`).
3. A partir daí, o programa vive chamando funções da Win32 API.

## 5. Arquitetura proposta

```
┌───────────────────────────────────────────────┐
│              Programa Windows (.exe)          │
│         (binário PE — acredita que está       │
│          rodando no Windows)                  │
└──────────────────────┬────────────────────────┘
                       │ chamadas Win32 API
┌──────────────────────▼────────────────────────┐
│              Camada de compatibilidade        │
│    ┌───────────────┐  ┌───────────────────┐   │
│    │ PE Loader     │  │ DLLs "falsas"     │   │
│    │ (carrega .exe)│  │ (kernel32, user32 │   │
│    │ + resolve     │  │  — implementadas  │   │
│    │   imports)    │  │  por nós)         │   │
│    └───────────────┘  └───────────────────┘   │
└──────────────────────┬────────────────────────┘
                       │ chamadas de sistema Linux
┌──────────────────────▼────────────────────────┐
│                 Linux (kernel)                │
│         (fork/exec, syscalls, X11/Wayland)    │
└───────────────────────────────────────────────┘
```

### 5.1. Componentes principais

| Componente | Função | Estado |
|---|---|---|
| **PE Loader** | Ler o `.exe`, mapear seções, localizar ponto de entrada | ✗ a fazer |
| **Import Resolver** | Achar as funções importadas e conectá-las às nossas DLLs | ✗ a fazer |
| **DLL Stubs** | Funções falsas de `kernel32.dll`/`user32.dll` que chamam o Linux por baixo | ✗ a fazer |
| **Memory Manager** | Alocar memória no processo Linux no estilo Win32 (`VirtualAlloc` etc.) | ✗ a fazer |
| **Console Win32** | Texto no terminal (`WriteFile`, `ReadFile` com handles) | ✗ a fazer |
| **UI mínima** | Janelas básicas via X11/Wayland (`MessageBox` como primeiro teste) | ✗ a fazer |

## 6. Roadmap (fases)

### Fase 0 — Fundação (documentação e setup)
- [ ] Definir estrutura de pastas e build system (CMake).
- [ ] Escolher distribuição Linux e ferramentas (gcc/clang).
- [ ] Escrever testes: compilar um `.exe` de teste ("Hello World") no Windows (ou com `mingw-w64` no Linux).

### Fase 1 — Entender o formato PE
- [ ] Escrever um leitor de PE que imprime: cabeçalhos, seções, imports.
- [ ] Validar contra binários reais (`file`, `objdump -x`, `llvm-objdump`).

### Fase 2 — Loader mínimo
- [ ] Mapear as seções do PE na memória.
- [ ] Pular para o ponto de entrada e executar um `.exe` que não usa API (ex.: `ret` puro / `int 3`).
- [ ] Detectar o momento em que o programa faz uma chamada Win32.

### Fase 3 — Primeiras DLLs (console)
- [ ] Implementar `kernel32.dll` parcial: `GetStdHandle`, `WriteFile`, `ReadFile`, `ExitProcess`.
- [ ] Rodar um "Hello World" de console Windows no Linux. 🎉 (primeiro marco!)

### Fase 4 — Memória e sistema
- [ ] `VirtualAlloc` / `VirtualFree`.
- [ ] `CreateFile`, `ReadFile`/`WriteFile` em arquivos reais.
- [ ] `GetSystemInfo`, `GetTickCount`, etc.

### Fase 5 — Interface gráfica mínima
- [ ] `MessageBox` renderizado em X11/Wayland.
- [ ] `CreateWindow` / `DefWindowProc` com janela básica.

### Fase 6 — Expandir cobertura
- [ ] Mais funções Win32, tratar arquivos `.dll` dinâmicos, recursos (.rsrc), etc.

## 7. Estrutura de pastas proposta

```
TradutorLinux/
├── PROJETO.md            ← este documento
├── CMakeLists.txt
├── README.md
├── docs/                 ← documentação técnica detalhada
│   ├── formatos/         ← anotações sobre PE, PE32+, etc.
│   └── api/              ← anotações sobre funções Win32 implementadas
├── src/                  ← código C/C++
│   ├── loader/           ← PE loader + import resolver
│   ├── dlls/             ← DLLs falsas (kernel32, user32, ...)
│   ├── memory/           ← gerenciamento de memória
│   └── main.cpp          ← ponto de entrada da camada
└── tests/
    ├── samples/          ← .exe de teste ("Hello World", etc.)
    └── unit/             ← testes do loader
```

## 8. Ferramentas / Stack

- **Linguagem:** C++ (C++17 ou superior) com partes de C e asm quando preciso.
- **Build:** CMake + Make/Ninja.
- **Compiladores:** gcc/clang no Linux.
- **Binários de teste:** `mingw-w64` (permite compilar `.exe` no Linux para testar).
- **Debug/inspeção:** `objdump`, `llvm-objdump`, `readelf`, `gdb`, `strace`, `ltrace`.
- **UI (futuro):** Xlib/XCB ou Wayland.
- **Controle de versão:** git (iniciar repositório local).

## 9. Riscos e desafios

| Desafio | Como vamos lidar |
|---|---|
| Formato PE é complexo | Estudar com binários de exemplo e ferramentas de disassembly |
| Programa Windows real chama muitas APIs | Começar com `.exe` feitos por nós (via mingw), aumentando a complexidade aos poucos |
| Chamadas de sistema mudam entre versões do Windows | Só precisamos do comportamento, não da implementação interna |
| Depuração é difícil (não temos símbolos de código Windows) | Usar `strace` para ver syscalls; instrumentar nossas DLLs |
| Escopo pode explodir | Manter MVP pequeno e documentado; cada fase só começa com a anterior concluída |

## 10. Critérios de sucesso

- **MVP:** rodar `hello_windows.exe` (console) no Linux apenas com nossa camada.
- **Marco 2:** rodar um programa com arquivos (ler/escrever) e memória.
- **Marco 3:** mostrar um `MessageBox` nativo.

## 11. Como testar um `.exe` sem ter Windows

Instalar o `mingw-w64` no Linux permite compilar código C/C++ para Windows:

```bash
sudo apt install gcc-mingw-w64-x86-64
x86_64-w64-mingw32-gcc hello.c -o hello.exe
```

Depois é só rodar com a nossa camada:

```bash
./tradutorlinux hello.exe
```
