# Diagnóstico e códigos de saída

## Saída de trace

O trace é habilitado por `--trace`, vai exclusivamente para `stderr` e ocupa uma linha por evento:

```text
[tl][<componente>][<nível>] <evento> chave="valor"
```

Componentes iniciais: `cli`, `pe`, `loader`, `imports`, `runtime` e `process`.

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

O resolvedor da Fase 3 emite um evento `resolved` por importação resolvida (`dll`, `symbol` — nome ou `ordinal(N)` — e `address`):

```text
[tl][imports][info] resolved dll="KERNEL32.dll" symbol="WriteFile" address="0x..."
```

As chamadas de console são registradas pelo componente `runtime` com os
tamanhos e resultados relevantes. O componente `process` registra o retorno
do código convidado:

```text
[tl][process][info] exit exit-code="0" explicit="sim"
```

O modo `--report` produz um relatório textual em stdout sem executar o entry
point. Cada import aparece com seu estado, seguido de `result: supported` ou
`result: unsupported` e `execution: not-attempted`.

Quando uma importação não pode ser resolvida, emite um evento `unresolved` com os campos `dll`, `symbol`, `status` e `detail`:

```text
[tl][imports][error] unresolved dll="USER32.dll" symbol="MessageBoxA" status="unknown-dll" detail="módulo não registrado"
```

Os valores possíveis de `status` são:

| `status` | Significado |
|---|---|
| `unknown-dll` | DLL não registrada no runtime. |
| `unknown-symbol` | DLL conhecida, símbolo não exportado. |
| `unknown-ordinal` | DLL conhecida, ordinal não exportado. |
| `not-implemented` | Símbolo conhecido, sem implementação no runtime. |
| `unsupported-mechanism` | Mecanismo ainda não suportado (ex.: delay imports, IAT fora das seções). |

Quando qualquer importação falha, a resolução inteira falha e o processo não tem entry point executado; o runtime retorna `5` (`Unsupported`). Mesmo na falha, todas as entradas são reportadas para que o diagnóstico seja completo.

## Eventos do componente `pe`

O leitor da Fase 1 emite um evento `image` com os campos `format`, `arch`, `entry`, `image-base`, `size-of-image` e `sections`, seguido de um evento `section` por seção (`index`, `name`, `virtual-address`, `virtual-size`, `raw-pointer`, `raw-size`, `characteristics`), um evento `import` por DLL (`dll`, `symbols`) e um evento `relocations` (`blocks`, `entries`).

Em nível `debug`, cada bloco de base relocation é registrado com `page-rva` e `entries`.

Quando o arquivo não é um PE32+ aceitável, o leitor emite:

```text
[tl][pe][error] parse-failed status="truncated" detail="arquivo menor que o cabeçalho DOS (64 bytes)"
```

Os valores possíveis de `status` são `truncated`, `malformed`, `unsupported-architecture` e `unsupported-format`. O campo `detail` informa a condição específica rejeitada. A partir da Fase 1, um arquivo de entrada regular que não seja PE válido retorna o código `4` (`MalformedPe`); o código `5` (`Unsupported`) fica reservado para arquivos PE válidos mas incompatíveis (arquitetura ou formato).

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
| 70 | `InternalError` | Erro interno inesperado do runtime. |

Na Fase 1, um arquivo regular que não é PE válido retorna `4`, e um PE válido porém incompatível (arquitetura ou formato não suportado) retorna `5`. A partir da Fase 2, uma imagem válida porém não mapeável por inconsistência estrutural retorna `4`, e uma falha de mapeamento por memória insuficiente retorna `70`. A partir da Fase 3, um PE válido com dependências não suportadas (DLL, símbolo, ordinal ou mecanismo desconhecidos) também retorna `5`, com diagnóstico completo no trace e o entry point nunca executado. Na Fase 4, `ExitProcess` gera `[tl][runtime][info]` com o código bruto e `[tl][process][info] exit`; esse código é propagado como status do processo Linux.
