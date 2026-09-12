# Diagnóstico e códigos de saída

Este arquivo é o índice do contrato de diagnóstico. Os eventos foram separados por tema; o formato do trace, os códigos de saída e as categorias de falha permanecem centralizados nos documentos temáticos.

## Documentos temáticos

| Documento | Conteúdo |
|---|---|
| [`diagnostico-backends.md`](diagnostico-backends.md) | Rust/C++, Proton, instalação, MSIX/AppX, perfis e catálogo. |
| [`diagnostico-runtime.md`](diagnostico-runtime.md) | Ambiente, processo, arquivos, segurança, rede, GUI, CRT, PE, SEH, loader e códigos de saída. |

Use o documento temático correspondente ao componente observado. A separação
documental não altera o formato do trace nem o comportamento do runtime.


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

No subconjunto de rede, `WSAAddressToStringA` retorna erro controlado quando
o endereço ou o buffer caller-owned é inválido. Em buffer curto, a API deixa
intacto o conteúdo fornecido, atualiza o tamanho necessário e usa
`WSAEFAULT`; isso permite distinguir erro de capacidade de uma falha de
resolução sem produzir diagnóstico parcial.

As chamadas de console são registradas pelo componente `runtime` com os
tamanhos e resultados relevantes. O componente `process` registra o retorno
do código convidado:

```text
[tl][process][info] exit exit-code="0" explicit="sim"
```

Operações genéricas de threads e eventos também registram o ciclo mínimo de
sincronização quando o trace completo está habilitado. Esses eventos não
alteram a semântica nem expõem memória convidada; os handles são apenas os
identificadores opacos usados pelo runtime para correlacionar chamadas:

```text
[tl][runtime][info] thread-create operation="guest-thread" detail="thread-id=2;start-address=...;creation-flags=0" thread="1" status="success"
[tl][runtime][info] thread-start operation="guest-thread" detail="thread-id=2" thread="2" status="success"
[tl][runtime][info] wait-single-begin operation="single-object" detail="kind=thread;handle=...;timeout-ms=4294967295" thread="1" status="success"
[tl][runtime][info] thread-exit operation="guest-thread" detail="thread-id=2;exit-code=0" thread="2" status="success"
[tl][runtime][info] wait-single-end operation="single-object" detail="kind=thread;handle=...;result=object-0" thread="1" status="success"
```

`wait-single-begin` sem um `wait-single-end` correspondente identifica uma
espera ainda pendente no instante do encerramento do processo. Esperas finitas
que terminam por `WAIT_TIMEOUT` registram `wait-single-end` com
`result=timeout`. `event-create` e `event-set` registram o mesmo vínculo para
eventos manuais ou automáticos. A fixture `tl_thread` protege a presença do
ciclo de criação, início, espera e término.

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
