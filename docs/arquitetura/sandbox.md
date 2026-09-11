# Arquitetura e Especificação: Sandbox e Confinamento de Execução

## 1. Visão Geral e Motivação

O TradutorLinux executa binários PE32+ diretamente sobre o kernel Linux sem máquina virtual e sem emulação de CPU. Tradicionalmente em camadas de compatibilidade (como o Wine clássico), o processo convidado herda acesso amplo ao sistema de arquivos do hospedeiro — mapeando a raiz `/` como `Z:\` e mantendo acesso direto à pasta pessoal do usuário (`$HOME`), suas chaves SSH (`~/.ssh`), credenciais, navegadores e outros processos.

Executáveis Windows do mundo real (instaladores, utilitários freeware, ferramentas legadas ou binários obtidos na internet) apresentam riscos inerentes:
- Tentativas de escrita ou varredura de diretórios fora do seu escopo pretendido.
- Coleta de telemetria ou comunicação de rede não autorizada em softwares que deveriam ser puramente locais (como compactadores e conversores).
- Criação de arquivos persistentes em pastas do sistema hospedeiro.

Este documento especifica a arquitetura técnica do **Sistema de Sandbox e Confinamento** do TradutorLinux, projetado para isolar de forma estrita a execução de programas Windows, estabelecendo uma fronteira de segurança sem depender de privilégios de superusuário (`root`).

---

## 2. Diferenciação: Contenção Atual vs. Sandbox de Segurança

| Capacidade | Contenção Operacional Atual (`src/process/isolate.cpp`) | Sandbox de Segurança (Esta Especificação) |
|---|---|---|
| **Processo** | `fork()` com monitoramento via pipe e timeout | PID Namespace (`CLONE_NEWPID`) privado; invisibilidade de processos do host |
| **Recursos** | `RLIMIT_CPU` e `RLIMIT_AS` (memória virtual) | `cgroups v2` (opcional) e limites `rlimit` rígidos |
| **Filesystem** | Prefixo virtual `drive_c`, mas com `Z:\` mapeando para `/` | Mount Namespace (`CLONE_NEWNS`) estrito; `/` e `$HOME` invisíveis |
| **Rede** | Acesso normal herdado da pilha de rede do host | Network Namespace (`CLONE_NEWNET`) com política `none`, `loopback` ou `full` |
| **Syscalls** | Syscalls diretas permitidas ao processo filho | Filtros `seccomp-bpf` bloqueando chamadas perigosas (`ptrace`, `bpf`, etc.) |
| **Privilégios** | Processo roda como o usuário Linux comum | User Namespace (`CLONE_NEWUSER`) sem privilégios; isolamento de UID/GID |

---

## 3. Pilares da Arquitetura de Sandbox

### 3.1. Confinamento de Sistema de Arquivos (Mount Namespace)

O convidado deve ser executado em um ambiente onde o seu prefixo virtual (`drive_c`) represente a sua única visão de armazenamento persistente.

1. **Raiz e Prefixo Virtual:**
   - O `drive_c` do aplicativo (`~/.tradutorlinux/prefixes/<app_id>/drive_c`) é mapeado como a raiz do namespace através de `pivot_root` ou bind mount isolado.
   - O diretório `$HOME` do hospedeiro, `/etc`, `/root`, `/boot` e outras áreas sensíveis **não são montadas**.
2. **Dependências Estritamente Necessárias do Host (Somente Leitura):**
   - `/usr/lib`, `/lib64`, `/lib`: montados exclusivamente como `MS_RDONLY` para permitir que as bibliotecas do runtime (`libc`, drivers gráficos, X11) sejam carregadas.
   - `/usr/share/fonts`: montado como `MS_RDONLY` para resolução de tipografia em aplicativos com interface gráfica.
   - `/dev`: montagem de um `devpts` e nós mínimos estritamente seguros (`/dev/null`, `/dev/zero`, `/dev/random`, `/dev/urandom`, `/dev/dri` quando aceleração 3D for concedida).
3. **Compartilhamento Controlado de Diretórios (`shared_directories`):**
   - O usuário pode conceder acesso a pastas pontuais (ex.: `~/Downloads` ou uma pasta de trabalho específica).
   - O diretório compartilhado é montado sob um drive dedicado (ex.: `D:\` ou `C:\Shared`), com permissões configuráveis de apenas-leitura (`ro`) ou leitura/escrita (`rw`).

### 3.2. Isolamento de Rede (Network Namespace)

Nem todo aplicativo Windows precisa ou deve ter acesso à internet.

- **Modo `none` (Padrão para Utilitários Locais):**
  - O processo é iniciado em um network namespace desassociado de interfaces físicas. Nenhuma interface de rede além de `lo` (loopback desativada) existe.
  - Tentativas de usar sockets ou chamadas de `ws2_32.dll` / `wininet.dll` falham imediatamente com códigos controlados (`WSAENETUNREACH`), impossibilitando exfiltração de dados.
- **Modo `loopback` (Comunicação Local / IPC):**
  - Apenas a interface `127.0.0.1` é criada. Útil para ferramentas que executam serviços de desenvolvimento local sem expor portas para a rede física.
- **Modo `full` (Acesso Externo Explícito):**
  - Conexão de rede normal permitida (para navegadores, instaladores com download de componentes ou utilitários como PuTTY).

### 3.3. Isolamento de Processos e IPC

- **`CLONE_NEWPID`:** O processo Windows principal nasce como PID 1 dentro do seu namespace. Ele não pode enumerar, sinalizar ou interagir com nenhum processo do Linux hospedeiro.
- **`CLONE_NEWIPC`:** Filas de mensagens POSIX e memória compartilhada SysV do hospedeiro são inacessíveis.

### 3.4. Filtro de Chamadas de Sistema (Seccomp-BPF)

Um filtro `seccomp-bpf` é compilado e injetado no filho antes de chamar o entry point do PE:
- **Bloqueio de escalonamento:** bloqueia chamadas como `ptrace`, `process_vm_readv`, `process_vm_writev` contra processos fora do namespace.
- **Bloqueio de reconfiguração de sandbox:** bloqueia chamadas que alterem chroot, montagens ou namespaces adicionais.
- Em caso de violação de syscall não autorizada, o kernel encerra imediatamente o processo com `SIGSYS` (que é registrado pelo `crash_reporter` existente como `guest-signal SIGSYS`).

---

## 4. Motor de Execução: Bubblewrap vs. Implementação Própria

Para garantir máxima robustez sem introduzir vulnerabilidades no core do projeto, a estratégia recomendada adota duas camadas:

1. **Camada Primária (Recomendada): Orquestração via Bubblewrap (`bwrap`)**
   - O `bwrap` é o padrão da indústria no Linux (utilizado pelo Flatpak, Steam Container Runtime / pressure-vessel e Bottles).
   - Ele já é auditado formalmente por equipes de segurança globais, lida de forma segura com unprivileged user namespaces em diversas distribuições e trata casos de borda de montagens de `/dev` e `/proc`.
   - O TradutorLinux gera os argumentos do `bwrap` de forma transparente baseado no perfil da aplicação.

2. **Camada Secundária: Mecanismo Embutido em C++ (`unshare`/`clone3`)**
   - Disponível como fallback autônomo quando `bwrap` não estiver instalado no sistema.
   - Utiliza as APIs de sistema Linux padrão com verificações rígidas de retorno de erro.

---

## 5. Interface de Configuração e Perfis

### 5.1. Manifesto no `profile.json`

O arquivo de perfil do aplicativo (em `compat/apps/<app_id>/profile.json` ou no catálogo de aplicativos gerenciados) define a política de segurança desejada:

```json
{
  "app_id": "7zip",
  "name": "7-Zip File Manager",
  "sandbox": {
    "enabled": true,
    "mode": "strict",
    "network": "none",
    "filesystem": {
      "isolate_home": true,
      "deny_host_root": true,
      "shared_directories": [
        {
          "host_path": "$HOME/Downloads",
          "guest_drive": "D:",
          "access": "rw"
        }
      ]
    },
    "display": {
      "x11": true,
      "wayland": false
    }
  }
}
```

### 5.2. Opções da Linha de Comando (CLI)

Flags de controle na CLI para execuções ad-hoc:

- `--sandbox`: ativa o confinamento estrito com base na política padrão.
- `--no-sandbox`: desativa o confinamento (comportamento permissivo para depuração).
- `--network=<none|loopback|full>`: define a política de rede.
- `--share-dir <host_path>[:<drive_letter>[:<ro|rw>]]`: expõe um diretório pontual do host no ambiente isolado.

Exemplo de uso:
```bash
# Executa 7-Zip totalmente desconectado da rede e acessando apenas a pasta Downloads
tradutorlinux run --sandbox --network=none --share-dir ~/Downloads:D: 7zFM.exe
```

---

## 6. Roteiro de Implementação (Roadmap)

1. **Marco S1 — Abstração de Launcher Seguro:**
   - Criar `src/process/sandbox.cpp` e `include/tradutorlinux/process/sandbox.hpp`.
   - Detecção de capacidade do hospedeiro (`bwrap` instalado vs. suporte a user namespaces).
2. **Marco S2 — Isolamento de Filesystem e Rede:**
   - Confinamento estrito de `drive_c`.
   - Remoção do link `Z:` para a raiz quando a sandbox estiver ativa.
   - Implementação da política de rede `none` via `CLONE_NEWNET`.
3. **Marco S3 — Compartilhamento Granular de Diretórios:**
   - Mapeamento dinâmico de diretórios informados na flag `--share-dir` para drives virtuais `D:\`, `E:\`, etc.
4. **Marco S4 — Filtros Seccomp e Diagnósticos:**
   - Injeção de perfil seccomp padrão.
   - Eventos de auditoria e diagnóstico no `--trace` indicando restrições ativas e acessos barrados.
