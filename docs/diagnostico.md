# Diagnóstico e códigos de saída

## Saída de trace

O trace é habilitado por `--trace` (todos os canais) ou `--trace=canal1,canal2` (filtrado, inspirado em `WINEDEBUG`), vai exclusivamente para `stderr` e ocupa uma linha por evento:

```text
[tl][<componente>][<nível>] <evento> chave="valor"
```

Componentes iniciais: `cli`, `pe`, `loader`, `imports`, `runtime`, `process`,
`gui`, `crt` e `install` (ver `src/diagnostics/trace.cpp`).

Filtragem: `--trace=pe,loader` emite apenas `pe` e `loader`; canal desconhecido retorna `canal de trace desconhecido` e exit `2` (`Usage`). Sem filtro, todos os canais são emitidos; a filtragem é feita em `diagnostics::is_trace_enabled` antes de `write_trace`.

Níveis iniciais: `debug`, `info`, `warning` e `error`.

Valores sempre usam aspas duplas. Dentro deles, barra invertida, aspas, quebra de linha, retorno de carro e tabulação são escapados como `\\`, `\"`, `\n`, `\r` e `\t`. A forma é legível por humanos e estável para ferramentas simples de parsing.

Exemplo atual:

```text
[tl][cli][info] input path="tests/samples/generated/tl_hello.exe"
[tl][pe][info] image format="PE32+" arch="x86-64" entry="0x1000" image-base="0x140000000" size-of-image="0x4000" sections="3"
[tl][pe][info] section index="0" name=".text" virtual-address="0x1000" virtual-size="0x90" raw-pointer="0x400" raw-size="0x200" characteristics="0x60000020"
[tl][pe][info] import dll="KERNEL32.dll" symbols="ExitProcess,GetStdHandle,WriteFile"
[tl][pe][info] relocations blocks="0" entries="0"
[tl][loader][info] mapped preferred-base="0x140000000" base="0x140000000" delta="0x0" size="0x4000" at-preferred="sim" relocations-applied="0"
[tl][loader][info] region name=".text" rva="0x1000" size="0x200" permissions="r-x"
[tl][loader][info] region name=".rdata" rva="0x2000" size="0x200" permissions="r--"
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="ExitProcess" address="0x... "
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="GetStdHandle" address="0x..."
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="WriteFile" address="0x..."
[tl][loader][info] unmap base="0x140000000"
```

## Eventos do componente `imports`

O resolvedor emite um evento `resolved` por importação resolvida (`dll`,
`symbol` — nome ou `ordinal(N)` —, `address` e `mechanism`). O mecanismo é
`import` para a tabela estática e `delay-import` para a tabela atrasada:

```text
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="WriteFile" address="0x..." mechanism="import" provider="builtin"
```

As chamadas de console são registradas pelo componente `runtime` com os
tamanhos e resultados relevantes. O componente `process` registra o retorno
do código convidado:

```text
[tl][process][info] exit exit-code="0" explicit="sim"
```

Durante `app run`, o runtime também consulta o perfil opcional do aplicativo
no prefixo. O evento `compat-profile` informa `missing`, `loaded` ou `invalid`;
um perfil ausente, inválido ou incompatível gera aviso e não muda o exit code
do convidado:

```text
[tl][runtime][info] compat-profile status="loaded" prefix="..." app-id="fixture" files="1" detail=""
[tl][runtime][warning] compat-profile status="invalid" prefix="..." app-id="fixture" files="0" detail="app_id do perfil não corresponde ao aplicativo"
[tl][runtime][info] compat-files status="applied" prefix="..." app-id="fixture" files="1" detail=""
[tl][runtime][info] compat-file status="copied" source="config.dat" target="C:\\Program Files\\Fixture\\config.dat"
[tl][runtime][info] compat-files-cleanup status="cleaned" prefix="..." app-id="fixture" removed-files="1" removed-directories="1" retained="0"
```

O contrato do arquivo está em
[perfis de compatibilidade](arquitetura/perfis-compatibilidade.md). Um perfil
válido expõe os arquivos por cópia temporária antes do entry point. Colisão,
symlink, falha de permissão ou outro erro de materialização gera
`compat-files status="rejected"`, aviso e fallback genérico. Falha ao
desfazer uma cópia parcial gera `status="rollback-failed"` e interrompe a
execução por segurança. A limpeza ocorre depois do processo convidado; um
arquivo substituído ou um diretório que ficou não vazio é preservado.
`compat/` não é exposta automaticamente ao convidado.

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

O modo `--report` produz um relatório textual em stdout sem executar o entry
point. Cada import aparece com seu estado e, quando resolvido, com
`support=full`, `support=limited` ou `support=stub`. O campo
`result: supported` ou `result: unsupported` continua indicando somente se todos
os imports foram resolvidos; `runtime-support` resume a cobertura comportamental
declarada pelas exports resolvidas, ou `unresolved` quando a resolução falha.
Em todos os casos, `execution: not-attempted` e
`execution-result: not-attempted` deixam claro que o relatório não executa o
PE.

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

## Eventos de ambiente, FLS e locale

O componente `runtime` registra `environment`, `fls` e `locale` para o núcleo
determinístico das Fases 13.6 e 13.7. Eles ficam em `stderr` e nunca misturam a
saída do convidado:

```text
[tl][runtime][info] environment operation="set" name="APPDATA" action="define" status="success"
[tl][runtime][info] fls operation="callback" detail="thread-exit" thread="2" status="success"
[tl][runtime][info] locale operation="cpinfo" code-page="437" status="success" max-char-size="1"
```

`environment` identifica alteração, expansão ou criação de bloco UTF-16;
`fls` identifica alocação, valor, callback e liberação; `locale` identifica
ACP/OEMCP, `CPINFO`, consulta de informação (inclusive `info-ex`), validação,
enumeração, tipo de caractere, formatação de data/hora e mapeamento. Erros de
ponteiro, índice, callback, flags ou buffer continuam a usar o retorno Win32 e
`GetLastError` sem executar código convidado inesperado.

## Eventos de processo e console

A Fase 13.8 acrescenta os eventos `process-context` e `console` no componente
`runtime`. O primeiro registra startup, handles padrão, tipo de arquivo,
diretório do sistema, recursos do processador, codificação de ponteiro e
inicialização de SList; o segundo registra as quantidades UTF-16 lidas ou
escritas por `ReadConsoleW`/`WriteConsoleW`:

```text
[tl][runtime][info] process-context operation="startup-info" detail="wide" thread="1" status="success"
[tl][runtime][info] console operation="write-wide" detail="18" thread="1" status="success"
```

Erros continuam expressos pelo retorno Win32 e `GetLastError`; a saída do
convidado permanece em stdout/stderr conforme o handle solicitado.

## Eventos de arquivos

A Fase 13.9 usa `filesystem` no componente `runtime` para enumeração,
alteração de atributos, metadados por handle e exclusão. O trace nunca inclui
o caminho Linux do hospedeiro:

```text
[tl][runtime][info] filesystem operation="find-first-ex" status="success" detail="basic" scope="prefix"
[tl][runtime][info] filesystem operation="set-information" status="success" detail="delete-on-close" scope="prefix"
```

O shell visual do 7-Zip registra a cópia opt-in com `SevenZipOperation`. O
evento informa a operação, o estado e somente os nomes dos arquivos; nunca
expõe o caminho Linux do hospedeiro:

```text
[tl][runtime][info] SevenZipOperation operation="copy" status="success" source-name="selected.txt" destination-name="selected.txt"
```

`destination-outside-prefix`, `destination-not-directory`, `path-outside-root`,
`no-selection` e `failed` são estados controlados; o comando só escreve quando
`TL_7ZFM_COPY_DESTINATION` aponta para um diretório existente dentro da raiz
visual e não sobrescreve um arquivo já existente.

## Eventos de segurança

A Fase 13.10 usa o evento `security` no componente `runtime` para token,
descritor e merge de ACL. Ele nunca inclui caminho Linux, SID do host ou outra
identidade do hospedeiro:

```text
[tl][runtime][info] security operation="token-information" status="success" detail="user" scope="prefix"
[tl][runtime][info] security operation="set-entries" status="success" detail="dacl" scope="prefix"
[tl][runtime][info] security operation="named-set" status="success" detail="dacl" scope="prefix"
```

Falhas de ponteiro, SID, ACL, objeto fora de `C:\` ou recurso fora do
subconjunto seguem o retorno/`GetLastError` Win32. SACL, herança complexa e
trustee não-SID retornam `ERROR_NOT_SUPPORTED`; nenhum desses eventos aplica
permissões Linux.

## Eventos WinINet

O subconjunto HTTPS de loopback da Fase 13.12 usa o evento `wininet` no
componente `runtime`. Ele registra somente operação, estado, porta, bytes
quando aplicável, esquema, destino e escopo:

```text
[tl][runtime][info] wininet operation="open" status="success" scheme="https" scope="prefix" destination="loopback" port="0"
[tl][runtime][info] wininet operation="send" status="success" scheme="https" scope="prefix" destination="loopback" bytes="17"
```

O trace nunca inclui URL completa, cabeçalhos, corpo, certificado ou caminho do
CA. Falha de biblioteca, CA, certificado ou transporte usa o mesmo evento com
estado controlado; o resultado detalhado continua em retorno WinINet e
`GetLastError`.

## Eventos OLE streams

`CreateStreamOnHGlobal` e os métodos do `IStream` em memória usam o componente
`runtime` e o evento `ole-stream`. O trace registra somente a operação, o
estado e o mecanismo de armazenamento:

```text
[tl][runtime][info] ole-stream operation="write" status="success" mechanism="memory"
```

O subconjunto não inclui conteúdo, endereços ou caminhos do host. Operações
fora do contrato (`clone`, `CopyTo` e lock de região) retornam HRESULT
controlado e não são apresentadas como compatíveis.

## Eventos WinTrust

`WinVerifyTrust` emite o evento `wintrust` no componente `runtime`, registrando
somente a operação, o resultado, a política e o mecanismo:

```text
[tl][runtime][info] wintrust operation="verify" status="success" policy="explicit-chain" mechanism="openssl"
```

Falhas de política ou de cadeia usam `status="invalid"` ou
`status="untrusted"`. O trace não inclui certificados, DER, nomes ou caminhos;
`WTD_CHOICE_FILE`, revogação e a loja do sistema não são aceitos.

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

## Categorias de falha

Eventos de erro podem incluir o campo `category`:

| Categoria | Significado |
|---|---|
| `exit-process` | O convidado encerrou explicitamente com `ExitProcess`. |
| `unsupported` | Dependência ou operação fora do subconjunto suportado. |
| `invalid-image` | PE malformado ou imagem não mapeável. |
| `guest-memory` | Ponteiro ou faixa fornecida pelo convidado não é válida. |
| `linux-error` | Operação Linux falhou; pode incluir `operation`, `errno` e `win32-error`. |
| `guest-signal` | Término do convidado por sinal Linux. |
| `guest-timeout` | O convidado não terminou dentro do limite de `--timeout` e foi morto pelo hospedeiro. |
| `guest-resource-limit` | O convidado excedeu um limite configurado de CPU ou memória. |
| `internal-error` | Falha inesperada do runtime. |

Exemplo de falha Linux em uma API:

```text
[tl][runtime][error] linux-failure category="linux-error" symbol="CreateFileA" operation="open" errno="2" win32-error="2"
```

O guest executa em processo filho isolado. O hospedeiro não instala um handler
geral de sinais C++; o pai observa o status do filho com `waitpid` e publica
`guest-signal` quando o entry point termina por sinal.

### Evento `terminated category="guest-signal"`

Quando o convidado morre por sinal fatal, o filho captura o endereço reportado
pelo kernel (`si_addr`) em um handler mínimo async-signal-safe e o entrega ao
pai por um pipe dedicado antes de reentregar o sinal com disposição padrão —
o status observado pelo `waitpid` não muda. O evento ganha então:

| Campo | Condição | Significado |
|---|---|---|
| `fault-address` | Sempre que o filho alcançou o handler | Endereço virtual da falta em hex (`si_addr`). Para sinais sem endereço associado (ex.: `SIGABRT`) pode ser `0x0`. |
| `rva` | Endereço dentro da imagem mapeada | `fault-address − base`, em hex. |
| `section` | RVA cai dentro de uma região nomeada | Nome da seção PE que contém o RVA. |
| `nearest-import` | Existe slot de IAT resolvido ≤ RVA | `DLL!símbolo` do slot de IAT mais próximo abaixo da falta — a melhor pista da importação em jogo. |

Campos são omitidos quando a condição não se aplica: desreferência de nulo
(`fault-address="0x0"`) está fora da imagem e não produz `rva`/`section`;
término por `SIGKILL` externo (ex.: timeout) não passa pelo handler e não
produz `fault-address`.

Exemplo com contexto completo (falta dentro de `.text`):

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória" fault-address="0x140001050" rva="0x1050" section=".text" nearest-import="KERNEL32.dll!ExitProcess"
```

Exemplo real da fixture `tl_crash.exe` (desreferência de nulo):

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória" fault-address="0x0"
```

## Componente `gui`

O protótipo X11 usa um subconjunto de `USER32.dll`: `MessageBoxA` (somente com
`hWnd == NULL` e `type == 0`) e as APIs de janela/eventos documentadas em
[`gui-x11.md`](arquitetura/gui-x11.md). Falha ao abrir o display, fechar a janela
sem confirmação ou criar uma janela retorna o comportamento documentado ao
programa convidado; a chamada não lança exceções nem compromete o diagnóstico
do loader.

As APIs de janela emitem eventos do componente `runtime`:

```text
[tl][runtime][info] RegisterClassExA symbol="RegisterClassExA" class="tlwin" atom="1" status="success"
[tl][runtime][info] CreateWindowExA symbol="CreateWindowExA" class="tlwin" window="Ola do Windows no Linux!" status="success"
[tl][runtime][info] TranslateMessage symbol="TranslateMessage" message="WM_CHAR" wparam="113" status="translated"
[tl][runtime][info] SetTimer symbol="SetTimer" id="1" elapsed-ms="200" status="success"
[tl][runtime][info] GetMessageA symbol="GetMessageA" message="WM_TIMER" id="1" status="delivered"
[tl][runtime][info] KillTimer symbol="KillTimer" id="1" status="success" result="killed"
[tl][runtime][info] GetStockObject symbol="GetStockObject" object="0" status="success" mechanism="token"
[tl][runtime][info] BeginPaint symbol="BeginPaint" status="success" mechanism="hdc=hwnd" result="painting"
[tl][runtime][info] TextOut symbol="TextOut" x="10" y="10" length="17"
[tl][runtime][info] EndPaint symbol="EndPaint" status="success" result="painted" mechanism="hdc=hwnd"
[tl][runtime][info] GetMessageA symbol="GetMessageA" message="WM_QUIT" exit-code="0" result="quit"
```

`GetMessageA` registra somente eventos notáveis: o fim do loop (`WM_QUIT`),
confirmando que `PostQuitMessage` foi acionado, e cada timer expirado entregue
(`WM_TIMER` com `id` e `status` `delivered`). As mensagens ordinárias não são
registradas para não poluir o trace. `TranslateMessage` registra a conversão de
um `WM_KEYDOWN` em `WM_CHAR` com o `wparam` (código do caractere) e `status`
(`translated` quando houve conversão; o evento só é emitido nesse caso).

As APIs do GDI mínimo emitem eventos do mesmo componente: `SetTimer`/`KillTimer`
confirmam criação e remoção de timers; `GetStockObject` registra o objeto e o
mecanismo `token`; `BeginPaint`/`EndPaint` registram o par de pintura (o `HDC`
é o próprio `HWND`); `TextOut` registra as coordenadas e o comprimento do texto
desenhado.

## Componente `crt`

As funções da fronteira do `msvcrt.dll` mínimo emitem eventos do componente
`crt` no nível `info` para o startup e o término, e no nível `error` para
falhas que encerram o convidado:

```text
[tl][crt][info] getmainargs argc="2" argv0="xxd.exe"
[tl][crt][error] amsg-exit code="1"
[tl][crt][error] abort
[tl][crt][info] seh-handler disposition="4"
[tl][crt][info] exit code="0"
```

As demais APIs de CRT não registram evento (são chamadas em volume e o detalhe
relevante está na saída do convidado). O `__getmainargs` reporta o `argc` final
e o `argv[0]`, que são os argumentos do convidado encaminhados pelo CLI depois
do executável.

As APIs de `KERNEL32.dll` do subconjunto de console/CRT (por exemplo
`VirtualQuery`, `VirtualProtect`, `MultiByteToWideChar`, `GetConsoleMode`,
`Sleep`) emitem eventos do componente `runtime` com os parâmetros relevantes:

```text
[tl][runtime][info] VirtualQuery symbol="VirtualQuery" address="1400080000" region-size="4096" status="success"
```

## Processo convidado: TEB e segmento GS

O `__mingw_CRTStartup` (crt2.o) lê o TEB x64 via `%gs:[0x30]` para obter o
`StackBase` (offset `0x8`) e marcar a inicialização. Em Linux x86-64 o segmento
`GS` é livre, então a fronteira aloca um TEB de uma página (`mmap` anônimo),
aponta o `GS` para ele com `arch_prctl(ARCH_SET_GS)` imediatamente antes de
chamar o entry point do convidado e restaura o `GS` do hospedeiro (e libera o
TEB) no retorno — tanto na saída normal quanto no `longjmp` do `ExitProcess`
capturado. Campos preenchidos: `Self`, `StackBase` (= topo da pilha convidada)
e `StackLimit` (= `StackBase - kGuestStackSize`). Um convidado que não lê o
TEB (fixtures sem CRT) não é afetado.

## Eventos do componente `pe`

O leitor emite um evento `image` com os campos `format`, `arch`, `entry`, `image-base`, `size-of-image` e `sections`, seguido de um evento `section` por seção (`index`, `name`, `virtual-address`, `virtual-size`, `raw-pointer`, `raw-size`, `characteristics`), um evento `import` por DLL estática, um evento `delay-import` por DLL atrasada (ambos com `dll`, `symbols`), `unwind` (`functions`, `v1`, `v2`, `epilogs`, `extended-set-fpreg`, `handlers`, `chained`) e `relocations` (`blocks`, `entries`).

Em nível `debug`, cada bloco de base relocation é registrado com `page-rva` e `entries`.

Exemplo de metadados de desempilhamento aceitos:

```text
[tl][pe][info] unwind functions="2" v1="1" v2="1" epilogs="1" extended-set-fpreg="0" handlers="0" chained="0"
```

Falhas de argumento ou de pilha nas APIs `Rtl*` são registradas no componente
`runtime` como `api-failure`, com `symbol`, `operation="unwind"` e `detail`.
Se o `ControlPc` cai em um epílogo V2, o mesmo diagnóstico é emitido e
`RtlVirtualUnwind` preserva contexto e parâmetros de saída, sem acessar a
pilha nem interpretar instruções do epílogo.

## Eventos SEH

O despachante de exceções explícitas usa eventos `runtime` com
`mechanism="x64-seh"`. Os estados são `raised`, `veh`, `frame`, `handler`,
`unwind`, `continued` e `failed`; o campo `code` traz o código da exceção e
`detail` identifica a etapa. Por exemplo:

```text
[tl][runtime][info] seh state="raised" code="3762438722" detail="RaiseException" mechanism="x64-seh"
[tl][runtime][info] seh state="unwind" code="3762438722" detail="target" mechanism="x64-seh"
```

Uma falha SEH controlada não executa o entry point seguinte nem código fora da
imagem; o convidado termina com o código da exceção. Exceções C++, `__finally`,
sinais Linux e epílogos V2 não são traduzidos por esse mecanismo.

Quando o arquivo não é um PE32+ aceitável, o leitor emite:

```text
[tl][pe][error] parse-failed status="truncated" detail="arquivo menor que o cabeçalho DOS (64 bytes)"
```

Os valores possíveis de `status` são `truncated`, `malformed`, `unsupported-architecture`, `unsupported-format` e `unsupported-mechanism`. O campo `detail` informa a condição específica rejeitada. Um arquivo de entrada regular que não seja PE válido retorna o código `4` (`MalformedPe`); o código `5` (`Unsupported`) fica reservado para PE válido incompatível ou mecanismo válido ainda não suportado, como delay-import fora do formato RVA adotado.

O subsistema de diálogos acrescenta eventos sem caminhos do hospedeiro:

```text
[tl][runtime][info] DialogBoxParamW symbol="DialogBoxParamW" template="101" controls="4" status="created"
[tl][runtime][info] IsDialogMessageW symbol="IsDialogMessageW" action="tab" status="handled" control="100"
[tl][runtime][info] IsDialogMessageW symbol="IsDialogMessageW" action="enter" status="handled" control="1"
[tl][runtime][info] EndDialog symbol="EndDialog" result="42" status="success" modal="closed"
[tl][runtime][info] DialogBoxParamW symbol="DialogBoxParamW" result="42" status="returned" modal="complete"
```

Os campos identificam somente o ordinal do template, a contagem lógica, o
comando e o resultado modal. Templates rejeitados, ponteiros inválidos,
callbacks fora da imagem convidada e tentativas aninhadas retornam erro Win32
controlado e não expõem a identidade ou os caminhos Linux.

## Eventos do componente `loader`

O mapeador da Fase 2 emite um evento `mapped` com os campos `preferred-base`, `base`, `delta`, `size`, `at-preferred` (`sim` ou `não`) e `relocations-applied`, seguido de um evento `region` por região mapeada (`name`, `rva`, `size`, `permissions` em `r-x`, `r--`, `rw-` ou `---`) e um evento `unmap` (`base`) ao liberar a imagem.

Quando o mapeamento falha:

```text
[tl][loader][error] map-failed status="invalid-image" detail="seções se sobrepõem na imagem"
```

Os valores possíveis de `status` são `invalid-image` e `out-of-memory`. Falhas de imagem malformada retornam `4` (`MalformedPe`); falta de memória retorna `70` (`InternalError`).

Quando a imagem é mapeada fora do endereço preferencial e não possui diretório de relocations, o runtime registra o aviso honesto:

```text
[tl][loader][warning] cannot-relocate reason="imagem sem diretório de relocations" delta="0x... "
```

## Códigos de saída do host

| Código | Nome | Significado |
|---:|---|---|
| 0 | `Success` | A operação solicitada terminou corretamente. |
| 2 | `Usage` | Argumentos inválidos, ausentes ou incompatíveis. |
| 3 | `InputUnavailable` | O arquivo informado não existe, não é regular ou não pode ser acessado. |
| 4 | `MalformedPe` | O arquivo é reconhecido como PE malformado ou truncado. |
| 5 | `Unsupported` | PE válido de arquitetura ou formato ainda não suportado (ex.: PE32/x86), ou etapa futura do runtime não disponível. |
| 6 | `InstallPending` | O setup terminou, mas o runtime não pôde escolher com segurança um executável final; o prefixo é preservado e nada é cadastrado. |
| 70 | `InternalError` | Erro interno inesperado do runtime. |
| 71 | `GuestFault` | O programa convidado terminou por um sinal Linux (`guest-signal`). |
| 72 | `GuestTimeout` | O programa convidado não terminou dentro do limite informado em `--timeout` e foi morto pelo hospedeiro (`guest-timeout`). |
| 73 | `GuestResourceLimit` | O programa convidado excedeu um limite de CPU ou memória configurado (`guest-resource-limit`). |

Na Fase 1, um arquivo regular que não é PE válido retorna `4`, e um PE válido porém incompatível (arquitetura ou formato não suportado) retorna `5`. A partir da Fase 2, uma imagem válida porém não mapeável por inconsistência estrutural retorna `4`, e uma falha de mapeamento por memória insuficiente retorna `70`. A partir da Fase 3, um PE válido com dependências não suportadas (DLL, símbolo, ordinal ou mecanismo desconhecidos) também retorna `5`, com diagnóstico completo no trace e o entry point nunca executado. Na Fase 4, `ExitProcess` gera `[tl][runtime][info]` com o código bruto e `[tl][process][info] exit`; esse código é propagado como status do processo Linux. Com o isolamento em processo filho, o convidado que termina por sinal (`SIGSEGV`, `SIGILL`, `SIGBUS`, etc.) não derruba o hospedeiro: o pai observa o sinal via `waitpid`, emite `terminated category="guest-signal"` e retorna `71`.
