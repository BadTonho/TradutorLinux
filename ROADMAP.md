# Roadmap do TradutorLinux

Este arquivo acompanha a execução do projeto. O documento de visão, escopo e arquitetura está em [PROJETO.md](PROJETO.md).

## Como usar este roadmap

Cada fase só deve avançar quando seus critérios de saída estiverem atendidos. Uma API nova entra no projeto apenas quando existir um teste ou aplicativo-alvo que justifique seu comportamento.

Os itens marcados como concluídos devem ter evidência no repositório: código, teste, documentação ou um artefato reproduzível. O roadmap descreve ordem de dependências, não uma promessa de prazo.

## Estado atual

- **Fase atual:** Fase 0 — Fundação e contrato.
- **Marco em andamento:** criar a estrutura inicial de build, testes e fixtures.
- **Próximo resultado observável:** um executável de teste PE32+ próprio e um leitor que imprima seus metadados.

## Fase 0 — Fundação e contrato

- [ ] Criar a estrutura CMake, compilação com warnings rigorosos e testes automatizados.
- [ ] Fixar o alvo: Linux x86-64 hospedando somente PE32+ x86-64.
- [ ] Definir formato do trace, códigos de erro e matriz de compatibilidade.
- [ ] Criar binários de teste próprios, incluindo um executável sem CRT para o primeiro salto ao entry point.
- [ ] Documentar as convenções Microsoft x64 e System V AMD64 usadas em cada fronteira.
- [ ] Configurar sanitizers e análise estática para os testes quando possível.

### Critério de saída

O projeto compila de forma reproduzível, executa seus testes básicos e possui fixtures Windows versionadas com seus imports documentados.

## Fase 1 — Leitor de PE seguro

- [ ] Ler e validar DOS header, NT headers, optional header e section headers.
- [ ] Exibir seções, entry point, imports, relocations e arquitetura.
- [ ] Rejeitar PE inválido, truncado ou de arquitetura incompatível com mensagens precisas.
- [ ] Cobrir o parser com testes unitários e corpus de arquivos malformados.
- [ ] Comparar a saída com `llvm-objdump` e outras ferramentas de inspeção.

### Critério de saída

O leitor identifica corretamente os fixtures válidos e nunca acessa memória fora dos limites ao processar fixtures inválidos.

## Fase 2 — Mapeamento de imagem

- [ ] Reservar a imagem no endereço preferencial quando possível.
- [ ] Copiar headers e seções, respeitando alinhamentos e permissões de página.
- [ ] Aplicar base relocations para PE32+ x86-64.
- [ ] Validar o mapeamento com executáveis mínimos que ainda não chamam APIs.
- [ ] Garantir que a imagem não permaneça inteira com permissão RWX por conveniência.

### Critério de saída

Um executável mínimo pode ser mapeado e inspecionado pelo runtime sem executar funcionalidades fora do escopo.

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
