# Ideias para diagnóstico e organização do runtime

## 1. Diagnóstico de falhas no Linux

O runtime deve diferenciar falhas do programa convidado, limitações de
compatibilidade e falhas internas do TradutorLinux.

### Categorias iniciais

- `ExitProcess`: o programa convidado encerrou explicitamente com um código.
- `unsupported`: DLL, símbolo, mecanismo ou operação ainda não suportada.
- `invalid-image`: o PE é malformado ou não pode ser mapeado com segurança.
- `guest-memory`: o programa convidado forneceu um ponteiro inválido ou uma
  faixa sem a permissão necessária.
- `linux-error`: uma operação Linux falhou; o diagnóstico deve incluir a
  operação e o `errno` convertido quando houver.
- `guest-signal`: o convidado terminou por um sinal Linux, como `SIGSEGV`,
  `SIGILL` ou `SIGBUS`.
- `internal-error`: falha inesperada na implementação do runtime.

### Contrato de saída

- `stdout` continua reservado para a saída do programa convidado.
- Diagnósticos continuam em `stderr`; `--trace` mantém o formato de
  `docs/diagnostico.md`.
- O trace deve registrar, quando disponível, `category`, `signal`, `errno`,
  `operation` e `detail`.
- O código de saída do hospedeiro continua distinguindo uso inválido, PE
  malformado, incompatibilidade e erro interno.

Exemplo desejado:

```text
[tl][process][error] terminated category="guest-signal" signal="SIGSEGV" detail="acesso inválido à memória"
```

### Isolamento recomendado

Historicamente o guest rodava no mesmo processo Linux do runtime. O fluxo atual
prepara o guest e o executa em um processo filho; assim, um `SIGSEGV` inesperado
é classificado pelo processo principal sem derrubar o launcher.

A solução implementada prepara o PE, inicia o guest no filho, espera com
`waitpid()`, distingue saída normal de término por sinal e publica um
diagnóstico controlado em `stderr`.

Não devemos usar `signal()` para tratamento geral. Um handler de sinal não pode
executar livremente C++, alocar memória ou escrever logs complexos. O isolamento
em processo filho é o marco M8 já implementado; a expansão de limites de
recursos continua sendo uma decisão futura.

### Ordem de implementação

1. Consolidar categorias e códigos para falhas já controladas.
2. Registrar `errno` e a operação nas APIs Linux relevantes.
3. Adicionar testes para falhas de ponteiro, arquivo, memória e imports.
4. Definir limites de CPU/RAM e observabilidade antes de prometer contenção de
   consumo; o isolamento em processo filho e o timeout já existem.

### Status

Implementado com o marco M8 (isolamento em processo filho): o convidado executa
em um filho (`fork`/`waitpid`), os sinais fatais do filho são restaurados para
`SIG_DFL`, e término por sinal gera `[tl][process][error] terminated
category="guest-signal" signal="SIGSEGV" detail="..."` e exit code `71`
(`GuestFault`) no hospedeiro, sem derrubar o processo principal. O exit code
bruto do convidado é transmitido por pipe e não é truncado pelo status POSIX.
Validado por `tl_crash.exe` nos presets `debug`, `sanitize` e `release`.

## 2. Política de linguagens

C++20 deve continuar sendo a linguagem principal do runtime hospedeiro. Ele é
adequado para loader, parser, gerenciamento de recursos, diagnóstico e testes,
desde que as fronteiras com o programa convidado sejam pequenas, explícitas e
`noexcept`.

### Uso de C++

- loader, parser de PE, relocations e resolvedor de imports;
- APIs internas, diagnóstico, CLI e testes;
- gerenciamento de recursos do hospedeiro.

### Uso de C

C deve ser usado somente quando trouxer vantagem clara:

- fixtures PE sem CRT, para testar o entry point mínimo;
- estruturas ou funções C simples em uma fronteira ABI;
- auxiliares que precisem evitar dependência de runtime C++.

As fixtures C não significam que o runtime inteiro será implementado em C.

### Uso de assembly

Assembly x86-64 deve permanecer restrito às fronteiras que o compilador não
expressa de forma suficientemente clara ou segura, atualmente a troca para a
pilha convidada e trampolins de ABI. Todo trecho assembly precisa documentar o
contrato, preservar registradores não voláteis e ter teste no Linux x86-64.

## 3. Relação com o roadmap

Estas ideias não autorizam compatibilidade geral, sandbox ou execução de
programas Windows arbitrários. Diagnóstico de erros controlados pode evoluir
junto do runtime atual; isolamento por processo filho deve ser um marco futuro;
a política de linguagens já é compatível com o contrato atual; e a GUI continua
limitada ao protótipo documentado na Fase 7.

colocar uma limitação de cpu e ram pro aplicativo
conseguir locar o aplicativo como path
quebrar o aplicativo, ser possivel colocar aquivos de tradução pra programas que eu nao fiz, entao assim eu posso focar em um aplicativo pra tradução, sem comprometer o resto do aplicativo
