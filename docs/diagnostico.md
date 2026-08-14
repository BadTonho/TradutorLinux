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
[tl][cli][info] input path="tests/samples/tl_hello.exe"
[tl][runtime][error] feature-unavailable feature="pe-loader" phase="1"
```

## Códigos de saída do host

| Código | Nome | Significado |
|---:|---|---|
| 0 | `Success` | A operação solicitada terminou corretamente. |
| 2 | `Usage` | Argumentos inválidos, ausentes ou incompatíveis. |
| 3 | `InputUnavailable` | O arquivo informado não existe, não é regular ou não pode ser acessado. |
| 4 | `MalformedPe` | O arquivo será reconhecido como PE malformado a partir da Fase 1. |
| 5 | `Unsupported` | Arquitetura, recurso ou etapa do runtime ainda não é suportada. |
| 70 | `InternalError` | Erro interno inesperado do runtime. |

Na Fase 0, qualquer arquivo regular retorna `Unsupported`, pois não há parser ou loader. A partir da Fase 4, o código bruto de `ExitProcess` do programa convidado será registrado no trace; a regra de propagação ao processo Linux será definida antes dessa implementação.
