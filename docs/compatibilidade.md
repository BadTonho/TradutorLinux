# Matriz de compatibilidade

Este arquivo é o índice da matriz de compatibilidade. Os contratos foram separados por tema para facilitar manutenção; a matriz continua registrando resolução de imports, execução, nível funcional e limitações. Ela não é uma promessa de compatibilidade geral com Windows.


Esta matriz registra resolução de imports, resultados de execução e limitações
conhecidas; ela não é uma promessa de compatibilidade geral com Windows.

## Como ler a matriz

Há três dimensões independentes:

- **`--report`/imports:** `supported` significa que todos os imports estáticos
  foram resolvidos; `unsupported` significa que ao menos uma dependência ou
  mecanismo não foi resolvido. Isso não comprova execução.
- **execução:** `not-attempted`, `passed` ou uma falha controlada como
  `GuestTimeout 72`, `GuestFault 71`, `GuestResourceLimit 73` ou um código
  explícito do convidado.
- **nível funcional:** o catálogo usa `inicia`, `fluxo principal restrito`,
  `fluxo principal` e `uso diário` somente quando há fluxo representativo,
  resultado observável e limitações publicadas.

Assim, um aplicativo pode ter `supported` na resolução de imports e continuar
`execution-failed` ou sem nível funcional no catálogo.

O probe local do PuTTY mantém essa distinção: as etapas E39–E41 validam o
transporte inicial de loopback, as notificações `WSAAsyncSelect` e `FD_CLOSE`,
e um `SSH_MSG_DISCONNECT` controlado, mas o fluxo ainda termina em
`GuestTimeout 72`; isso não promove o aplicativo nem o protocolo SSH a suporte
funcional.

### Análise aprofundada de requisitos com `--report`

O comando `tradutorlinux --report <app.exe>` realiza inspeção estática aprofundada sem executar a aplicação, antecipando requisitos de infraestrutura e limitações conhecidas:
- **Aceleração 3D (DirectX/Vulkan):** detecta dependências de `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `d3d9.dll`, `vulkan-1.dll` e `xinput1_4.dll`, recomendando `backend.kind: proton` quando gráficos 3D forem essenciais.
- **Ambiente gerenciado (.NET/CLR):** identifica binários dependentes de runtime .NET (`mscoree.dll` ou diretório COM descriptor), orientando a execução via `dotnet` ou Proton na ausência de compilação nativa AOT.
- **Drivers de kernel e anticheat:** detecta módulos de anticheat (EasyAntiCheat, BattlEye, Riot Vanguard, PunkBuster, Denuvo) e chamadas de gerenciamento de serviços/drivers (`CreateServiceW`, `OpenSCManagerW` em `advapi32.dll`), sinalizando a impossibilidade de execução nativa de módulos ring-0.
- **Packers e proteções (UPX, Themida, VMProtect):** detecta código compactado e seções anômalas com permissões W+X ou sem dados físicos (`raw_size=0`), prevenindo violações da política W^X em runtime nativo.
- **Mitigações de segurança (`DllCharacteristics`):** inspeciona o suporte a ASLR, High-Entropy VA, DEP/NX, Control Flow Guard (CFG) e AppContainer.
- **Identidade e versão (`RT_VERSION`):** extrai nome do produto, versão e fabricante diretamente da árvore de recursos `.rsrc`.
- **Inventário de recursos e manifesto (`RT_MANIFEST`):** inventaria tipos de recursos embutidos e requisitos de UAC (`asInvoker`, `requireAdministrator`), DPI e versões de SO suportadas.

O teste CTest `popular_apps_report_matrix`, habilitado quando
`TL_POPULAR_APPS_DIR` aponta para o corpus pinado, repete a análise estrutural
dos 26 PE selecionados e do pacote MSIX. Ele verifica os códigos esperados de
sucesso (`0`), rejeição estrutural (`4`) e formato/arquitetura não suportados
(`5`) sem mapear ou executar os arquivos; a mesma matriz é executada nos
builds Rust ON e C++ OFF.

O teste `popular_apps_recursive_report_matrix` amplia essa verificação para
todos os 64 arquivos PE, DLL e MSIX encontrados recursivamente no corpus
pinado, incluindo os diretórios extraídos de 7-Zip e Notepad++. Ele executa
somente `--report`, sem iniciar DLLs, instaladores ou pacotes rejeitados; após a
validação file-backed do diretório de exceções do Rufus, os builds Rust ON e
C++ OFF retornam `24` sucessos, `3` rejeições estruturais e `37`
formatos/arquiteturas não suportados em ambos os casos.

O teste `popular_apps_native_matrix` cobre seis casos de execução direta ou
rejeição pré-entry: 7-Zip e os dois WinRAR terminam com `0`, Rockstar preserva
seu `ExitProcess(3)`, Rufus é rejeitado com `4` no parsing do diretório de
exceções virtual-only e o GUP do Notepad++ é rejeitado com `5` quando a cadeia `libcurl.dll` exige
`WLDAP32.dll`. Cada caso recebe prefixo temporário, limites de CPU/memória e
timeout; isso é evidência de comportamento controlado, não uma promoção geral
de compatibilidade.

O teste `notepadpp_fh4_headless_smoke` acrescenta uma execução real do
`Notepad++/notepad++.exe` sem display: o runtime seleciona os catches FH4
tipados, limita uma cadeia observada de 13 cleanups a quatro ações seguras,
registra `fh4-cleanup-limit`, não produz `guest-signal` nem `guest-timeout` e
termina com `ExitProcess(0)`. A fixture FH4 também rejeita um auto-link cíclico
com diagnóstico controlado. Isso valida apenas o caminho de inicialização e
C++ EH observado; o fluxo GUI continua dependente do smoke Xvfb e não é
promovido a suporte diário.

O teste `popular_apps_install_matrix` cobre as instalações autorizadas de
Roblox (`3` após `RBXCRASH`), G HUB e seu alias (`1` após `ExitProcess(1)`) e
Affinity (`4` antes da extração), além das rejeições pré-extração de CPU-Z,
GPU-Z, HWMonitor e HWiNFO (`5`, `5`, `5` e `4`). Ele isola prefixo, `HOME`,
configuração e `APPDATA`, exige que não haja arquivos, extração ou cadastro
parcial e preserva o fallback genérico somente para instaladores cujo payload
PE32+ seja validado antes do cadastro.

## Documentos temáticos

| Documento | Conteúdo |
|---|---|
| [`compatibilidade-aplicativos.md`](compatibilidade-aplicativos.md) | Aplicativos de teste, corpus popular, instalação e evidências operacionais. |
| [`compatibilidade-runtime.md`](compatibilidade-runtime.md) | Loader PE, imagem, imports, console, GUI, CRT, arquivos, recursos, segurança, COM e concorrência. |
| [`compatibilidade-backends.md`](compatibilidade-backends.md) | Políticas Rust/C++, MSIX/AppX, perfis, catálogo e backend Proton. |

A matriz temática deve ser atualizada junto com o diagnóstico quando uma
função, fixture ou aplicativo mudar de estado.
