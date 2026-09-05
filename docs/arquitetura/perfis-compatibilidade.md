# Perfis de compatibilidade por aplicativo

## Objetivo

Cada aplicativo cadastrado pode ter arquivos auxiliares do TradutorLinux no
próprio prefixo, sem misturá-los aos arquivos reais do convidado e sem alterar
o comportamento de outro aplicativo.

```text
<prefixo>/
├── drive_c/       # arquivos reais visíveis como C:\
└── compat/
    ├── profile.json
    ├── files/     # fontes dos arquivos auxiliares
    └── dlls/      # DLLs PE32+ explicitamente declaradas pelo perfil
```

`compat/` não é um drive Windows e não fica visível automaticamente ao
convidado. Nesta versão, o perfil é colocado manualmente no prefixo. A
exposição dos arquivos para caminhos de `drive_c` está definida na B14.3.

## Formato v1

O arquivo `compat/profile.json` é um objeto JSON UTF-8 com os campos abaixo:

```json
{
  "schema": 1,
  "app_id": "meu-aplicativo",
  "app_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "app_version": "1.2.3",
  "files": [
    {
      "source": "config.dat",
      "target": "C:\\Program Files\\Meu Aplicativo\\config.dat"
    }
  ]
}
```

`schema` e `app_id` são obrigatórios. `app_sha256` e `app_version` são
opcionais; quando presentes, precisam corresponder aos dados registrados para
o executável no catálogo. O `source` é relativo a `compat/files/`, usa `/` e
não pode conter `.` ou `..`. O `target` é um arquivo dentro de `C:\`, que o
runtime valida como pertencente a `drive_c`.

A v1 aceita somente esses campos. Não aceita regras de API, comandos, scripts,
DLLs arbitrárias ou outros mecanismos executáveis.

## Formato v2 e extensões de DLL

A v2 mantém todos os campos da v1 e acrescenta `dlls`. Perfis v1 continuam
válidos e não carregam DLLs personalizadas:

```json
{
  "schema": 2,
  "app_id": "meu-aplicativo",
  "files": [],
  "dlls": [
    {
      "module": "compat.dll",
      "source": "compat.dll"
    }
  ]
}
```

Cada `module` é um nome lógico normalizado sem distinção entre maiúsculas e
minúsculas; a extensão `.dll` é acrescentada quando ausente. Cada `source` é
relativo a `compat/dlls/`, sem traversal, symlink ou arquivo não regular. Não
há descoberta automática: uma DLL só pode ser escolhida se houver uma entrada
explícita no manifesto. A fonte permanece nessa área e nunca é copiada para
`drive_c`.

Durante `app run`, a resolução usa esta precedência por módulo e por export:

1. DLL PE32+ AMD64 declarada no perfil;
2. DLL PE32+ encontrada no `drive_c` do mesmo prefixo;
3. implementação genérica interna do runtime.

Uma DLL personalizada pode fornecer apenas parte dos exports. Exports ausentes
continuam procurando o provider seguinte. Um provider de perfil ausente,
inválido, com imports não resolvidos, ciclo ou attach rejeitado é descartado
inteiro antes de executar código dependente, e o provider seguinte é usado
quando existir. Falha produzida pelo código da DLL depois do attach não recebe
fallback silencioso e é tratada como falha do convidado.

O loader mantém um grafo por execução, sem estado compartilhado entre prefixos.
Ele mapeia DLLs PE32+ AMD64, aplica relocations, valida exports por nome,
ordinal e forwarder, resolve imports estáticos e delay imports com política
eager e controla `LoadLibrary`, `GetProcAddress`, `FreeLibrary`, referências,
TLS callbacks e `DllMain`. A ordem de attach percorre dependências antes do
módulo dependente; detach e unload seguem a ordem inversa. DLLs de perfil são
código executável com os privilégios do runtime: não existe sandbox, assinatura
ou verificação de hash nesta versão, e bibliotecas Linux, scripts e campos de
regras declarativas continuam proibidos.

## Formato v3 e backend Proton

A v3 mantém os campos de arquivos e DLLs da v2 e acrescenta a seleção explícita
de backend. A ausência de `backend` equivale a `native`; `auto` não faz parte do
contrato inicial:

```json
{
  "schema": 3,
  "app_id": "meu-aplicativo",
  "files": [],
  "dlls": [],
  "backend": {
    "kind": "proton",
    "min_version": "11.0"
  }
}
```

`backend.kind` aceita somente `native` ou `proton`. `min_version` é opcional e
usa comparação numérica de versão; ele só é válido para `proton`. A seleção é
consultada por `app run` de aplicativo cadastrado. Execução direta e instalação
continuam usando o runtime próprio.

Quando `proton` é selecionado, `files[]` continua podendo ser materializado no
prefixo Proton. `dlls[]` continua sendo uma extensão do loader próprio e não é
copiada nem injetada no Proton; a combinação será rejeitada antes da execução,
com diagnóstico explícito. Um Proton solicitado que não esteja disponível ou
não passe pela validação também falha com `Unsupported`, sem fallback silencioso
para `native`.

O caminho da instalação fica fora do perfil, em
`$XDG_CONFIG_HOME/tradutorlinux/backends.json` ou
`~/.config/tradutorlinux/backends.json`:

```json
{
  "schema": 1,
  "proton": {
    "root": "/caminho/para/Proton",
    "sha256": "opcional"
  }
}
```

`TL_PROTON_ROOT` é uma substituição temporária para testes e diagnóstico. Não
há download automático, descoberta do Steam ou caminho implícito. A validação
exige o launcher `proton`, o arquivo `version`, Wine ELF x86-64 em
`files/bin/wine` e `files/bin/wineserver`, `files/share/wine/wine.inf` e o
prefixo padrão da distribuição; o hash opcional cobre um inventário
determinístico da instalação.

### Execução e staging do Proton

Quando a seleção é `proton`, o runtime mantém uma árvore persistente e separada
por aplicativo:

```text
<prefixo-do-aplicativo>/proton/
├── compatdata/pfx/drive_c/
├── client/
└── application-manifest.json
```

O executável cadastrado precisa estar dentro do `drive_c` nativo. O runtime
estagia o diretório do aplicativo no mesmo caminho Windows dentro de
`compatdata/pfx/drive_c`; somente arquivos regulares e diretórios são aceitos,
e symlinks são rejeitados. Arquivos novos são copiados com permissões `0644` e
diretórios novos com `0755`. Um arquivo já existente só é reutilizado quando o
conteúdo é idêntico; se foi alterado no prefixo Proton, a sincronização falha
sem sobrescrevê-lo. O manifesto registra o aplicativo, a versão do Proton e
os hashes das fontes. O `drive_c` nativo nunca é alterado pela sincronização.

Os `files[]` do perfil são materializados temporariamente no `drive_c` do
prefixo Proton antes do launcher e removidos após o processo. A limpeza usa
identidade POSIX para não apagar uma substituição feita pelo convidado. A área
`compat/` do prefixo nativo não é copiada nem fica visível dentro do Proton.
O launcher recebe `proton runinprefix <executável-estagiado>` com
`STEAM_COMPAT_DATA_PATH`, `WINEPREFIX`, `STEAM_COMPAT_CLIENT_INSTALL_PATH` e
`STEAM_COMPAT_INSTALL_PATH` apontando para a árvore isolada. O stdout é
herdado sem transformação; o stderr recebe o contexto `[tl][proton]`.

O piloto `integration_proton_backend` usa `tl_proton_probe.exe` e um launcher
mockado para reproduzir argv, ambiente, staging, limpeza, exit code e rejeição
sem fallback quando o Proton é inválido. Isso valida o adaptador, mas não
declara suporte a uma instalação Proton real nem ao Roblox.

O primeiro alvo gráfico controlado é `tl_graphics_probe.exe`. A integração
opcional `integration_proton_graphics`, registrada quando
`TL_PROTON_ROOT` aponta para uma instalação real, executa sob Xvfb e valida
janela X11, criação de dispositivo D3D11, swap chain, render target e
`Present`, com stdout preservado e exit code `0`. Essa evidência cobre somente
D3D11→DXVK/Vulkan em X11; VKD3D-Proton/D3D12, áudio, entrada e jogos ainda
dependem de alvos reproduzíveis próprios.

O slice D3D12 é exercido separadamente por `tl_d3d12_probe.exe` e
`integration_proton_d3d12`. A fixture cria dispositivo, fila, allocator,
command list e fence, cria uma swapchain flip de dois buffers para uma janela
e confirma `Present` sob Proton Experimental. Isso valida um caminho
controlado D3D12→VKD3D-Proton/Vulkan, sem declarar suporte geral a D3D12,
áudio, entrada ou jogos.

A entrada é exercida separadamente por `tl_input_probe.exe`,
`proton_input_driver` e `integration_proton_input`. O driver usa Xvfb e
XTest para localizar, mapear e focar a janela Proton; movimento, botão
esquerdo e `q` são convertidos em mensagens Win32 e a fixture só termina
após validar a sequência completa. A prova cobre apenas essa entrada de
janela controlada, não raw input, gamepad/XInput ou jogos.

O áudio é exercido separadamente por `tl_audio_probe.exe` e
`integration_proton_audio`. O perfil continua selecionando somente o backend;
o Proton fornece `XAudio2_8.dll` dentro do prefixo isolado. A fixture valida o
engine, as vozes e o ciclo básico de um buffer PCM, mas não transforma esse
slice em suporte geral a áudio, codecs, dispositivos ou multimídia.

## Auditoria do 7-Zip — sem regra específica

O 7-Zip 24.08 foi auditado como alvo real após a implementação da B14.3. Não
foi encontrada uma necessidade reproduzível de comportamento adicional no
perfil: os arquivos auxiliares cobertos pela B14.3 são suficientes para a
necessidade identificada. Por isso, a v1 não possui campo `rules` e a B14.4
não adiciona regras condicionais para o 7-Zip. A extensão de DLL da B14.4 é um
mecanismo geral de perfil; ela não altera o tratamento específico do 7-Zip.

O tratamento da classe `7-Zip::FM` continua pertencendo ao shell GUI
experimental, separado dos perfis. `TL_7ZFM_COPY_DESTINATION` é um hook de
teste e não uma configuração de perfil. Uma futura regra específica exigirá um
alvo, comportamento, justificativa, precedência, isolamento, diagnóstico,
fixture e regressão reproduzíveis.

## Fallback e diagnóstico

`app run` consulta o perfil do aplicativo cadastrado. Um perfil ausente,
inválido ou incompatível não impede a execução: o runtime emite um aviso em
`stderr`, registra o evento `compat-profile` no trace quando habilitado e
preserva o exit code produzido pelo convidado.

Um perfil é inválido quando há JSON malformado, schema desconhecido, campo
desconhecido ou repetido, identidade incompatível, hash/versão divergente,
arquivo de origem ausente, caminho fora de `compat/files/` ou destino fora de
`drive_c`. O perfil inteiro é ignorado; não há aplicação parcial.

Exemplos de estados:

- **Ausente:** não existe `compat/profile.json`; segue o comportamento genérico.
- **Válido:** todos os campos e arquivos declarados passam pela validação.
- **Inválido:** por exemplo, um campo não reconhecido ou uma origem ausente.
- **Incompatível:** `app_id`, SHA-256 ou versão não correspondem ao catálogo.

A ausência ou a rejeição do perfil não altera o resultado normal do programa.
O arquivo `compat/` continua sendo apenas uma área de dados; a materialização
ou o mapeamento de `files/` é descrito abaixo.

## Exposição controlada na B14.3

Durante `app run`, somente um perfil carregado e compatível pode expor
arquivos. Antes da execução do entry point, o runtime faz um pré-voo de todos
os mapeamentos. A origem precisa continuar sendo um arquivo regular dentro de
`compat/files/`, sem symlink. O destino precisa continuar dentro de `drive_c`;
destinos existentes, symlinks e diretórios-pai que não sejam diretórios
regulares rejeitam o perfil inteiro.

Para cada mapeamento aceito, o runtime cria os diretórios-pai ausentes dentro
de `drive_c` e copia o conteúdo para o caminho Windows declarado. A cópia é
exclusiva, não sobrescreve arquivos reais e usa permissões padrão `0644` para
arquivos e `0755` para diretórios, respeitando a umask do processo. A origem
em `compat/files/` não é modificada. Se qualquer cópia falhar, as cópias e os
diretórios criados anteriormente são desfeitos; sem rollback seguro, a
execução é interrompida com erro interno.

Depois que o processo convidado termina, o runtime remove somente os arquivos
e diretórios criados por aquela exposição. Um arquivo que tenha sido removido
ou substituído pelo aplicativo não é apagado; diretórios que tenham recebido
outros arquivos também são preservados. Falha de limpeza gera diagnóstico,
mas não substitui o exit code do convidado. Os arquivos materializados não
persistem no `drive_c` entre execuções.

A precedência é conservadora: um arquivo já existente em `drive_c` vence por
não permitir a ativação do perfil, e o runtime segue o comportamento genérico
sem sobrescrevê-lo. A exposição vale somente para `app run` de aplicativo
cadastrado; execução direta, instalação e `TL_DLL_OVERRIDES` permanecem
separadas. `compat/` nunca é exposta automaticamente como diretório Windows.

## Integração e promoção na B14.5

O ID do catálogo seleciona o aplicativo e, junto com ele, o prefixo que contém
seu perfil. Portanto, dois aplicativos cadastrados podem declarar o mesmo
destino Windows sem compartilhar a fonte: cada um lê somente seu próprio
`compat/files/`, materializa somente em seu próprio `drive_c` e limpa somente
os caminhos criados naquele prefixo.

A integração `integration_compat_profile_isolation` reutiliza a fixture
`tl_compat_file.exe` com dois IDs e dois prefixos independentes. Os perfis usam
conteúdos diferentes para o mesmo destino `C:\\Program Files\\Compat
Fixture\\injected.dat`; as execuções sequenciais confirmam o conteúdo correto,
a preservação das duas fontes, a remoção dos dois destinos e a ausência de
`compat/` dentro de `drive_c`. O trace registra carregamento, aplicação, cópia
e limpeza para cada ID.

Em conjunto com `integration_compat_profile`, que cobre perfil carregado,
ausente e inválido, e `integration_compat_dll_profile`, que cobre a fixture
`tl_compat_dll_app.exe`, duas DLLs personalizadas, dependência, TLS, attach,
detach, fallback por provider e isolamento, essa integração confirma que a
rejeição do perfil mantém o fallback genérico, preserva o exit code do
convidado e não permite vazamento de dados entre aplicativos ou prefixos. O
7-Zip continua sem regra declarativa específica; a B14.4 trata apenas da
extensão PE documentada acima.

## Piloto real e isolamento entre prefixos na B14.6.6

A promoção da B14.6.6 usa a integração real `integration_proton_isolation` com
`tl_compat_file.exe` e a instalação Proton configurada por `TL_PROTON_ROOT`.
O mesmo binário é cadastrado nos IDs `proton-isolation-a` e
`proton-isolation-b`, cada um com seu próprio prefixo, perfil e
`compat/files/injected.dat`. Os perfis declaram o mesmo destino Windows, mas
as fontes contêm marcadores diferentes (`proton profile a` e `proton profile b`).

As execuções sequenciais passam em Debug e Sanitize: cada fixture lê somente o
marcador do seu prefixo, retorna `0`, altera o arquivo materializado e deixa o
adaptador removê-lo sem apagar a fonte nativa. O executável estagiado, o
manifesto e o prefixo Proton permanecem independentes; `compat/` não aparece em
nenhuma árvore `drive_c`. A validação complementa o piloto mockado de
`tl_proton_probe.exe` e preserva o fallback nativo para perfis sem backend
Proton. Ela não constitui suporte a aplicativos do catálogo, especialmente
Roblox, que continua dependendo de um teste real próprio e de limitações
publicadas.
