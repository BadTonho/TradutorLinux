# TradutorLinux — Runtime de Compatibilidade Win32 para Linux

## 1. Visão geral

O TradutorLinux é um projeto educativo e funcional de sistemas: um runtime capaz de executar uma **classe explicitamente suportada de executáveis Windows no Linux**, sem máquina virtual e sem emular a CPU.

A primeira meta não é substituir o Wine nem prometer compatibilidade geral com aplicativos Windows. É construir, do zero, uma implementação pequena, legível, observável e confiável para programas Win32 de console selecionados.

O alvo inicial é deliberadamente restrito:

| Dimensão | Suporte inicial |
|---|---|
| Arquitetura do programa | PE32+ x86-64 |
| Máquina hospedeira | Linux x86-64 |
| Tipo de aplicativo | Console Win32, sem GUI |
| Dependências | Conjunto documentado de APIs `kernel32`/`KernelBase` |
| Fora do escopo inicial | PE32 x86, ARM, .NET, COM, drivers, DirectX e anticheat |

Com CPU igual nos dois lados, as instruções x86-64 do programa podem ser executadas nativamente. O trabalho do TradutorLinux é carregar a imagem PE, preparar o contexto de processo esperado por ela e fornecer as APIs Windows que o programa importa.

## 2. O problema, com precisão

Não é correto resumir o funcionamento a “traduzir syscalls do Windows para syscalls do Linux”. A maior parte dos aplicativos chama funções de DLLs Windows; essas funções, por sua vez, dependem de convenções de chamada, estruturas de processo, handles, erros e semânticas próprias do Windows.

Portanto, o projeto implementa uma **camada de compatibilidade em espaço de usuário**. Ela encaminha operações ao Linux quando isso preserva o comportamento definido para o escopo suportado.

Exemplo simplificado:

```text
Programa PE32+ x86-64
       │ chamadas importadas (Win32)
       ▼
PE loader + resolvedor de imports
       │
       ▼
Runtime de compatibilidade
  ├─ ponte de ABI Microsoft x64 ↔ System V AMD64
  ├─ APIs Win32 suportadas
  ├─ handles, memória, erros e caminhos
  └─ rastreamento e diagnóstico
       │
       ▼
Linux / POSIX
```

## 3. Objetivos

### 3.1. Objetivos de produto

- Executar bem um conjunto pequeno e publicado de utilitários Win32 de console.
- Informar com clareza quando um executável ou API ainda não é suportado.
- Produzir rastros de execução que expliquem imports, chamadas e falhas.
- Manter uma matriz de compatibilidade e testes automatizados para cada função implementada.

### 3.2. Objetivos de aprendizado

- Entender PE/COFF, mapeamento de memória, linking dinâmico e ABI.
- Estudar as diferenças de modelo entre processos Windows e Linux.
- Desenvolver software de baixo nível com testes, documentação e depuração de qualidade.

### 3.3. Primeiro MVP

Executar `tl_hello.exe`, um binário de teste próprio para x86-64 que importe apenas:

- `GetStdHandle`
- `WriteFile`
- `ExitProcess`

O resultado deve imprimir a saída esperada, retornar o código correto e gerar um trace reproduzível. Se uma importação desconhecida aparecer, o runtime deve encerrar de forma controlada e explicar qual módulo, símbolo e mecanismo não foram suportados.

### 3.4. Fora de escopo, por enquanto

- Compatibilidade ampla com programas comerciais ou jogos.
- Interface gráfica Win32, GDI, DirectX, áudio e GPU.
- COM, ActiveX, .NET, drivers e serviços Windows.
- Suporte a 32 bits, ARM, WOW64 ou execução cruzada de arquitetura.
- Segurança de executáveis não confiáveis. Compatibilidade não é sandbox: um `.exe` executado nativamente tem os privilégios do usuário atual.

## 4. Arquitetura proposta

| Componente | Responsabilidade |
|---|---|
| **CLI / Runner** | Receber o caminho do `.exe`, configurar logs e apresentar erros. |
| **Leitor de PE** | Validar headers e diretórios; expor seções, imports e relocations sem executar código. |
| **Image mapper** | Reservar memória, copiar seções, aplicar proteções e relocations. |
| **Loader de módulos** | Registrar módulos compatíveis e resolver imports normais, por ordinal e encaminhamentos suportados. |
| **Pontes de ABI** | Permitir que código Microsoft x64 chame uma API hospedada no Linux e retorne corretamente. |
| **Runtime Win32** | Implementar APIs compatíveis, handles, `GetLastError`, memória e caminhos. |
| **Diagnóstico** | Produzir `--trace`, relatório de imports e mensagens de incompatibilidade acionáveis. |
| **Testes** | Exercitar parser, loader, APIs e binários de integração determinísticos. |

As “DLLs” iniciais não serão arquivos falsos soltos. Serão módulos registrados pelo loader, cujas exportações apontam para funções de compatibilidade e trampolins de ABI. Essa escolha reduz o número de partes móveis do MVP e preserva o modelo que um executável PE espera encontrar.

## 5. Regras de projeto

- Implementar comportamento documentado e testado, não apenas nomes de funções.
- Acrescentar uma API somente quando existir um programa de integração que a justifique.
- Nunca chamar o ponto de entrada de uma imagem antes de mapear seções, aplicar relocations, resolver imports e preparar o contexto mínimo necessário.
- Implementar páginas de memória com permissões coerentes; não manter toda a imagem RWX por conveniência.
- Tratar executáveis de entrada como dados hostis durante o parsing. Parser, tabelas e limites devem ser validados e testados contra entradas malformadas.
- Usar documentação pública e testes de comportamento como referência. Qualquer reaproveitamento de código de terceiros exige revisão da licença aplicável antes de distribuição.

## 6. Roadmap

### Fase 0 — Fundação e contrato

- [ ] Criar a estrutura CMake, compilação com warnings rigorosos e testes automatizados.
- [ ] Fixar o alvo: Linux x86-64 hospedando somente PE32+ x86-64.
- [ ] Definir formato do trace, códigos de erro e matriz de compatibilidade.
- [ ] Criar binários de teste próprios, incluindo um executável sem CRT para o primeiro salto ao entry point.
- [ ] Documentar convenções Microsoft x64 e System V AMD64 usadas em cada fronteira.

### Fase 1 — Leitor de PE seguro

- [ ] Ler e validar DOS header, NT headers, optional header e section headers.
- [ ] Exibir seções, entry point, imports, relocations e arquitetura.
- [ ] Rejeitar PE inválido, truncado ou de arquitetura incompatível com mensagens precisas.
- [ ] Cobrir o parser com testes unitários e corpus de arquivos malformados.

### Fase 2 — Mapeamento de imagem

- [ ] Reservar a imagem no endereço preferencial quando possível.
- [ ] Copiar headers e seções, respeitando alinhamentos e permissões de página.
- [ ] Aplicar base relocations para PE32+ x86-64.
- [ ] Validar o mapeamento com executáveis mínimos que ainda não chamam APIs.

### Fase 3 — Imports e bootstrap mínimo

- [ ] Resolver import table para módulos internos suportados.
- [ ] Implementar trampolins e ponte de ABI para chamadas do programa à camada hospedeira.
- [ ] Preparar as estruturas mínimas de processo e thread exigidas pelo escopo inicial.
- [ ] Adicionar diagnóstico para DLL, símbolo, ordinal, forwarder ou delay import ainda ausente.

### Fase 4 — Console: primeiro marco público

- [ ] Implementar `GetStdHandle`, `WriteFile`, `ReadFile` e `ExitProcess`.
- [ ] Definir e testar conversão entre handles Windows e descritores Linux.
- [ ] Executar `tl_hello.exe` e uma ferramenta de eco construída no repositório.
- [ ] Verificar saída, retorno, trace e tratamento de erros em CI.

### Fase 5 — Runtime básico

- [ ] Implementar `GetLastError`/`SetLastError` e mapeamento de erros necessários.
- [ ] Implementar `VirtualAlloc`/`VirtualFree` com semântica limitada e documentada.
- [ ] Implementar abertura, leitura, escrita e fechamento de arquivos para um subconjunto de flags.
- [ ] Definir normalização de caminhos e política explícita para caminhos Windows.

### Fase 6 — Carregamento e cobertura controlada

- [ ] Adicionar APIs somente guiadas por aplicações-alvo e testes de regressão.
- [ ] Evoluir suporte a DLLs, resources, TLS callbacks, forwarders e delay-load conforme necessário.
- [ ] Publicar uma matriz: aplicativo, arquitetura, imports, APIs usadas, estado e limitações.

### Fase 7 — Avaliar GUI

- [ ] Decidir se uma interface Win32 mínima é um objetivo real de produto.
- [ ] Se sim, criar um subsistema de janela e eventos separado do runtime de console.
- [ ] Começar por `MessageBox` e uma janela simples, com testes manuais e automatizados quando viável.

## 7. Estratégia de testes e qualidade

| Nível | O que verificar |
|---|---|
| Unitário | Leitura de estruturas PE, validação de limites, conversões e APIs isoladas. |
| Integração | Executáveis de teste construídos no repositório com imports conhecidos. |
| Regressão | Saída, código de retorno, trace e erros esperados de cada aplicação-alvo. |
| Robustez | Arquivos PE truncados, offsets inválidos, imports malformados e imagens incompatíveis. |
| Manual | Depuração com `gdb`, inspeção de imagem com `llvm-objdump` e rastreamento do runtime. |

Cada marco deve ser reproduzível sem Windows. `mingw-w64` pode gerar os binários de teste, mas os primeiros exemplos devem controlar cuidadosamente CRT e imports para que cada dependência nova seja intencional.

## 8. Estrutura de pastas

```text
TradutorLinux/
├── PROJETO.md
├── README.md
├── CMakeLists.txt
├── docs/
│   ├── arquitetura/
│   ├── formatos/
│   ├── api/
│   └── compatibilidade.md
├── src/
│   ├── cli/
│   ├── pe/
│   ├── loader/
│   ├── runtime/
│   ├── abi/
│   ├── diagnostics/
│   └── main.cpp
└── tests/
    ├── fixtures/
    ├── samples/
    ├── unit/
    ├── integration/
    └── malformed-pe/
```

## 9. Stack inicial

- **Linguagem:** C++20, com C ou assembly somente nas fronteiras em que forem necessários.
- **Build:** CMake + Ninja.
- **Compilador:** Clang ou GCC no Linux, com sanitizers em testes quando possível.
- **Binários de teste:** `mingw-w64` para x86-64 e assembly/linking explícito quando for preciso eliminar CRT.
- **Inspeção e depuração:** `llvm-objdump`, `readelf`, `gdb`, `strace` e `ltrace`.
- **CI:** build, testes unitários, testes de integração e análise estática desde a primeira fase.

## 10. Riscos e respostas

| Risco | Resposta |
|---|---|
| Escopo explode com APIs | Compatibilidade publicada e aplicações-alvo antes de implementar novas funções. |
| ABI e pilha incorretas | Pontes isoladas, testes de chamada e documentação por fronteira. |
| PE malformado causa falha | Validação rigorosa de offsets/tamanhos, fuzzing e sanitizers. |
| Binário de teste traz dependências ocultas | Fixtures sem CRT e relatório automático de imports. |
| Semântica difere do Linux | Contratos explícitos, `GetLastError`, testes de regressão e limitações declaradas. |
| Execução de software malicioso | Não prometer isolamento; considerar sandbox Linux como recurso futuro independente. |

## 11. Critérios de sucesso

- **Marco 1:** o leitor identifica corretamente um PE32+ válido e rejeita entradas inválidas com segurança.
- **Marco 2:** uma imagem mínima é mapeada, relocada e tem seus imports resolvidos sem executar código fora do escopo.
- **MVP:** `./tradutorlinux tests/samples/tl_hello.exe` escreve a saída correta e encerra com o código esperado.
- **Qualidade do MVP:** todas as APIs usadas pelo exemplo possuem testes; imports ausentes são diagnosticados; o trace é reproduzível.
- **Expansão:** cada nova aplicação suportada entra na matriz de compatibilidade e na suíte de regressão.

## 12. Exemplo de uso futuro

```bash
./tradutorlinux --trace tests/samples/tl_hello.exe
```

Saída esperada:

```text
[loader] PE32+ x86-64 reconhecido
[imports] kernel32.dll!GetStdHandle -> suportado
[imports] kernel32.dll!WriteFile -> suportado
Olá do Windows no Linux!
[process] ExitProcess(0)
```
