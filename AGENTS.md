# Diretriz do agente — TradutorLinux

## Missão do projeto

O TradutorLinux é um runtime de compatibilidade educacional e funcional para executar, de forma progressiva, **classes cada vez mais amplas de aplicativos Win32** no Linux, sem máquina virtual e sem emular a CPU.

O alvo inicial é estrito: **executáveis PE32+ x86-64 em Linux x86-64**. A meta estratégica é tornar o runtime útil para uma variedade crescente de aplicativos Windows, começando por programas selecionados e avançando por classes de uso, sem prometer compatibilidade universal imediata.

Antes de iniciar qualquer trabalho, leia `PROJETO.md`, `ROADMAP.md`, `docs/compatibilidade.md` e os contratos técnicos relevantes em `docs/`.

## O que mantém o projeto no rumo

- Trabalhe somente na fase atual ou em uma dependência direta dela descrita no roadmap.
- Prefira um subconjunto pequeno, bem definido e testado de comportamento Win32 a uma lista grande de APIs parcialmente implementadas.
- Nunca alegue que um executável, DLL ou API é suportado sem teste de integração e registro na matriz de compatibilidade.
- Quando algo ainda não for suportado, falhe de forma controlada: informe módulo, símbolo, mecanismo e próxima limitação conhecida no trace ou na mensagem de erro.
- Cada API nova precisa ter um aplicativo-alvo ou fixture que justifique sua existência e um teste de regressão que a proteja.
- Priorize correção do loader, ABI, memória, imports e diagnósticos antes de ampliar a cobertura de APIs, famílias de DLL ou GUI.

## Escopo atual

Em cada momento, `ROADMAP.md` é a fonte de verdade para a fase em andamento. As restrições de cada fase são gates reais: uma capacidade só pode ser usada ou declarada quando a fase correspondente e seus testes a autorizarem. Na Fase 0, por exemplo, o projeto fornecia apenas infraestrutura; o carregamento e a execução de PE só começaram nas fases seguintes.

O primeiro marco funcional do projeto foi um `tl_hello.exe` próprio, sem CRT, que importa somente:

- `KERNEL32.dll!GetStdHandle`
- `KERNEL32.dll!WriteFile`
- `KERNEL32.dll!ExitProcess`

Esse marco continua protegido por testes automáticos de saída, código de retorno, trace e APIs usadas.

## Fora de escopo sem autorização explícita

- Declarar suporte ou executar de forma presumidamente compatível um aplicativo Windows arbitrário sem análise de imports, implementação, teste de integração e registro na matriz.
- PE32/x86, ARM, WOW64, emulação de CPU ou execução cruzada de arquitetura.
- .NET, COM, ActiveX, drivers, serviços Windows, anticheat, DirectX, GPU, áudio, jogos e GDI completo permanecem fora do escopo atual; qualquer inclusão futura exige decisão explícita, aplicativo-alvo e testes.
- GUI Win32 ampla sem aplicativo-alvo e sem uma fase que defina seus contratos; a GUI mínima experimental já faz parte do roadmap.
- Adicionar dependências grandes, frameworks, APIs ou compatibilidade de terceiros apenas por antecipação.
- Tratar execução nativa de `.exe` como recurso de segurança. O runtime não é sandbox.

Compatibilidade ampla é o objetivo de longo prazo, não uma autorização para implementar APIs sem alvo. Cada novo aplicativo ou classe de aplicativos deve avançar por etapas verificáveis, com limitações publicadas e nível de compatibilidade explícito.

## Regras técnicas

- Use C++20 como linguagem principal. Use C somente em estruturas binárias ou fronteiras com ABI simples; use assembly x86-64 apenas quando atributos do compilador não forem suficientes.
- Mantenha toda fronteira entre ABI Microsoft x64 e System V AMD64 explícita, pequena e testada. Nenhuma exceção C++ pode atravessá-la.
- Trate cada arquivo PE como entrada hostil: valide tamanhos, offsets, alinhamentos, inteiros e permissões antes de acessar ou mapear qualquer dado.
- Não execute o entry point antes de mapear a imagem, aplicar relocations, resolver imports e preparar o contexto mínimo definido para a fase.
- Não deixe memória executável gravável por conveniência. Respeite permissões de seção e o princípio do menor privilégio.
- Use tipos de largura fixa para dados binários e documente toda conversão entre representações Windows e Linux.
- Não copie código de Wine ou de outro runtime. Referencie comportamento por documentação pública e testes; antes de reutilizar código de terceiros, revise sua licença.

## Qualidade, testes e documentação

- Todo código novo deve compilar com os warnings rigorosos configurados pelo projeto e passar em CTest.
- Antes de declarar uma mudança pronta, execute os presets relevantes, incluindo `sanitize` quando houver código C/C++ novo.
- Preserve `stdout` para a saída do futuro programa convidado; logs do runtime usam `stderr` e o formato de `docs/diagnostico.md`.
- Atualize `docs/compatibilidade.md` quando uma fixture, aplicação ou API mudar de estado.
- Atualize documentos de ABI, API ou diagnóstico sempre que um contrato mudar.
- Marque itens no `ROADMAP.md` somente depois de haver evidência reproduzível: código, teste e validação no ambiente Linux/CI.

## Forma de trabalhar

1. Confirme a fase e os critérios de saída no roadmap.
2. Faça a menor alteração que avance o próximo resultado verificável.
3. Adicione ou ajuste testes e diagnósticos junto com a implementação.
4. Valide no Linux x86-64 e registre limitações honestamente.
5. Pare e peça direção antes de expandir o escopo além dos documentos do projeto.

Se uma solicitação conflitar com estas diretrizes, explique o conflito, proponha o menor ajuste que preserve a missão e espere decisão do usuário antes de ampliar o escopo.
