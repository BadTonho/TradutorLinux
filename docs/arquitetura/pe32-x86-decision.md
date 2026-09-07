# Decisão de escopo: PE32/x86

Data da decisão: 2026-09-07

## Decisão

O TradutorLinux continuará rejeitando PE32/x86 com `Unsupported` enquanto não
houver uma fase própria, autorização explícita e um contrato técnico completo
para essa arquitetura. O escopo operacional atual permanece PE32+ AMD64 em
host Linux x86-64.

Esta decisão não declara suporte para nenhum aplicativo PE32/x86 e não muda o
comportamento do parser, do loader, do `--report`, do `app run` ou do Proton.

## Evidência do corpus

Os arquivos locais foram classificados pelo `file` durante a auditoria do
corpus de aplicativos:

- `CPU-Z_2.18_en.exe`: PE32 Intel 80386;
- `GPU-Z_2.70.0.exe`: PE32 Intel 80386, empacotado com UPX;
- `HWMonitor_1.67.exe`: PE32 Intel 80386;
- instaladores de CPU-Z, GPU-Z e HWMonitor: PE32 Intel 80386.

O relatório do runtime retorna arquitetura não suportada para esses arquivos,
antes de mapeamento ou execução. HWiNFO64, em contraste, é PE32+ x86-64, mas
continua limitado pelo resultado específico documentado na matriz de
compatibilidade.

## Impacto técnico

Abrir suporte x86 exigiria uma fase própria para, no mínimo:

1. validar cabeçalhos, diretórios, relocations, imports, TLS e exceções do
   formato PE32;
2. definir a fronteira de execução para o conjunto de registradores, ponteiros,
   stack e convenções de chamada de 32 bits;
3. decidir como o host Linux x86-64 executará código x86 e quais bibliotecas
   convidadas serão disponibilizadas;
4. revisar o mapeamento de memória, permissões, unwind, threads e chamadas
   Win32 sem reutilizar suposições de PE32+;
5. criar fixtures mínimas, testes diferenciais, testes de segurança e uma
   matriz de compatibilidade separada.

Implementar somente o parsing ou remover a rejeição da arquitetura não seria
suporte funcional e poderia permitir que um arquivo incompatível alcançasse o
loader. Por isso, a rejeição atual é preservada como comportamento controlado.

## Critérios para reabrir a decisão

Uma futura fase só poderá começar depois de:

- autorização explícita para ampliar o escopo para x86;
- atualização de `ROADMAP.md`, `docs/compatibilidade.md` e dos contratos
  técnicos;
- desenho revisado da execução x86 e das fronteiras ABI;
- fixture PE32 mínima com análise, mapeamento e execução separadamente
  verificáveis;
- testes de regressão para garantir que PE32+ AMD64 e os caminhos Proton não
  sejam alterados.

Até lá, PE32/x86 permanece fora do escopo e deve falhar de forma previsível
com diagnóstico de arquitetura não suportada.
