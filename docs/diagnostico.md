# Diagnóstico e códigos de saída

## Saída de trace

O trace é habilitado por `--trace`, vai exclusivamente para `stderr` e ocupa uma linha por evento:

```text
[tl][<componente>][<nível>] <evento> chave="valor"
```

Componentes iniciais: `cli`, `pe`, `loader`, `imports` e `runtime`.

Níveis iniciais: `debug`, `info`, `warning` e `error`.

Valores sempre usam aspas duplas. Dentro deles, barra invertida, aspas, quebra de linha, retorno de carro e tabulação são escapados como `\\`, `\"`, `\n`, `\r` e `\t`. A forma é legível por humanos e estável para ferramentas simples de parsing.

Exemplo atual:

```text
[tl][cli][info] input path="tests/samples/generated/tl_hello.exe"
[tl][pe][info] image format="PE32+" arch="x86-64" entry="0x1000" image-base="0x140000000" size-of-image="0x4000" sections="3"
[tl][pe][info] section index="0" name=".text" virtual-address="0x1000" virtual-size="0x90" raw-pointer="0x400" raw-size="0x200" characteristics="0x60000020"
[tl][pe][info] import dll="KERNEL32.dll" symbols="ExitProcess,GetStdHandle,WriteFile"
[tl][pe][info] relocations blocks="0" entries="0"
```

## Eventos do componente `pe`

O leitor da Fase 1 emite um evento `image` com os campos `format`, `arch`, `entry`, `image-base`, `size-of-image` e `sections`, seguido de um evento `section` por seção (`index`, `name`, `virtual-address`, `virtual-size`, `raw-pointer`, `raw-size`, `characteristics`), um evento `import` por DLL (`dll`, `symbols`) e um evento `relocations` (`blocks`, `entries`).

Em nível `debug`, cada bloco de base relocation é registrado com `page-rva` e `entries`.

Quando o arquivo não é um PE32+ aceitável, o leitor emite:

```text
[tl][pe][error] parse-failed status="truncated" detail="arquivo menor que o cabeçalho DOS (64 bytes)"
```

Os valores possíveis de `status` são `truncated`, `malformed`, `unsupported-architecture` e `unsupported-format`. O campo `detail` informa a condição específica rejeitada. A partir da Fase 1, um arquivo de entrada regular que não seja PE válido retorna o código `4` (`MalformedPe`); o código `5` (`Unsupported`) fica reservado para arquivos PE válidos mas incompatíveis (arquitetura ou formato).

## Códigos de saída do host

| Código | Nome | Significado |
|---:|---|---|
| 0 | `Success` | A operação solicitada terminou corretamente. |
| 2 | `Usage` | Argumentos inválidos, ausentes ou incompatíveis. |
| 3 | `InputUnavailable` | O arquivo informado não existe, não é regular ou não pode ser acessado. |
| 4 | `MalformedPe` | O arquivo é reconhecido como PE malformado ou truncado. |
| 5 | `Unsupported` | PE válido de arquitetura ou formato ainda não suportado (ex.: PE32/x86), ou etapa futura do runtime não disponível. |
| 70 | `InternalError` | Erro interno inesperado do runtime. |

Na Fase 1, um arquivo regular que não é PE válido retorna `4`, e um PE válido porém incompatível (arquitetura ou formato não suportado) retorna `5`. A partir da Fase 4, o código bruto de `ExitProcess` do programa convidado será registrado no trace; a regra de propagação ao processo Linux será definida antes dessa implementação.
