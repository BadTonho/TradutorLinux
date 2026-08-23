# TradutorLinux — Runtime de Compatibilidade Win32 para Linux

## 1. Visão geral

O TradutorLinux é um projeto educativo e funcional de sistemas: um runtime capaz de executar, de forma progressiva, **classes cada vez mais amplas de executáveis Windows no Linux**, sem máquina virtual e sem emular a CPU.

O objetivo estratégico é tornar o runtime útil para a maior variedade prática de
aplicativos Windows de espaço de usuário dentro do alvo suportado, avançando por
classes de uso e por um portfólio de aplicativos reais. Isso é uma meta de
produto de longo prazo, não uma alegação de compatibilidade universal imediata:
cada capacidade precisa ser implementada, testada e publicada antes de ser
considerada suportada.

O alvo inicial é deliberadamente restrito:

| Dimensão | Suporte inicial |
|---|---|
| Arquitetura do programa | PE32+ x86-64 |
| Máquina hospedeira | Linux x86-64 |
| Tipo de aplicativo | Console Win32 e GUI Win32 mínima experimental |
| Dependências | Conjunto documentado de APIs `KERNEL32`, `USER32` e `GDI32` |
| Fora do escopo inicial | PE32 x86, ARM, .NET, COM, drivers, GUI Win32 ampla, GDI completo, DirectX e anticheat |

Com CPU igual nos dois lados, as instruções x86-64 do programa podem ser executadas nativamente. O trabalho do TradutorLinux é carregar a imagem PE, preparar o contexto de processo esperado por ela e fornecer as APIs Windows que o programa importa.

### Linguagem principal

O projeto será escrito principalmente em **C++20**. C poderá ser usado em estruturas compatíveis com o formato PE, interfaces C e trechos em que uma ABI simples seja importante. Assembly x86-64 ficará restrito às fronteiras realmente necessárias, como trampolins ou bootstrap do processo.

Essa divisão mantém o controle de baixo nível sem transformar todo o projeto em assembly ou em C puro. A regra é deixar a fronteira entre o código Windows e o código Linux pequena, explícita e coberta por testes.

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

- Manter os alvos já suportados e ampliar progressivamente um portfólio de
  console, arquivos, rede, instaladores e GUI, priorizando capacidades úteis a
  várias aplicações em vez de ajustes exclusivos para um programa.
- Buscar cobertura prática de classes comuns de aplicativos Win32 PE32+ x86-64
  de espaço de usuário, sem declarar que todo `.exe` dessas classes funciona
  antes de haver evidência reproduzível.
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

### 3.4. Fora de escopo no estágio atual

- Compatibilidade presumida com qualquer executável sem análise, implementação e regressão correspondentes.
- Interface gráfica Win32 além do subconjunto experimental, GDI completo, DirectX, áudio e GPU.
- COM, ActiveX, .NET, drivers e serviços Windows.
- Suporte a 32 bits, ARM, WOW64 ou execução cruzada de arquitetura.
- Segurança de executáveis não confiáveis. Compatibilidade não é sandbox: um `.exe` executado nativamente tem os privilégios do usuário atual.

Esses limites descrevem o estágio atual, não o limite definitivo do produto. A expansão para novas classes exige uma fase ou aplicativo-alvo explícito, contratos técnicos, testes de integração e atualização da matriz de compatibilidade.

O plano de execução, dividido em fases, marcos e entregas verificáveis, está em [ROADMAP.md](ROADMAP.md).

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

## 6. Estratégia de testes e qualidade

| Nível | O que verificar |
|---|---|
| Unitário | Leitura de estruturas PE, validação de limites, conversões e APIs isoladas. |
| Integração | Executáveis de teste construídos no repositório com imports conhecidos. |
| Regressão | Saída, código de retorno, trace e erros esperados de cada aplicação-alvo. |
| Robustez | Arquivos PE truncados, offsets inválidos, imports malformados e imagens incompatíveis. |
| Manual | Depuração com `gdb`, inspeção de imagem com `llvm-objdump` e rastreamento do runtime. |

Cada marco deve ser reproduzível sem Windows. `mingw-w64` pode gerar os binários de teste, mas os primeiros exemplos devem controlar cuidadosamente CRT e imports para que cada dependência nova seja intencional.

## 7. Estrutura de pastas

```text
TradutorLinux/
├── PROJETO.md
├── ROADMAP.md
├── README.md
├── CMakeLists.txt
├── CMakePresets.json
├── docs/
│   ├── arquitetura/
│   ├── formatos/
│   ├── api/
│   └── compatibilidade.md
├── src/
│   ├── cli.cpp
│   ├── diagnostics/
│   ├── main.cpp
│   └── (pe/, loader/, runtime/ e abi/ nas fases posteriores)
└── tests/
    ├── samples/
    ├── test_cli.cpp
    ├── test_trace.cpp
    └── (malformed-pe/ a partir da Fase 1)
```

## 8. Stack inicial

- **Linguagem:** C++20, com C ou assembly somente nas fronteiras em que forem necessários.
- **Build:** CMake + Ninja.
- **Compilador:** Clang ou GCC no Linux, com sanitizers em testes quando possível.
- **Binários de teste:** `mingw-w64` para x86-64 e assembly/linking explícito quando for preciso eliminar CRT.
- **Inspeção e depuração:** `llvm-objdump`, `readelf`, `gdb`, `strace` e `ltrace`.
- **CI:** build, testes unitários, testes de integração e análise estática desde a primeira fase.

## 9. Riscos e respostas

| Risco | Resposta |
|---|---|
| Escopo explode com APIs | Compatibilidade publicada e aplicações-alvo antes de implementar novas funções. |
| ABI e pilha incorretas | Pontes isoladas, testes de chamada e documentação por fronteira. |
| PE malformado causa falha | Validação rigorosa de offsets/tamanhos, fuzzing e sanitizers. |
| Binário de teste traz dependências ocultas | Fixtures sem CRT e relatório automático de imports. |
| Semântica difere do Linux | Contratos explícitos, `GetLastError`, testes de regressão e limitações declaradas. |
| Execução de software malicioso | Não prometer isolamento; considerar sandbox Linux como recurso futuro independente. |

## 10. Critérios de sucesso

- **Marco 1:** o leitor identifica corretamente um PE32+ válido e rejeita entradas inválidas com segurança.
- **Marco 2:** uma imagem mínima é mapeada, relocada e tem seus imports resolvidos sem executar código fora do escopo.
- **MVP:** `./tradutorlinux tests/samples/tl_hello.exe` escreve a saída correta e encerra com o código esperado.
- **Qualidade do MVP:** todas as APIs usadas pelo exemplo possuem testes; imports ausentes são diagnosticados; o trace é reproduzível.
- **Expansão:** cada nova aplicação suportada entra na matriz de compatibilidade e na suíte de regressão.

## 11. Exemplo de uso futuro

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
