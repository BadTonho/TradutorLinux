# Melhorias de diagnóstico

## Objetivo

Permitir que o TradutorLinux explique de forma clara quais dependências um
executável PE possui, quais APIs foram usadas durante a execução e por que uma
operação falhou.

O diagnóstico deve se concentrar no executável que o TradutorLinux está
analisando ou executando. O runtime não pretende monitorar automaticamente
qualquer aplicativo aberto no Linux.

## Recursos propostos

### 1. Relatório estático

Disponibilizar um modo de análise antes da execução, por exemplo:

```text
tradutorlinux --report aplicativo.exe
```

O relatório deve informar:

- formato e arquitetura do executável;
- DLLs importadas;
- símbolos e APIs solicitados;
- APIs suportadas, ausentes ou parcialmente implementadas;
- mecanismos ainda não suportados, como delay imports;
- declaração de que o entry point não foi executado.

### 2. Trace de execução

Durante a execução, o modo `--trace` deve registrar:

- imports resolvidos;
- chamadas às APIs Win32 implementadas;
- resultado de cada chamada;
- código de erro Win32, quando houver;
- `errno` e operação Linux relacionada, quando aplicável;
- encerramento do programa e código de retorno;
- falhas de memória, sinais e limitações de compatibilidade.

O trace continua sendo enviado para `stderr`, preservando `stdout` para a
saída do programa convidado.

### 3. Salvamento em arquivo

Adicionar uma opção para salvar o diagnóstico, por exemplo:

```text
tradutorlinux --trace --log-arquivo tradutorlinux.log aplicativo.exe
```

O formato deve continuar legível por humanos e estável para ferramentas de
análise automática. Uma opção futura pode fornecer JSON Lines para integração
com scripts e ferramentas externas.

### 4. Mensagens de erro acionáveis

Toda falha deve informar o componente envolvido, a causa e a próxima
limitação conhecida. Exemplo:

```text
[tl][imports][error] API não suportada dll="USER32.dll" symbol="MessageBoxW" status="unknown-symbol" detail="o módulo existe, mas essa exportação ainda não foi implementada" next="consulte --report e a matriz de compatibilidade"
```

O diagnóstico não deve apenas dizer que o aplicativo falhou; deve indicar o
módulo, símbolo, mecanismo ou operação que precisa ser implementado ou
corrigido.

## Limites e segurança

Não faz parte desta melhoria analisar automaticamente todos os processos em
segundo plano. Um aplicativo Linux nativo não realiza chamadas Win32 que o
runtime possa interpretar, e observar processos arbitrários exigiria recursos
de debugger, permissões adicionais e cuidados de privacidade.

Uma futura ferramenta explícita de observação de processos só deve ser
considerada como projeto separado, com alvo, permissões e comportamento bem
definidos.

O trace também não transforma o TradutorLinux em sandbox. O executável
continua sendo executado com os privilégios do usuário, conforme o escopo do
projeto.

## Critérios de conclusão

- `--report` lista as dependências sem mapear nem executar o entry point;
- `--trace` registra imports, chamadas, resultados e falhas relevantes;
- o usuário pode salvar o diagnóstico em um arquivo;
- falhas de DLL, símbolo, API, memória e operações Linux incluem contexto
  suficiente para investigação;
- stdout permanece reservado para a saída do programa convidado;
- cada comportamento novo possui testes e documentação correspondente.

## Direção de longo prazo: cobertura ampla de Win32

O objetivo de longo prazo pode ser tornar o TradutorLinux capaz de executar
uma parcela cada vez maior dos aplicativos Win32, e não apenas as fixtures
educacionais atuais. Isso deve ser tratado como uma evolução progressiva,
medida por aplicativos reais e por uma matriz de compatibilidade, sem prometer
compatibilidade geral antes de haver testes reproduzíveis.

Para avançar nessa direção, o projeto deve:

- escolher aplicativos-alvo representativos e registrar suas dependências;
- implementar APIs guiadas por falhas reais e testes de regressão;
- evoluir gradualmente console, arquivos, processos, threads, memória, GUI,
  registro, sincronização e demais subsistemas necessários;
- manter fronteiras claras entre o código Win32 e o Linux;
- isolar o programa convidado antes de oferecer recuperação confiável de
  falhas fatais;
- publicar limitações e níveis de compatibilidade para cada aplicativo.

A quantidade de APIs não deve ser o único critério. Uma API só deve ser
considerada compatível quando seu comportamento, erros, memória, handles e
integração com as demais partes do runtime estiverem testados.

## Seleção explícita e listagem de executáveis

Para preservar a privacidade e dar controle ao usuário, o TradutorLinux deve
analisar somente o executável escolhido explicitamente. O fluxo proposto é:

```text
tradutorlinux --list --scan-dir ~/AplicativosWindows
tradutorlinux --report caminho/aplicativo.exe
tradutorlinux --trace --log-arquivo aplicativo.log caminho/aplicativo.exe
```

O modo de listagem deve:

- examinar somente as pastas informadas pelo usuário;
- localizar arquivos `.exe` e identificar os que são PE32+ x86-64;
- mostrar caminho, arquitetura, imports conhecidos e status de suporte;
- permitir selecionar um executável para gerar o relatório ou executar;
- evitar monitorar automaticamente processos em segundo plano;
- não enviar nomes de arquivos, imports ou logs para serviços externos.

Uma interface futura pode oferecer uma lista interativa, mas a seleção direta
por caminho deve continuar disponível para automação e scripts. A listagem de
aplicativos Linux em geral não é suficiente: o foco dessa funcionalidade é
encontrar executáveis Windows que possam ser carregados pelo runtime.
