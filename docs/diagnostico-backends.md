# Diagnóstico — backends e instalação

Este documento contém os eventos e contratos dos backends Rust/C++, Proton, instalação e parsers de pacotes, perfis e catálogo.

## Validação lexical operacional e promoção B20.6 com Rust

Quando o runtime foi compilado com `TL_BUILD_RUST=ON`, `app run --trace` cria
uma sessão Rust para o carregamento do perfil e outra para a materialização de
`files[]`. Cada sessão reutiliza um handle local e emite uma métrica ao final
da fase:

```text
[tl][runtime][info] path-validation phase="profile" backend="rust" handle-count="1" checks="2" rejected="0" duration-us="..." status="completed" detail=""
[tl][runtime][info] path-validation phase="files" backend="rust" handle-count="1" checks="2" rejected="0" duration-us="..." status="completed" detail=""
```

`checks` conta as entradas examinadas, `rejected` conta rejeições lexicais e
`duration-us` é a duração acumulada da fase. O campo `status` é
`completed`, `invalid-input` ou `internal-error`. Um caminho lexicalmente
inválido mantém o fallback genérico do runtime nativo e aparece junto de
`compat-profile status="invalid"` ou `compat-files status="rejected"`.

Uma falha interna do adaptador, um status inesperado ou a impossibilidade de
criar o handle é falha fechada: o convidado não é iniciado, o erro é emitido
com a fase e o detalhe e o comando retorna `70` (`InternalError`). O C++ ainda
faz todas as verificações físicas depois da pré-validação Rust. Com
`TL_BUILD_RUST=OFF`, o caminho C++ e os diagnósticos existentes permanecem
ativos e nenhum evento `path-validation` Rust é emitido.

A B20.6 promove somente essa validação lexical opt-in. Ela não altera os
diagnósticos de imports nem declara suporte funcional a aplicativos reais. Em
particular, `unknown-dll` continua significando módulo não registrado e
`unknown-symbol` significa módulo conhecido sem o export solicitado; ambos
retornam `5` antes do entry point quando a resolução falha.

No Proton, a métrica da materialização usa o componente `proton`:

```text
[tl][proton][info] path-validation phase="files" backend="rust" handle-count="1" checks="2" rejected="0" duration-us="..." status="completed" detail=""
```

O arquivo auxiliar é limpo após o processo, inclusive quando o convidado
termina por timeout. O evento de limpeza indica `cleaned` ou
`cleanup-failed`; a origem em `compat/files/` não é removida.

Na promoção B20.6, o gate específico de validação lexical passou 7/7 em
Debug, Sanitize e Release, e a comparação com `TL_BUILD_RUST=OFF` confirmou
que o caminho C++ não emite eventos `path-validation`. A suíte completa
Sanitize ainda registra dez falhas históricas ou ambientais fora desse gate
(ASan/UBSan em helpers/fixtures, `RLIMIT_AS`, imagens sem relocations e
Xvfb/LSan); elas não devem ser confundidas com uma falha do adaptador Rust.

## Seleção do backend Proton

Na B14.6, `app run` pode selecionar explicitamente o backend Proton por meio de
um perfil schema 3. Sem `backend`, o runtime próprio continua sendo usado. O
diagnóstico não substitui um Proton solicitado por `native` quando a instalação
está ausente ou inválida:

```text
[tl][proton][info] provider-selected kind="proton" version="11.0-1"
[tl][proton][info] staging-complete source=".../drive_c/Program Files/App" target=".../compatdata/pfx/drive_c/Program Files/App"
[tl][proton][info] launch launcher=".../proton" working-directory="..."
[tl][proton] mock-or-proton-stderr
[tl][proton][info] files-cleanup removed-files="1" removed-directories="0" retained="0"
[tl][proton][info] exit exit-code="0"
```

Para o slice D3D12, `integration_proton_d3d12` usa a fixture
`tl_d3d12_probe.exe`. A saída esperada é `D3D12 command path ready` com exit
code `0`; o teste confirma a criação do dispositivo, fila, command list,
fence, swapchain flip de dois buffers e `Present` sob Proton Experimental.
Essa validação cobre um caminho controlado D3D12→VKD3D-Proton/Vulkan, não
D3D12 completo, áudio ou entrada. Em caso de rejeição da descrição da
swapchain, a fixture escreve o HRESULT no stderr para diagnóstico.

Para o slice de entrada, `integration_proton_input` usa
`tl_input_probe.exe` e o driver `proton_input_driver`. O driver injeta
movimento, botão esquerdo e `q` com X11/XTest; a saída esperada é
`Proton input ready` com exit code `0`. A fixture somente encerra após
observar `WM_CREATE`, `WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_LBUTTONUP`,
`WM_KEYDOWN`, `WM_CHAR` e `WM_KEYUP`. Falha do driver ou ausência da janela
retorna erro de integração sem deixar o processo Proton continuar em segundo
plano.

Para o slice de áudio, `integration_proton_audio` usa
`tl_audio_probe.exe`. A saída esperada é `Proton audio ready` com exit code
`0`; o teste confirma `XAudio2Create`, criação das vozes master/source,
`SubmitSourceBuffer`, `Start`, `Stop` e liberação dos recursos. O diagnóstico
prova a cadeia de engine/vozes no host de teste, não a qualidade do som nem a
compatibilidade de todo dispositivo ou codec.

O piloto real de isolamento usa `integration_proton_isolation` com
`tl_compat_file.exe`, os IDs `proton-isolation-a` e `proton-isolation-b` e
prefixos separados. Ambos os perfis podem declarar o mesmo destino Windows;
os marcadores diferentes permitem confirmar pelo stdout que cada execução leu
somente sua fonte. O teste exige, para cada ID, os eventos `provider-selected`,
`staging-complete`, `launch`, `files-cleanup` e `exit`, além de confirmar que o
destino temporário foi removido, a fonte nativa permaneceu e nenhum diretório
`compat/` foi copiado para o `drive_c` Proton. Os manifestos e executáveis
estagiados continuam separados. Essa integração passou em Debug e Sanitize
com Proton Experimental real; ela promove o backend como opcional, não declara
suporte a aplicativos do catálogo e mantém Roblox como alvo exploratório.

O componente `proton` pode ser selecionado com `--trace=proton` ou incluído
em `--trace=proton,process`. Um backend solicitado sem launcher, componentes,
arquitetura, hash ou versão mínima válidos registra `provider-rejected` e
retorna `5` (`Unsupported`). Perfil Proton com `dlls[]` também é rejeitado
antes do lançamento. O stdout do processo convidado permanece sem prefixo;
mensagens do launcher são encaminhadas ao stderr com o contexto
`[tl][proton]`. Sinal, timeout, limite de recurso e falha interna mantêm,
respectivamente, os códigos `71`, `72`, `73` e `70` definidos pelo runtime.

Quando a integração real está configurada com
`-DTL_PROTON_ROOT=/caminho/para/Proton`, o CTest
`integration_proton_graphics` executa `tl_graphics_probe.exe` sob Xvfb. A
fixture cria uma janela, inicializa D3D11 e apresenta um frame; a saída
esperada é `D3D11 frame presented` com exit code `0`. A evidência é restrita ao
caminho D3D11→DXVK/Vulkan em X11. O trace do adaptador continua no stderr:

```text
[tl][proton][info] provider-selected kind="proton" version="..."
[tl][proton][info] staging-complete source="..." target="..."
[tl][proton][info] launch launcher=".../proton" working-directory="..."
[tl][proton][info] files-cleanup removed-files="0" removed-directories="0" retained="0"
[tl][proton][info] exit exit-code="0"
```

## Eventos do grafo de DLLs por perfil

Quando uma execução nativa usa um perfil v2, o componente `loader` registra o
provider escolhido e o ciclo de vida das DLLs PE32+ AMD64. Os eventos usam
`module` normalizado e, quando aplicável, `provider` com `profile`, `drive_c`
ou `builtin`:

```text
[tl][loader][info] dll-found module="compat.dll" provider="profile"
[tl][loader][info] dll-mapped module="compat.dll" provider="profile"
[tl][loader][info] provider-selected module="compat.dll" provider="profile"
[tl][loader][info] import-resolved module="KERNEL32.dll" provider="builtin"
[tl][loader][info] fallback-export module="compat.dll" provider="drive_c" detail=""
[tl][loader][info] tls-callback module="compat.dll" detail="process-attach"
[tl][loader][info] dll-attach module="compat.dll" provider="profile"
[tl][loader][info] module-refcount module="compat.dll" detail="0"
[tl][loader][info] dll-detach module="compat.dll" provider="profile"
[tl][loader][info] dll-unload module="compat.dll" provider="profile"
```

Os eventos `dll-found`, `dll-mapped`, `provider-selected` e `import-resolved`
identificam descoberta, mapeamento, provider efetivo e importação resolvida.
`fallback-export` identifica a continuação de uma exportação ausente no
provider anterior. `tls-callback`, `dll-attach`, `dll-detach` e
`dll-unload` mostram a ordem de ciclo de vida; `module-refcount` mostra a soma
das referências estáticas e dinâmicas. Dependência ausente, ciclo, arquivo
inválido, import não resolvido ou falha de `DllMain` em uma DLL do perfil usam
`provider-rejected`, com o motivo em `detail`, e descartam o provider inteiro
antes de executar o entry point. O runtime então registra o provider de
fallback quando houver um.

Para a busca normal de DLLs PE, o primeiro diretório de arquivo é o diretório
da aplicação solicitante, inclusive quando o executável foi chamado diretamente
fora do prefixo. Depois dele vêm `drive_c`, `C:\Windows\System32`,
`C:\Windows` e a raiz de `C:`. O trace mantém os eventos de descoberta,
mapeamento, attach e unload; a execução de código encontrado ao lado do
aplicativo continua sujeita às mesmas validações PE32+ AMD64, relocations,
imports, W^X e isolamento do convidado.

O loader não registra nem expõe a pasta `compat/` ao convidado. Não há evento
de descoberta automática: dependências de DLLs nessa pasta também precisam ser
declaradas no `dlls[]`. O código PE personalizado roda com os privilégios do
processo filho; uma falha depois do attach é `guest-signal`, `guest-fault` ou
outro erro da execução, sem fallback silencioso. `--report` permanece
inalterado e não carrega DLLs do perfil.

A partir do diagnóstico de falhas, o convidado executa em um processo filho
isolado (`fork`/`waitpid`). O processo hospedeiro prepara o PE, o mapeamento e
os imports, inicia o convidado no filho, espera o término e distingue saída
normal de término por sinal. Os handlers de sinais fatais do filho são
restaurados para o padrão antes do entry point, para que um `SIGSEGV` do
convidado não seja interceptado pelo runtime do hospedeiro (ex.: AddressSanitizer).

Quando o convidado termina por um sinal Linux, o componente `process` emite um
evento `terminated` de nível `error` com a categoria `guest-signal`, o nome do
sinal e uma descrição:

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória"
```

Falha ao criar o processo filho emite o mesmo evento com `category="internal-error"`.
O exit code bruto do convidado é transmitido por um pipe interno e propagado
como status do processo Linux; quando o convidado morre por sinal, o hospedeiro
retorna `71` (`GuestFault`).

O CLI aceita `--timeout <segundos>` para limitar a duração do convidado (padrão
`0`, sem limite). Quando o limite expira, o hospedeiro envia `SIGKILL` ao
filho, aguarda o término, emite o evento `terminated` com `category="guest-timeout"`
e `timeout-ms`, e retorna `72` (`GuestTimeout`):

```text
[tl][process][error] terminated category="guest-timeout" timeout-ms="1000"
```

Em `app run`, a opção pode ser informada depois do ID do catálogo, por
exemplo `app run meu-id --timeout 1`. A exposição de compatibilidade é limpa
antes de o comando devolver `72`; se a limpeza não puder remover algum caminho
alterado pelo convidado, o diagnóstico conserva esse caminho para evitar
apagamento indevido.

O probe local do PuTTY usa o mesmo diagnóstico de processo para registrar uma
limitação de rede sem introduzir um evento específico no runtime: depois de
configurar `PuTTY Configuration`, o servidor privado pode receber zero bytes e
o convidado terminar com `category="guest-timeout"` (`72`). Esse resultado é
distinto de um handshake SSH válido e não promove o aplicativo a suporte geral;
o cenário não acessa a Internet e compara Rust ON com C++ OFF.

O CLI também aceita `--cpu <segundos>` e `--memory <MiB>` para instalar limites
opcionais no filho isolado; `0` significa sem limite. `--cpu` mede tempo de CPU
(`RLIMIT_CPU`) e `--memory` limita o espaço de endereçamento virtual
(`RLIMIT_AS`). Os limites são herdados por processos Win32 criados pelo
convidado porque o launcher usa `fork`; isso é contenção de recursos, não uma
sandbox. O evento de instalação identifica a herança:

```text
[tl][process][info] resource-limits inheritance="fork" cpu-seconds="1" memory-mib="128"
```

Quando o limite de CPU é excedido, o Linux entrega `SIGXCPU`; o runtime preserva
o host, emite `guest-resource-limit` e retorna `73` (`GuestResourceLimit`):

```text
[tl][process][error] terminated category="guest-resource-limit" resource="cpu" signal="SIGXCPU" limit-seconds="1"
```

Se um limite não puder ser instalado, o runtime emite
`resource-limit-setup-failed` com `category="internal-error"` e retorna `70`.
Falhas de alocação sob `RLIMIT_AS` continuam sendo reportadas pela API Win32
convidada — por exemplo, `VirtualAlloc` retorna nulo e `GetLastError` informa
`ERROR_NOT_ENOUGH_MEMORY` — e não são confundidas com OOM global do hospedeiro.

O filho ignora `SIGPIPE` antes do entry point: uma escrita do convidado em um
pipe sem leitor (ex.: `tradutorlinux prog.exe | head -c 0`) falha com
`errno=32` e `win32-error="109"` (`ERROR_BROKEN_PIPE`) no evento
`linux-failure` do `WriteFile`, em vez de matar o convidado pelo sinal.

### Isolamento de rede do sandbox (`--no-network` e `--network=<modo>`)

O runtime suporta isolamento estrito de rede para proteger o hospedeiro e impedir comunicação não autorizada ou exfiltração de dados por executáveis convidados:
- `--no-network` ou `--network=none`: cria um namespace de rede privado (`CLONE_NEWNET` via user namespace desprivilegiado) sem nenhuma interface de rede ativa. Conexões de rede e criação de sockets falham com `ENETUNREACH` (`WSAENETUNREACH`).
- `--network=loopback`: cria um namespace de rede isolado ativando exclusivamente a interface local de loopback (`127.0.0.1`), permitindo IPC local enquanto bloqueia todo tráfego para a rede física externa.
- `--network=full`: mantém o acesso direto normal herdado da pilha de rede do hospedeiro (comportamento padrão).

Quando qualquer política não padrão estiver configurada e o trace estiver ativo, o evento `sandbox` é emitido:

```text
[tl][process][info] sandbox network="none"
```

Se a instalação do isolamento de rede falhar no filho, o evento `network-isolation-failed` é emitido no componente `process` com `category="internal-error"`, retornando código de saída `70` (`InternalError`).

O modo `--report` produz um relatório textual em stdout sem executar o entry
point. Cada import aparece com seu estado e, quando resolvido, com
`support=full`, `support=limited` ou `support=stub`. O campo
`result: supported` ou `result: unsupported` continua indicando somente se todos
os imports foram resolvidos; `runtime-support` resume a cobertura comportamental
declarada pelas exports resolvidas, ou `unresolved` quando a resolução falha.
Em todos os casos, `execution: not-attempted` e
`execution-result: not-attempted` deixam claro que o relatório não executa o
PE.

Quando a imagem PE é analisada, o relatório inclui inspeção aprofundada de segurança, empacotamento, framework e recursos:

- `mitigations: aslr=<enabled(high-entropy)|enabled|disabled> dep=<enabled|disabled> cfg=<enabled|disabled> seh=<present|no-seh> [appcontainer=yes] [force-integrity=yes]`:
  inspeciona o campo `DllCharacteristics` do cabeçalho opcional PE, reportando o estado das mitigações modernas do sistema operacional (ASLR com alta entropia, DEP/NX, Control Flow Guard, isolamento AppContainer e integridade forçada).
- `packer: detected (<nome>: <indicadores>)`:
  identifica compressores/obfuscadores de código conhecidos (UPX, VMProtect, Themida, ASPack, Enigma, MPRESS, PECompact) ou características anômalas de seções (permissões W+X ou seções executáveis descompactadas em memória com `raw_data_size=0`). Quando presente, emite recomendação para descompactar previamente a imagem a fim de evitar violações de W^X em runtime nativo.
- `toolchain: <MSVC CRT | MinGW-w64 | Legacy MSVC CRT>`:
  identifica o compilador e biblioteca de runtime CRT associada ao binário (ex.: Visual Studio 2015-2022 v14x, Visual Studio 2013/2012/2010, MinGW-w64 GCC ou o histórico `msvcrt.dll`).
- `runtime: .NET CLR (Managed code via ...)`:
  detecta binários gerenciados .NET através da importação do host `mscoree.dll` ou da presença do descritor COM/CLR na tabela de diretórios PE. Emite recomendação para uso com runtime `dotnet` ou Proton na ausência de código AOT nativo.
- `gui-framework: <framework1, framework2, ...>`:
  detecta bibliotecas de interface gráfica e toolkits utilizados (ex.: Qt 6, Qt 5, MFC, wxWidgets, Electron/Chromium Embedded Framework, WinUI 3).
- `anticheat: detected (<nome>: <dlls>)`:
  identifica módulos conhecidos de anticheat (EasyAntiCheat, BattlEye, Riot Vanguard, PunkBuster, Denuvo). Emite aviso/recomendação explícito informando que componentes ring-0 de proteção em nível de kernel não são suportados no runtime nativo.
- `services: detected driver/service installation APIs (advapi32.dll: <APIs>)`:
  identifica a presença de chamadas de instalação ou controle de serviços e drivers Windows (`CreateServiceW`, `OpenSCManagerW`, `StartServiceW`, etc.).

Quando a imagem PE possuir recursos internos (`.rsrc`), o relatório inclui:
- `identity: "<ProductName>" v<ProductVersion> (<CompanyName>)`:
  extrai os metadados de produto e versão do bloco `RT_VERSION` (`VS_VERSIONINFO` / `StringFileInfo`), identificando o nome do produto, versão textual/estruturada e empresa desenvolvedora.
- `resources: <N> types (<tipo>=<qtd>, ...)`: inventário estrutural de tipos
  Win32 embutidos (ex.: `dialog`, `icon`, `version`, `manifest`).
- `manifest: uac="..." dpi-aware="..." os-compat="..."`: extração segura do
  `RT_MANIFEST`, identificando privilégio de execução solicitado (`asInvoker`,
  `requireAdministrator`, etc.), percepção de DPI e versões de SO suportadas.
  Caso `uac="requireAdministrator"`, emite alerta explícito de necessidade de
  elevação.

Caso a aplicação importe ou use delay-import de bibliotecas de aceleração
gráfica 3D (`d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `d3d9.dll`, `vulkan-1.dll`,
`xinput1_4.dll`), o relatório emite uma recomendação direta para direcionar a
execução ao backend Proton:
`recommendation: requer aceleracao grafica 3D (...); configure profile.json com 'backend.kind: proton'`

### Saída estruturada em JSON (`--report --json`)

Quando combinado com a flag `--json` (`tradutorlinux --report --json <app.exe>`), o comando emite o relatório completo em formato JSON padronizado diretamente em `stdout`. Isso viabiliza o consumo automatizado pelo lançador gráfico Qt (`src/ui/`), ferramentas de CI e scripts de análise:

- Chaves de primeiro nível: `format`, `entry_point`, `image_base`, `size_of_image`, `sections_count`.
- Objetos estruturados: `mitigations` (ASLR/DEP/CFG/SEH/AppContainer), `packer` (is_packed, name, indicators), `toolchain` (compiler, .NET, frameworks GUI), `security` (anticheat e APIs de driver/serviços), `identity` (metadados do bloco `RT_VERSION`), `resources` (inventário por tipo), `manifest` (UAC, DPI, SOs suportados) e `imports` (módulos, contadores, status e suporte por símbolo).
- Lista de `recommendations`: array de strings com todas as recomendações aplicáveis (aceleração 3D, descompactação de packers, runtime .NET, restrições de anticheat de kernel).

Para evitar confundir análise com compatibilidade de aplicativo, use esta ordem
de evidência:

1. `result` mede somente a resolução estática de imports.
2. `execution` registra se o entry point foi executado (`not-attempted`,
   `passed` ou `failed`).
3. O catálogo só pode registrar `fluxo principal`, `uso diário` ou outro nível
   funcional quando houver um fluxo representativo executado, resultado
   observável e limitações publicadas.

Um `result: supported` sem execução correspondente significa apenas
`imports-resolved`; não deve ser apresentado como suporte funcional ao usuário.

### Diagnóstico de ambiente com o comando `doctor` (`tradutorlinux doctor [--json]`)

O subcomando `tradutorlinux doctor` verifica a prontidão do ambiente Linux hospedeiro para executar aplicativos Windows pelo TradutorLinux (tanto via runtime nativo quanto via backend Proton).

Ele inspeciona e relata o status de 6 categorias principais:
1. **Sistema e Kernel**: arquitetura do host (`x86_64`), versão do kernel Linux via `uname()`.
2. **Servidor Gráfico**: detecção de X11 (`$DISPLAY`), Wayland (`$WAYLAND_DISPLAY`) e disponibilidade de `Xvfb` no `$PATH` para testes headless.
3. **Aceleração 3D e GPU**: presença de nós de renderização DRM (`/dev/dri/renderD*`) e manifestos ICD de drivers Vulkan instalados (`/usr/share/vulkan/icd.d`, `/etc/vulkan/icd.d`).
4. **Backend Proton**: status da configuração de runtime (`root` configurada em `~/.config/tradutorlinux/proton.json` ou `TRADUTORLINUX_PROTON_ROOT`).
5. **Sandbox e Isolamento**: presença do utilitário Bubblewrap (`bwrap`) no `$PATH` e status de namespaces de usuário desprivilegiados (`/proc/sys/kernel/unprivileged_userns_clone`).
6. **Servidor de Áudio**: detecção de sockets de runtime PipeWire (`$XDG_RUNTIME_DIR/pipewire-0`) e PulseAudio (`$XDG_RUNTIME_DIR/pulse/native`).

Com a opção `--json` (`tradutorlinux doctor --json`), o comando emite um documento JSON estruturado contendo as chaves `system`, `display`, `graphics_3d`, `proton`, `sandbox`, `audio` e a lista `issues`, viabilizando diagnósticos automáticos integrados a scripts de instalação, CI e interface gráfica Qt.

### Backend Rust no relatório direto e no `app run` nativo — R21.3–R21.5

Quando o projeto é construído com `TL_BUILD_RUST=ON`, somente
`tradutorlinux [--trace] --report arquivo.exe` e `app run <id>` nativo sem
`--report` usam Rust para analisar a imagem principal do PE. O evento de
parsing identifica o backend:

```text
[tl][pe][info] image format="PE32+" arch="x86-64" entry="0x1000" image-base="0x140000000" size-of-image="0x5000" sections="4" backend="rust"
```

Se a análise Rust falhar, não há fallback para o parser C++:

```text
[tl][pe][error] parse-failed status="malformed" detail="assinatura DOS ausente (esperado MZ)" backend="rust" code="16" phase="2" input-offset="0" detail-value="26915"
```

`truncated`/`malformed` retornam `4`; `unsupported-architecture`,
`unsupported-format` e `unsupported-mechanism` retornam `5`. Falhas da ABI,
limites, buffer, wire inválido, panic ou falha interna retornam `70`.
`code`, `phase`, `input-offset` e `detail-value` são os campos estruturados
de `tl_pe_error_v1`; `detail` é apenas diagnóstico humano.

No `--report`, o caminho Rust termina antes de mapear, resolver imports para
execução ou executar o entry point. No `app run` nativo, o resultado é entregue
ao loader C++ depois da análise; as DLLs dependentes continuam sendo lidas pelo
parser C++ do `GuestModuleGraph`. `app run --report`, execução direta normal,
instalação, Proton e qualquer build com `TL_BUILD_RUST=OFF` emitem o trace C++
sem `backend="rust"` no evento PE da imagem.

Não há fallback silencioso. Se a análise Rust falhar no `app run`, nenhum evento
de imagem mapeada, resolução de imports para execução ou processo convidado é
emitido. O status é mapeado para exit `4` (`truncated`/`malformed`), `5`
(`unsupported-architecture`, `unsupported-format` ou `unsupported-mechanism`)
ou `70` (erro FFI, wire inválido, limite, panic, status inesperado ou falha
interna). O evento `parse-failed` mantém `code`, `phase`, `input-offset` e
`detail-value` para automação.

Essa seleção é centralizada: Rust é canônico somente nos dois caminhos
promovidos quando `TL_BUILD_RUST=ON`; o parser C++ continua sendo produção em
DLLs dependentes, Proton, instalação, `app run --report`, execução direta
normal e builds `TL_BUILD_RUST=OFF`. Nos caminhos promovidos, o C++ serve apenas
como oráculo diferencial de testes. O build OFF é uma variante C++ explícita,
sem link ou referência aos símbolos Rust.

Quando uma importação não pode ser resolvida, emite um evento `unresolved` com os campos `dll`, `symbol`, `status`, `detail` e `mechanism`:

```text
[tl][imports][error] unresolved dll="USER32.dll" symbol="MessageBoxA" status="unknown-dll" detail="módulo não registrado" mechanism="delay-import"
```

Os valores possíveis de `status` são:

| `status` | Significado |
|---|---|
| `unknown-dll` | DLL não registrada no runtime. |
| `unknown-symbol` | DLL conhecida, símbolo não exportado. |
| `unknown-ordinal` | DLL conhecida, ordinal não exportado. |
| `not-implemented` | Símbolo conhecido, sem implementação no runtime. |
| `unsupported-mechanism` | Mecanismo ainda não suportado (ex.: slot da IAT fora das seções). |

Quando qualquer importação falha, a resolução inteira falha e o processo não tem entry point executado; o runtime retorna `5` (`Unsupported`). Mesmo na falha, todas as entradas são reportadas para que o diagnóstico seja completo.

## Componente `install`

O comando `install` emite seus eventos neste componente, sempre em `stderr`.
Eles são a interface que o launcher usa para acompanhar a instalação; a saída
em `stdout` pertence exclusivamente ao programa convidado. Os estados são:

| Evento | Campos principais | Significado |
|---|---|---|
| `prepared` | `prefix`, `app-id`, `name` | Prefixo exclusivo preparado antes de iniciar um setup ou extrair um pacote. |
| `extracted` | `prefix`, `app-id`, `path` | Pacote MSIX/AppX foi extraído com segurança e `path` aponta para o PE declarado pelo manifesto. |
| `candidate` | `prefix`, `app-id`, `path` | PE32+ AMD64 novo ou alterado em `drive_c` após o setup. |
| `registered` | `prefix`, `app-id`, `path` | Executável escolhido e entrada salva no catálogo. |
| `pending` | `reason`, `prefix`, `app-id` | Setup terminou, mas o cadastro precisa de escolha ou não há candidato válido. |
| `failed` | `stage`, `prefix`, `app-id` | Entrada, `package-parse`, `package-extract`, `package-executable`, imports, preparação, setup, timeout/sinal ou persistência do catálogo falhou; o prefixo é preservado. |

`pending reason="selection-required"`, `pending reason="no-candidate"` e
`pending reason="invalid-app-exe"` retornam `6` (`InstallPending`). Um setup
que retorna código diferente de zero não cria entrada no catálogo; seu código
de saída é preservado e há um evento `failed stage="setup"`.

Para `.msix`/`.appx`, `install` não executa um instalador convidado: valida o
ZIP, lê `AppxManifest.xml`, extrai o conteúdo para `Program Files/<app-id>` e
registra o PE32+ x86-64 declarado. `failed stage="package-parse"` indica
manifesto/estrutura inválida, `package-extract` indica falha nas validações ou
na escrita segura e `package-executable` indica que o executável declarado não
é PE32+ x86-64. O caminho `--report` continua somente estrutural e não extrai.

### Análise Rust de MSIX/AppX — R22.2

Com `TL_BUILD_RUST=ON`, Rust é canônico no `--report` direto de um pacote e no
`install`. A seleção usa a extensão `.msix`, `.appx`, `.msixbundle` ou
`.appxbundle` antes de validar a assinatura ZIP, permitindo diagnosticar
arquivos truncados ou inválidos. `app run`, `app run --report`, execução direta
normal, Proton, DLLs dependentes e `TL_BUILD_RUST=OFF` continuam no caminho C++.

O caminho Rust emite um evento novo sem modificar a saída normal:

```text
[tl][cli][info] package-parse format="MSIX / AppX" backend="rust" status="success"
[tl][install][info] package-parse format="MSIX / AppX" backend="rust" status="success"
```

Em falhas, o mesmo evento inclui `code`, `phase`, `input-offset` e
`detail-value`. A fonte de automação continua sendo o status e esses campos; a
mensagem caller-owned é somente diagnóstico humano.

O mapeamento de falhas Rust é:

- `truncated` e `malformed`: `4` (`MalformedPe`);
- `unsupported-format` e `unsupported-mechanism`: `5` (`Unsupported`);
- argumento inválido, buffer, limite, wire inválido, panic ou falha interna:
  `70` (`InternalError`).

No `--report`, o resultado TLMS é decodificado e impresso sem mapeamento ou
execução. No `install`, Rust valida antes de C++ abrir o ZIP e extrair; o
extrator usa o executável principal validado por Rust. Falha ou divergência não
faz fallback, não inicia o PE e não cria entrada no catálogo. A validação do
PE32+ AMD64 extraído permanece C++.

O contrato rejeita deterministicamente bundles, ZIP64 multipartes, encryption,
métodos ZIP desconhecidos, links, traversal, NUL, colisões após normalizar
`\\` para `/`, DTD e entidades externas. ZIP64 de disco único, com EOCD,
localizador e extras `0x0001` válidos, é aceito dentro do limite agregado
descompactado de 512 MiB. Rust não acessa o filesystem e não valida o PE
interno do pacote.

### Análise Rust de perfis — R23.1–R23.2

Com `TL_BUILD_RUST=ON`, `load_profile` usa Rust como parser canônico depois de
o C++ confirmar a presença, regularidade, leitura e limite de
`profile.json`. Rust recebe somente os bytes do arquivo e o contexto esperado
de `app_id`, SHA-256 e versão; valida JSON, schemas 1/2/3, identidade, backend
e regras lexicais de caminhos. O C++ continua responsável por filesystem,
materialização, permissões e condições físicas.

As funções `tl_profile_parse_v1_size` e `tl_profile_parse_v1_fill` usam buffers
caller-owned. `fill` não modifica a saída quando a capacidade é insuficiente;
mensagens usam `error_required` incluindo o NUL. O erro estruturado TLPR tem
`code`, `phase`, `input-offset` e `detail-value`; ele é a fonte para automação,
enquanto a mensagem é diagnóstico humano. O decoder rejeita magic, versão,
offsets, strides, alinhamento, referências, reservados e limites inválidos.

O evento existente `compat-profile` permanece estável. Quando Rust é tentado,
ele acrescenta `backend="rust" parser-status="success"`; em rejeições,
acrescenta também `code`, `phase`, `input-offset` e `detail-value`. Perfil
ausente é detectado antes da chamada e não recebe campos Rust. Rejeição de
conteúdo (`malformed`, `unsupported-format`, `input-too-large` ou
`output-too-large`) preserva o fallback genérico; falha interna da ABI,
decoder, transporte TLPR, panic ou status inesperado retorna `InternalError`
sem fallback.

Depois de um parsing Rust bem-sucedido, uma falha física de fonte, symlink,
confinamento ou permissão invalida o perfil no C++ e não materializa arquivos.
O build `TL_BUILD_RUST=OFF` continua a variante C++ explícita e padrão, sem
referenciar os símbolos Rust e sem campos Rust no trace.

### Contrato, decoder e promoção Rust do catálogo — R24.1–R24.3

R24.1 implementa a ABI TLAC para analisar os bytes do `library.json` e R24.2
adiciona o decoder/adaptador diferencial. Em R24.3, com
`TL_BUILD_RUST=ON`, `AppCatalog::load_from_file` usa esse adaptador como
backend canônico para arquivos existentes; a escrita, o filesystem e os
demais fluxos continuam em C++.

O parser é estrito para o schema atual (`version=1`, `apps`), rejeita campos
desconhecidos ou repetidos, JSON incompleto, trailing comma, IDs inválidos ou
repetidos, tipos incorretos, escapes inválidos e limites excedidos. Strings são
preservadas como bytes e não exigem UTF-8. O Rust não acessa filesystem nem
faz validações físicas.

As funções `tl_app_catalog_parse_v1_size` e `fill` usam erro estruturado com
`code`, `phase`, `input-offset` e `detail-value`, além de mensagem caller-owned.
As chamadas são stateless; `fill` não escreve em capacidade insuficiente e
panics são convertidos em `internal`. R24.2 valida o TLAC com decoder C++
little-endian, sem casts de layout, rejeitando offsets, strides, referências,
padding, reservas, contagens e sobreposições inválidos. O resultado só é
publicado após a validação completa e o adaptador não chama `add_app`.

No ramo Rust, `load_from_file` limpa primeiro o catálogo, aplica o limite de
4 MiB antes de alocar, lê o arquivo em buffer caller-owned e publica apenas o
vetor completamente validado pelo TLAC. Conteúdo inválido, limites excedidos
ou identidade lexical inválida retornam `false` e deixam o catálogo vazio;
nenhum resultado é substituído pelo parser C++. Falhas de ABI, transporte,
wire, status inesperado, divergência de `size`/`fill` ou panic também retornam
`false` sem fallback. Arquivo ausente ou erro de abertura/leitura é detectado
antes da chamada Rust e não gera evento Rust.

Quando `--trace` solicita o componente `runtime`, o sucesso emite, por
exemplo:

`[tl][runtime][info] catalog-parse path="..." backend="rust" parser-status="success" apps="1"`

Rejeições de conteúdo/limite usam nível `warning`; falhas internas usam
`error`. Em ambos os casos o evento contém `backend="rust"`,
`parser-status`, `code`, `phase`, `input-offset`, `detail-value` e `detail`.
Sucesso também informa `apps`. Sem `--trace`, stderr e stdout normais não
recebem campos Rust. O build `TL_BUILD_RUST=OFF` segue emitindo o trace C++
existente, sem evento Rust ou campos Rust.
