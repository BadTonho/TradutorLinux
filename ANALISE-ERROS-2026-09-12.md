# Triagem de erros — TradutorLinux — 2026-09-12

Análise estática por leitura, sem build e sem execução adicional. O objetivo é
separar defeitos observáveis de hipóteses que ainda precisam de fixture ou
teste. As referências usam símbolos e arquivos; números de linha são apenas
indícios e podem mudar.

## Regra de classificação

- **Confirmado estaticamente:** o código observado viola um contrato local claro
  ou produz falso-sucesso sem depender de uma condição externa.
- **Alta prioridade — validar:** há uma fronteira de segurança/correção
  plausivelmente vulnerável, mas é necessário um teste mínimo para provar o
  caminho completo.
- **Problema documental:** o comportamento pode ser intencional, mas a matriz,
  o trace ou a nomenclatura induz a uma conclusão errada.
- **Fora desta análise:** observação de build, ambiente ou performance que não é
  defeito do runtime por si só.

## Loader, PE e isolamento

| ID | Classificação | Achado e próxima evidência |
|---|---|---|
| E1 | **Corrigido e protegido** | O caminho de delay-import agora propaga para `ResolveResult` qualquer falha de `patch_address`. `ImportResolverTest.PropagatesInvalidDelayIatSlotToOverallStatus` cobre IAT fora da imagem e os 16 testes da suíte passaram no Debug. |
| E2 | **Corrigido e protegido** | `GuestModuleGraph::is_guest_executable` agora exige que a VA pertença à própria imagem convidada do callback ou `DllMain` antes de consultar permissões `x`. A regressão `ModuleGraphTest.RejectsTlsCallbackOutsideGuestImageBeforeInvocation` confirma rejeição sem invocar uma função executável do host. |
| E3 | **Corrigido e protegido** | A coleta de callbacks TLS agora é limitada a 4096 entradas e rejeita uma tabela excessiva com `ParseStatus::Malformed`. A regressão `PeReaderTest.RejectsExcessiveTlsCallbacks` cobre 4097 callbacks sem terminador. |
| E4 | **Corrigido e protegido** | Diretórios TLS truncados agora retornam `ParseStatus::Malformed`, assim como `.pdata` que ocupa uma região virtual sem dados físicos. As regressões `PeReaderTest.RejectsTruncatedTlsDirectory` e `PeReaderTest.RejectsVirtualOnlyExceptionDirectory` distinguem corrupção da ausência legítima do diretório. |
| E5 | **Corrigido e protegido** | A validação de `CHAININFO` resolve alvos ordenados em `O(log n)` e detecta ciclos em uma passagem, evitando buscas aninhadas quadráticas. `PeReaderTest.ParsesLargeChainedUnwindTable` cobre 65.536 `RUNTIME_FUNCTIONs` encadeadas. |
| E6 | **Alta prioridade — validar** | O isolamento de sinais e o encerramento por timeout precisam provar que o filho não continua corrompido e que toda a árvore de processos é encerrada. Criar fixture de sinal e de `CreateProcess` filho, verificando `pgid`, reentrega do sinal e ausência de processo residual. |
| E7 | **Alta prioridade — validar** | Threads secundárias ignoram o resultado de `ARCH_SET_GS`, aceitam apenas endereço legível e não usam a mesma proteção de stack do caminho principal. Fixture deve exigir falha controlada para start não executável e erro de inicialização do TEB. |
| E8 | **Problema de robustez — validar** | `file_bytes` retém cópias integrais de DLLs e o grafo aceita crescimento sem limite evidente. Medir memória e criar limite de módulos/bytes antes de transformar a otimização em mudança de contrato. |
| E9 | **Corrigido e protegido** | O cálculo do bloco UTF-16 agora verifica overflow da soma e do tamanho em bytes, e o allocator captura falhas antes de retornar `nullptr` pela fronteira `noexcept`. `Win32EnvTest.EnvironmentBlockSizeRejectsCheckedOverflow` cobre soma e multiplicação impossíveis. |
| E10 | **Alta prioridade — validar** | `image_mapper.cpp` torna memória de imagem gravável durante patches. É necessário testar concorrência, páginas compartilhadas por IAT/código e restauração de permissões antes de decidir o mecanismo seguro de relocação/imports. |
| E11 | **Risco de robustez — validar** | A validação por `/proc/self/maps` é uma fotografia e a string UTF-16 não exige alinhamento de 2 bytes. Criar teste de ponteiro desalinhado e documentar o limite da validação; a correção de TOCTOU exige desenho próprio. |
| E12 | **Confirmado como lacuna de validação — validar regressão** | O parser não rejeita cedo `SectionAlignment`/`FileAlignment` inválidos nem entry point fora de `SizeOfImage`. Fixtures PE32/PE32+ malformadas devem receber `malformed` antes do mapeamento. |
| E13 | **Confirmado como risco de diagnóstico — validar regressão** | `resource_inspector.cpp` usa zero como retorno de leitura fora dos limites e segue somente a primeira entrada de cada nível. Recursos truncados e múltiplas entradas devem ser testados contra falso recurso válido. |
| E14 | **Confirmado como lacuna de validação — validar regressão** | `inspect_pe_mitigations` precisa validar o magic do optional header antes de ler `DllCharacteristics`. Um PE32 fornecido ao caminho PE32+ deve ser rejeitado, não interpretado com offsets x64. |

## Falso-sucesso de APIs

Este grupo é um problema real de classificação e diagnóstico. Uma exportação
resolvida não deve parecer `Full` quando retorna dados inventados ou sucesso
sem executar a operação.

- **Alta prioridade:** revisar os registros de `DWMAPI`, `UxTheme`, `COMDLG32`,
  `IMM32`, `version`, `gdiplus`, `MPR`, `SHELL32`, `SHLWAPI`, `USER32` menu e
  APIs de disco. Cada export deve ser explicitamente `Full`, `Limited` ou
  `Stub`, de acordo com fixture e comportamento real.
- **Confirmado como caso de falso-sucesso:** `CRYPT32!CertNameToStrW` não pode
  retornar um CN fixo para parâmetros arbitrários; precisa validar contexto,
  flags e buffer, ou retornar limitação/erro.
- **Confirmado como caso de falso-sucesso:** `DBGHELP!SymFromAddr` não deve
  retornar ponteiro nulo com sucesso. O mesmo vale para `SHBrowseForFolder` e
  qualquer API que devolva handle/ponteiro fictício.
- **Alta prioridade:** módulos que falham sem atualizar `GetLastError` precisam
  de testes de erro. `IMM32`, `DWMAPI`, `WINMM`, `COMDLG32` e `version` não
  podem deixar o erro anterior do convidado parecer a causa atual.
- **A validar por módulo:** alguns stubs de D3D/SetupAPI já retornam erro
  explícito. Eles não devem ser rebaixados automaticamente se esse é o contrato
  documentado; devem apenas ser separados de `runtime-support: full`.

## Relatório, matriz e fixtures

Este é um **problema documental confirmado**: `result: supported` no relatório
significa resolução de imports, não execução nem suporte funcional. A saída e a
matriz devem mostrar campos distintos para `imports`, `runtime-support`,
`execution` e `functional-level`.

Também é necessário corrigir a nomenclatura de fixtures sintéticas que se
parecem com aplicativos reais e atualizar referências a fixtures que mudaram de
nome. A existência de `tl_version`, `tl_shell`, `tl_network_loopback`,
`tl_trust` e demais alvos deve ser conferida no CMake antes de ser citada como
evidência.

## Unicode e caminhos

Estes itens são riscos reais, mas não devem ser marcados como bugs confirmados
sem regressão específica:

- fallback silencioso de páginas não suportadas em `WideCharToMultiByte`;
- cálculo de offsets em UTF-8 dentro de `StrStrIW`;
- resolução de caminho relativo dependente do CWD do host;
- fallback de módulo sem evento de trace;
- distinção entre arquivo INI ausente e chave ausente.

Cada item precisa de teste ANSI/Unicode, caminho fora do prefixo ou erro de INI,
conforme o caso, e de um contrato explícito antes da implementação.

## Build, CI e ambiente

São problemas de manutenção ou melhorias de infraestrutura, não bugs de
execução confirmados:

- Qt obrigatório para o CLI;
- download não raso do GoogleTest;
- diretórios de build fora dos presets;
- `compile_commands.json` apontando sempre para `debug`;
- jobs Xvfb/Proton e limites de paralelismo não alinhados com `AGENTS.md`;
- artefatos gerados ou pacotes eventualmente rastreados pelo Git.

Esses itens entram no novo roadmap somente depois de confirmar o estado atual no
CI e no índice do Git. O arquivo não autoriza remover artefatos ou mudar o build
por inferência.

## Ordem recomendada para o novo roadmap

1. E1, E2, E3, E9, E12, E13 e E14, sempre com fixtures de rejeição.
2. E4, E5, E6, E7, E8, E10 e E11, após reproduções controladas.
3. Reclassificação de stubs e correção de `GetLastError`.
4. Separação de `imports`, execução e nível funcional no relatório/matriz.
5. Unicode, caminhos, CI e performance, cada qual com teste ou medição própria.
