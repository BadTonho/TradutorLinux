# Análise crítica consolidada — TradutorLinux

Data da revisão: 2026-09-04

Este arquivo reúne as pendências que ainda exigem decisão, código, teste ou
atualização documental, além das correções aplicadas nesta rodada que aguardam
validação no ambiente Linux. Os achados encerrados ficam registrados no final
para não serem reabertos por análises antigas.

O `ROADMAP.md` continua sendo a fonte de verdade para a fase do projeto. Esta
lista é o backlog consolidado da auditoria técnica e documental. Nenhuma
entrada aqui autoriza declarar compatibilidade ampla: cada item precisa de
implementação, regressão e evidência no aplicativo-alvo correspondente.

## Pendências atuais

### P0 — segurança, estado e robustez

#### P0.1 — Relatório das APIs remotas (corrigido nesta rodada)

`CreateRemoteThread` e `WriteProcessMemory` continuam falhando de forma
controlada com `ERROR_NOT_SUPPORTED`, e agora estão marcadas como
`ExportSupport::Stub` em `src/runtime/dlls/kernel32/module.cpp`. O `--report`
e os testes de registro não devem mais classificá-las como `full`.

Critério atendido para as duas exports: o registro e a regressão em
`tests/test_module.cpp` confirmam `support=stub`. A revisão sistemática de
outras exports que retornam sucesso sintético permanece coberta pela P2.5.

#### P0.2 — Estado modal e tabelas de USER32 com afinidade explícita

O runtime agora restringe o estado de USER32/GUI ao thread convidado principal
(`g_current_thread_id == kMainThreadId`). Chamadas de outros threads falham com
`ERROR_NOT_SUPPORTED` antes de acessar `g_windows`, `g_classes`, filas, foco,
captura, menus ou estado modal. `g_modal_*` continua protegido por
`g_modal_mutex` nas transições do diálogo; a expansão para janelas em threads
distintos permanece fora do contrato atual.

Critérios de conclusão:

- documentar quais operações podem ser chamadas pelo thread GUI principal;
- rejeitar chamadas de outros threads antes de tocar o estado compartilhado;
- proteger todas as leituras e escritas do estado modal com o mesmo protocolo;
- manter ponteiros de `g_windows` restritos ao thread proprietário e ao ciclo de
  vida documentado;
- cobrir a rejeição cross-thread com teste de regressão e, quando disponível,
  ThreadSanitizer.

#### P0.3 — Inspeção MSIX (implementada nesta rodada; falta validar no Linux)

`src/package/msix.cpp` agora limita o manifesto a 16 MiB, impõe limites de
entradas, nomes e tamanho descompactado acumulado, rejeita traversal, entradas
criptografadas, ZIP64 e tamanhos inconsistentes, valida CRC e captura
`bad_alloc`. A leitura usa a central directory, aceita manifesto DEFLATE e
data descriptors com os tamanhos autenticados pelo registro central.

O parser XML agora valida aninhamento, comentários, CDATA, namespaces por nome
local, aspas simples/duplas e entidades XML sem resolver DTDs ou recursos
externos. As regressões cobrem manifesto DEFLATE em central directory, data
descriptor, CRC, traversal, truncamento, manifesto acima do limite e excesso
de entradas. A execução no Linux/CTest ainda é necessária para fechar a
validação desta etapa.

#### P0.4 — Popup X11 (cancelamento temporal implementado; falta validar)

`src/gui/x11.cpp::track_popup_menu` agora trata Escape, `DestroyNotify`, clique
fora do menu e timeout configurável por `TL_GUI_POPUP_TIMEOUT_MS` (30 segundos
por padrão, no máximo 10 minutos). A implementação libera grabs e destrói a
janela também no timeout. Continua pendente a validação de integração sob Xvfb
para todos os caminhos de encerramento; o smoke `x11_popup_smoke` agora cobre
Escape, clique externo, destruição externa e timeout.

Critérios de conclusão:

- validar o timeout configurável, o cancelamento por evento e a API não
  bloqueante sob Xvfb;
- manter a garantia de liberação de grabs e da janela em todos os caminhos;
- adicionar teste sob Xvfb para Escape, clique externo, timeout e destruição.

### P1 — compatibilidade e decisões de arquitetura

#### P1.1 — Delay imports continuam resolvidos antecipadamente

`src/loader/import_resolver.cpp` resolve e grava toda a delay IAT antes do
entry point. O comportamento está documentado em
`docs/arquitetura/imports.md` e protegido pela fixture `tl_delay_import.exe`,
mas diverge da resolução sob demanda do Windows e pode impedir a inicialização
quando uma API atrasada não é usada.

Critérios de conclusão:

- decidir formalmente entre manter o subconjunto eager ou implementar resolução
  lazy;
- se permanecer eager, expor essa limitação no relatório/matriz e não chamá-la
  de equivalência Win32;
- se for implementada resolução lazy, criar fixture para import atrasado não
  usado, usado e ausente, com diagnóstico por símbolo.

#### P1.2 — Forwarders reais ainda não têm cadeia de múltiplos saltos

`src/loader/module.cpp` resolve API Sets e o alias `KERNELBASE -> KERNEL32`,
mas `ExportedFunction` não representa um forwarder textual
`DLL.Símbolo`. Portanto, a implementação não prova uma cadeia
forwarder -> forwarder; a documentação deve distinguir API Set mapping de
forwarder real.

Critérios de conclusão:

- definir o modelo de export forwarder e limite de profundidade;
- resolver ciclos e destinos ausentes como erro controlado;
- adicionar testes para cadeia válida, ciclo, ordinal e símbolo inexistente;
- alinhar `docs/compatibilidade.md` e `docs/arquitetura/imports.md`.

#### P1.3 — TLS genérico ainda depende de correção específica do Roblox

O realocamento dos endereços de callbacks TLS já foi corrigido nos fluxos
normal e filho. A pendência diferente é o slot TLS zero-initializado usado pelo
benchmark Roblox: `ROADMAP.md` registra o fix específico do slot `0x430` e
mantém aberto um alocador genérico para slots sem dados iniciais.

Critérios de conclusão:

- implementar alocação sob demanda somente para slots/RVAs válidos da imagem;
- registrar e liberar o bloco no ciclo de vida da thread convidada;
- criar `tl_tls_generic.exe` e regressão de zero-init, leitura e encerramento;
- manter Roblox como benchmark e só mudar seu estado após execução reproduzível
  do fluxo definido no roadmap.

#### P1.4 — Concorrência geral do backend GUI restrita no escopo atual

O `ready flag` antigo do X11 foi substituído por `std::call_once`, e o contrato
atual de USER32/GUI foi declarado como thread-affine ao thread convidado
principal. O backend não aceita chamadas de GUI vindas de threads convidados
secundários; elas falham de forma controlada, sem tocar estado ou display.

Critérios de conclusão:

- documentar o modelo de threads suportado;
- impedir corrida em estado de janela, fila, display e recursos X11;
- cobrir uma chamada de GUI em thread secundário com rejeição, erro controlado
  e teste; uma futura implementação cross-thread exigirá um contrato novo.

### P2 — manutenção, testes e melhorias futuras

#### P2.1 — Vazamento de cores no X11 (corrigido nesta rodada; falta validar)

`pixel_for_rgb` agora reutiliza um cache fixo de 256 cores e libera as
alocações rastreadas com `XFreeColors` antes de fechar o display. O estado dos
brushes permanece limitado ao ciclo de vida do display. Continua pendente a
validação sob Xvfb com desenho repetido e LeakSanitizer; o smoke de popup
também exercita a inicialização e o teardown do display sob múltiplos cenários.

Critérios de conclusão:

- executar validação sob Xvfb com desenho repetido e ASAN/LeakSanitizer.

#### P2.2 — Parser XML do MSIX (implementado nesta rodada; falta validar no Linux)

`parse_appx_manifest_xml` deixou de buscar substrings e passou a usar um parser
estrutural pequeno, limitado ao contrato de metadados do AppX. Ele rejeita XML
malformado e DTDs, não busca recursos externos e decodifica apenas entidades
XML predefinidas/númericas. A regressão estrutural aguarda CTest no Linux.

#### P2.3 — Suíte de testes Win32 ainda é monolítica

`tests/test_win32.cpp` tem aproximadamente 130 KiB e concentra testes de
arquivos, GUI, catálogo, locale, memória e aplicativos. A divisão por domínio
reduziria tempo de diagnóstico e custo de execução.

Critérios de conclusão:

- separar pelo menos arquivos, memória/loader, USER32/GDI, catálogo e
  cobertura de aplicativos;
- preservar nomes e mensagens de falha úteis;
- mover helpers compartilhados para um único suporte de testes.

#### P2.4 — Helpers e constantes de teste duplicados

`maps_permissions_for` aparece em três testes. Ainda há constantes como
`259U` e `0xC002U` sem nome semântico próximo ao uso.

Critérios de conclusão:

- criar helper compartilhado;
- substituir números por constantes nomeadas ou comentários que indiquem o
  contrato Win32 testado.

#### P2.5 — Testes de stubs precisam verificar contrato negativo

Há testes que aceitam tokens sintéticos e retornos de sucesso para APIs sem
semântica real, como a cobertura de menus. Isso pode mascarar regressões e
contradiz a política de falha controlada.

Nesta rodada, as APIs de menu sem implementação real foram classificadas como
`Stub`, passaram a retornar `ERROR_NOT_SUPPORTED` (ou parâmetro inválido) e a
regressão em `tests/test_win32.cpp` verifica esses retornos. Ainda falta
revisar as demais famílias de stubs.

Critérios de conclusão:

- para cada stub, verificar retorno, `GetLastError`, buffers de saída e trace;
- não usar sucesso sintético como prova de compatibilidade comportamental;
- incluir pelo menos uma regressão para cada família marcada `Stub`.

#### P2.6 — Limites configuráveis de CPU e RAM ainda não existem

`ideia.md` propõe limitar CPU e RAM por aplicativo. O código atual implementa
isolamento de processo e timeout, mas não há limite efetivo por `setrlimit`,
cgroup ou mecanismo equivalente.

Critérios de conclusão:

- decidir se esse recurso pertence à fase atual ou a uma fase futura;
- definir CLI, herança para processos filhos, erro e observabilidade;
- validar limites de CPU, memória e timeout sem confundir isso com sandbox.

#### P2.7 — Camada genérica de tradução por aplicativo ainda não existe

O instalador copia especificamente alguns arquivos de idioma do Notepad++,
mas não existe uma camada geral para aplicar arquivos de tradução por aplicativo
sem alterar o runtime inteiro.

Critérios de conclusão:

- definir formato, diretório, seleção por ID/hash/versão e precedência;
- manter a camada isolada do loader e do comportamento Win32;
- adicionar um aplicativo externo de teste e validar fallback quando a tradução
  estiver ausente ou inválida.

## Pendências documentais

Estas são as únicas pendências documentais identificadas nesta revisão:

1. `docs/requisitos-aplicativos.md` contém medições históricas honestas, mas
   ainda precisa indicar de forma uniforme data, hash, versão do binário e se a
   medição foi apenas `--report`.

As demais inconsistências documentais desta lista foram corrigidas nesta
revisão: a matriz de compatibilidade agora separa resolução de imports,
suporte de runtime e execução testada; os caminhos antigos foram marcados como
históricos ou substituídos; e `ideia.md`, o documento de reorganização, o
`ROADMAP.md` e os contratos de ABI/GUI refletem o estado atual.

## Achados encerrados nesta revisão

Os itens abaixo não devem voltar ao backlog sem uma nova evidência:

- C1: seções `exec+write` são rebaixadas para `ReadWrite`; não há página RWX.
- C2: callbacks e campos TLS são realocados antes da execução normal e filha.
- C3: `fill_rectangle` valida índice, dimensões e o brush nulo no X11.
- C4: o backend Wayland calcula limites com inteiros de largura maior.
- C5: `CreateDIBSection` aloca pixels conforme o bitmap e impõe limite.
- C6: `GetSystemTimeAsFileTime` e `GetTextExtentPoint32W` validam destinos.
- M1: `MoveFileExA` executa rename real e trata replace/erros.
- M2: caminhos como `C:\\windows\\temp\\` são caminhos Windows virtuais
  traduzidos pelo prefixo; não são uma promessa de caminho POSIX ao guest.
- M3: `GetWindowTextA` retorna o comprimento copiado.
- M6: patches da IAT podem cobrir os headers mapeados.
- M10: IDs do catálogo rejeitam traversal e componentes inseguros.
- `unescape_json_string` trata `\\uXXXX` e pares substitutos.
- O antigo `ready flag` do X11 foi substituído por inicialização com
  `std::call_once`.

## Limitação da validação desta revisão

Esta consolidação foi feita por inspeção estática, testes existentes e
referências cruzadas. `CTest` não foi executado nesta máquina porque o cache
existente de `build/debug` foi criado em WSL e está sendo acessado pelo caminho
Windows; o ambiente WSL não estava disponível. Antes de fechar qualquer item,
executar o preset Linux/CI correspondente e registrar a evidência no roadmap e
na matriz de compatibilidade.
