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

## Integração com “Abrir com”

O TradutorLinux deve oferecer uma integração opcional com os gerenciadores de
arquivos Linux. Ao clicar com o botão direito em um arquivo `.exe`, o usuário
poderá escolher uma ação do TradutorLinux, sem que o runtime precise monitorar
processos ou arquivos automaticamente.

Um comando de instalação por usuário pode registrar essa integração:

```text
tradutorlinux --install-file-association
```

Esse comando deve criar um lançador `.desktop` no perfil do usuário e associar
o caminho selecionado ao comando do runtime. A instalação não deve exigir
permissões administrativas nem modificar associações globais sem confirmação.

As ações disponíveis podem incluir:

- **Analisar com TradutorLinux**: executa `--report arquivo.exe` sem iniciar o
  programa convidado;
- **Executar com TradutorLinux**: carrega o executável selecionado;
- **Executar com trace**: inicia o programa e salva o diagnóstico em um arquivo
  escolhido pelo usuário.

O lançador deve receber somente o arquivo selecionado pelo usuário, manter os
diagnósticos em arquivos locais e preservar as mesmas validações de formato,
arquitetura e compatibilidade usadas pela CLI.

## Auditoria do projeto — direção para compatibilidade ampla

Esta auditoria foi registrada em 2026-08-16. A conclusão é que a direção
arquitetural está correta, mas o runtime ainda está na fase de fundação: o
`xxd.exe` é o primeiro aplicativo real executado de ponta a ponta; `bzip2`,
`dos2unix` e `unix2dos` ainda estão em estado `unsupported`.

O objetivo estratégico continua sendo ampliar progressivamente o suporte para
aplicativos Windows em geral. Esse objetivo deve ser medido por aplicativos
reais, fluxos completos, resultados corretos e regressões automatizadas, não
apenas pela quantidade de APIs implementadas.

### Prioridade imediata — concluir a Fase 8

- [ ] Implementar `msvcrt.dll!strncpy`.
- [ ] Implementar `msvcrt.dll!strstr`.
- [ ] Implementar `msvcrt.dll!ungetc`.
- [ ] Criar e executar o teste e2e do `bzip2.exe`, verificando stdout, stderr,
      exit code, arquivos produzidos e timeout.
- [ ] Atualizar `docs/compatibilidade.md` somente depois de haver evidência
      reproduzível.
- [ ] Escolher o caminho de `dos2unix`/`unix2dos`: implementar o subconjunto
      `W` e `SHELL32.dll!CommandLineToArgvW`, ou registrar formalmente a
      decisão de adiar esses alvos.
- [ ] Encerrar a Fase 8 somente quando pelo menos três aplicativos reais
      executarem fluxos completos no Linux.

### Prioridade estrutural — preparar o runtime para crescer

- [ ] Separar `src/runtime/winapi.cpp` em subsistemas menores: console,
      arquivos, memória, processo, sincronização, GUI e GDI.
- [ ] Substituir o estado global por um contexto explícito do processo
      convidado (`GuestContext` ou equivalente).
- [ ] Substituir limites fixos de handles, arquivos, alocações, janelas e TLS
      por gerenciadores controlados, com diagnóstico claro de exaustão.
- [ ] Definir uma política de handles única, com tipo, proprietário, validade,
      encerramento e conversão documentados.
- [ ] Garantir que cada API nova continue tendo contrato, fixture ou aplicativo
      real, teste de regressão e entrada na matriz.

### Loader e processo — bloqueios para aplicativos comuns

- [ ] Implementar carregamento de DLLs PE convidadas e dependências recursivas.
- [ ] Implementar `LoadLibrary`/`GetProcAddress` dentro do conjunto permitido.
- [ ] Implementar forwarders, inicialização de módulos e encerramento de DLLs.
- [ ] Definir suporte para TLS callbacks e dados TLS por thread.
- [ ] Implementar threads convidadas, TEB por thread, `WaitForSingleObject`,
      eventos e mutexes quando um aplicativo-alvo justificar.
- [ ] Substituir o stub de `__C_specific_handler` por suporte compatível com o
      fluxo de exceções estruturadas que os alvos exigirem.
- [ ] Testar falhas de `SIGSEGV`, `SIGILL`, `SIGBUS`, timeout e encerramento
      durante operações com múltiplas threads.

### Arquivos e Unicode — maior ganho prático depois do CRT

- [ ] Implementar diretório atual e diretório do executável.
- [ ] Definir tradução segura de caminhos Windows, incluindo separadores,
      caminhos absolutos e a política para letras de unidade.
- [ ] Implementar `FindFirstFileA/W`, `FindNextFileA/W`, `FindClose`,
      `GetFileAttributesA/W`, `CreateDirectoryA/W`, `MoveFileA/W` e
      `DeleteFileA/W` conforme os aplicativos-alvo exigirem.
- [ ] Completar o caminho UTF-16/UTF-8 e testar nomes não ASCII.
- [ ] Expandir `VirtualAlloc`/`VirtualFree` somente quando um alvo real exigir
      reserva, commit parcial ou memória executável.

### GUI — continuar orientada por aplicativo

- [ ] Escolher um aplicativo GUI real, pequeno e de código aberto antes de
      ampliar genericamente `USER32.dll` ou `GDI32.dll`.
- [ ] Implementar somente os controles, mensagens, recursos, fontes, Unicode,
      mouse e pintura exigidos por esse alvo.
- [ ] Adicionar teste automatizado do fluxo principal, além de verificar apenas
      abertura de janela e exit code.
- [ ] Manter a GUI mínima atual explicitamente experimental até existir um
      aplicativo GUI real com fluxo principal validado.

### Documentação a alinhar

- [ ] Revisar `ideia.md`, que ainda contém linguagem anterior dizendo que o
      projeto não autoriza compatibilidade geral.
- [ ] Revisar o cabeçalho de `docs/compatibilidade.md` para distinguir
      “objetivo de compatibilidade ampla” de “suporte comprovado atual”.
- [ ] Separar nos documentos as referências históricas às Fases 5, 7 e 9 da
      fase atualmente ativa.
- [ ] Manter `AGENTS.md`, `PROJETO.md`, `ROADMAP.md`, `README.md` e a matriz
      com a mesma definição de objetivo e de suporte.

### Checklist de validação no Linux

Executar no Linux x86-64, com `cmake`, `ninja`, `llvm`, `mingw-w64`, X11 e
`Xvfb` instalados:

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure

cmake --preset release
cmake --build --preset release --parallel
ctest --preset release --output-on-failure

cmake --preset sanitize
cmake --build --preset sanitize --parallel
ctest --preset sanitize --output-on-failure

cmake -S . -B build/targetapps -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTL_BUILD_TARGET_APPS=ON
cmake --build build/targetapps --parallel
ctest --test-dir build/targetapps -L targetapp --output-on-failure
```

Antes de marcar uma tarefa como concluída, confirmar também:

- `cppcheck` sem pendências;
- `clang-tidy` sem warnings tratados como erro;
- `stdout` preservado para o convidado;
- trace e erros em `stderr`;
- matriz de compatibilidade atualizada;
- nenhuma regressão nos aplicativos e fixtures anteriores;
- limitações novas documentadas de forma honesta.

### Estado da auditoria

- [x] Loader, relocations, imports básicos e ponte de ABI possuem fundação
      testada.
- [x] Diagnóstico, timeout e isolamento por processo filho estão implementados.
- [x] Existe um aplicativo real (`xxd.exe`) executando de ponta a ponta.
- [ ] A Fase 8 ainda não atingiu três aplicativos reais executando fluxos
      completos.
- [ ] A compatibilidade ampla ainda depende de DLLs convidadas, threads,
      exceções, filesystem completo, Unicode e maior cobertura de APIs.
