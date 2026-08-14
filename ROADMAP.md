# Roadmap do TradutorLinux

Este arquivo acompanha a execução do projeto. O documento de visão, escopo e arquitetura está em [PROJETO.md](PROJETO.md).

## Como usar este roadmap

Cada fase só deve avançar quando seus critérios de saída estiverem atendidos. Uma API nova entra no projeto apenas quando existir um teste ou aplicativo-alvo que justifique seu comportamento.

Os itens marcados como concluídos devem ter evidência no repositório: código, teste, documentação ou um artefato reproduzível. O roadmap descreve ordem de dependências, não uma promessa de prazo.

## Stack decidido

- **Linguagem principal:** C++20.
- **C:** estruturas PE, interfaces C e trechos que precisem de ABI simples.
- **Assembly x86-64:** somente trampolins, bootstrap ou outras fronteiras que não possam ser expressas com segurança pelo compilador.
- **Build:** CMake + Ninja.
- **Hospedeiro inicial:** Linux x86-64.
- **Binários de teste:** PE32+ x86-64 produzidos com `mingw-w64`.

## Estado atual

- **Fase atual:** Fase 2 — Mapeamento de imagem.
- **Marco em andamento:** imagem PE32+ mapeada no endereço preferencial quando possível, headers/seções copiados com permissões de página e base relocations aplicadas para PE32+ x86-64.
- **Próximo resultado observável:** CLI mapeia, realoca e inspeciona as fixtures mínimas (`tl_nop`, `tl_hello`, `tl_reloc`) com trace e resumo cobertos por CTest; nenhum entry point é executado antes da Fase 3.

## Fase 0 — Fundação e contrato

- [x] Criar a estrutura CMake, compilação com warnings rigorosos e testes automatizados.
- [x] Fixar o alvo: Linux x86-64 hospedando somente PE32+ x86-64.
- [x] Definir formato do trace, códigos de erro e matriz de compatibilidade.
- [x] Criar binários de teste próprios, incluindo um executável sem CRT para o primeiro salto ao entry point.
- [x] Documentar as convenções Microsoft x64 e System V AMD64 usadas em cada fronteira.
- [x] Configurar sanitizers e análise estática para os testes quando possível.

### Critério de saída

O projeto compila de forma reproduzível, executa seus testes básicos e possui fixtures Windows versionadas com seus imports documentados.

## Fase 1 — Leitor de PE seguro

- [x] Ler e validar DOS header, NT headers, optional header e section headers.
- [x] Exibir seções, entry point, imports, relocations e arquitetura.
- [x] Rejeitar PE inválido, truncado ou de arquitetura incompatível com mensagens precisas.
- [x] Cobrir o parser com testes unitários e corpus de arquivos malformados.
- [x] Comparar a saída com `llvm-readobj` nas fixtures geradas.

### Critério de saída

O leitor identifica corretamente os fixtures válidos e nunca acessa memória fora dos limites ao processar fixtures inválidos. Validação: fixtures `tl_hello.exe`/`tl_nop.exe` parseados e comparados com `llvm-readobj` em CTest, corpus malformado coberto por testes, presets `debug` e `sanitize` verdes e análise estática sem pendências.

## Fase 2 — Mapeamento de imagem

- [x] Reservar a imagem no endereço preferencial quando possível.
- [x] Copiar headers e seções, respeitando alinhamentos e permissões de página.
- [x] Aplicar base relocations para PE32+ x86-64.
- [x] Validar o mapeamento com executáveis mínimos que ainda não chamam APIs.
- [x] Garantir que a imagem não permaneça inteira com permissão RWX por conveniência.

### Critério de saída

Um executável mínimo pode ser mapeado e inspecionado pelo runtime sem executar funcionalidades fora do escopo.

Validação: `tl_nop.exe`, `tl_hello.exe` e `tl_reloc.exe` mapeados via CLI em `debug` e `sanitize`; relocations aplicadas e verificadas em memória mapeada quando a base difere da preferencial (ASan bloqueia `0x140000000` no preset `sanitize`); permissões de região verificadas via `/proc/self/maps` (headers `r--`, `.text` `r-x`, nunca `rwx`); presets `debug` e `sanitize` verdes e análise estática sem pendências. O contrato de mapeamento está em `docs/arquitetura/mapeamento-imagem.md`.

## Fase 3 — Imports e bootstrap mínimo

- [ ] Resolver a import table para módulos internos suportados.
- [ ] Implementar trampolins e ponte de ABI para chamadas do programa à camada hospedeira.
- [ ] Preparar as estruturas mínimas de processo e thread exigidas pelo escopo inicial.
- [ ] Adicionar diagnóstico para DLL, símbolo, ordinal, forwarder ou delay import ausente.
- [ ] Definir o comportamento de falha antes do entry point quando uma dependência não for suportada.

### Critério de saída

O runtime resolve imports conhecidos com o ABI correto e informa de maneira reproduzível qualquer dependência desconhecida.

## Fase 4 — Console: primeiro marco público

- [ ] Implementar `GetStdHandle`, `WriteFile`, `ReadFile` e `ExitProcess`.
- [ ] Definir e testar conversão entre handles Windows e descritores Linux.
- [ ] Executar `tl_hello.exe` e uma ferramenta de eco construída no repositório.
- [ ] Verificar saída, retorno, trace e tratamento de erros em CI.
- [ ] Documentar exatamente quais flags, handles e encodings são suportados.

### Critério de saída — MVP

```text
./tradutorlinux --trace tests/samples/tl_hello.exe
```

O comando escreve a saída esperada, retorna o código correto, produz trace reproduzível e possui testes para todas as APIs usadas pelo fixture.

## Fase 5 — Runtime básico

- [ ] Implementar `GetLastError`/`SetLastError` e o mapeamento de erros necessário.
- [ ] Implementar `VirtualAlloc`/`VirtualFree` com semântica limitada e documentada.
- [ ] Implementar abertura, leitura, escrita e fechamento de arquivos para um subconjunto de flags.
- [ ] Definir normalização de caminhos e política explícita para caminhos Windows.
- [ ] Adicionar testes de concorrência somente quando o modelo de threads fizer parte do escopo.

### Critério de saída

Uma aplicação de console consegue ler e escrever arquivos e usar memória alocada pelo runtime, com erros verificáveis e documentados.

## Fase 6 — Carregamento e cobertura controlada

- [ ] Adicionar APIs somente guiadas por aplicações-alvo e testes de regressão.
- [ ] Evoluir suporte a DLLs, resources, TLS callbacks, forwarders e delay-load conforme necessário.
- [ ] Publicar uma matriz com aplicativo, arquitetura, imports, APIs usadas, estado e limitações.
- [ ] Adicionar um modo de relatório que mostre o que falta para tentar executar um `.exe`.
- [ ] Revisar periodicamente o custo de cada API em relação ao valor para os aplicativos-alvo.

### Critério de saída

Cada aplicação declarada como suportada possui um teste de regressão e uma lista explícita de limitações.

## Fase 7 — Avaliar GUI

- [ ] Decidir se uma interface Win32 mínima é um objetivo real de produto.
- [ ] Se sim, criar um subsistema de janela e eventos separado do runtime de console.
- [ ] Começar por `MessageBox` e uma janela simples, com testes manuais e automatizados quando viável.
- [ ] Definir se a integração será com X11, Wayland, toolkit ou uma camada própria.

### Critério de saída

Uma aplicação gráfica de teste cria uma janela, recebe eventos básicos e encerra corretamente, sem comprometer o runtime de console.

## Próximos marcos

| Marco | Resultado verificável |
|---|---|
| M1 — Parser | PE32+ válido lido; PE inválido rejeitado com segurança. |
| M2 — Image mapper | Imagem mínima mapeada, relocada e protegida. |
| M3 — Imports | Imports conhecidos resolvidos; ausentes diagnosticados. |
| M4 — Console | `tl_hello.exe` executa com saída e retorno corretos. |
| M5 — Arquivos | Fixture lê e escreve arquivo com semântica documentada. |
| M6 — Cobertura | Primeira aplicação-alvo adicional incluída na matriz e na regressão. |
| M7 — GUI | Decisão de produto tomada e, se aprovada, primeiro protótipo funcional. |

## Definição de pronto

Uma tarefa do roadmap só é considerada pronta quando:

- o código foi compilado com as configurações suportadas;
- existe um teste automatizado ou uma justificativa documentada para teste manual;
- falhas são observáveis pelo trace ou por uma mensagem de erro útil;
- a documentação da API ou limitação foi atualizada;
- o comportamento não quebra os fixtures já suportados.
