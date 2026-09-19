# Roadmap atual — TradutorLinux

## Estado do documento

Este é o roadmap de trabalho atual do TradutorLinux. Ele define as próximas
etapas de desenvolvimento e estabilização do runtime a partir de 2026-09-19.

O histórico das etapas concluídas está preservado em:
- [feitos/ROADMAP-R1-R24.md](feitos/ROADMAP-R1-R24.md) — Auditoria e ciclo R1 a R24 (2026-09-12 a 2026-09-15);
- [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md) — Fases fundacionais e marcos legados.

As regras de escopo continuam rigorosas: nenhuma capacidade deve ser declarada
sem teste de integração e registro na matriz de compatibilidade em
[`docs/compatibilidade.md`](docs/compatibilidade.md).

## Regras de execução

Cada etapa deve produzir:

1. implementação mínima no runtime ou no fixture de teste;
2. teste unitário ou de robustez;
3. fixture ou integração com aplicativo-alvo quando a mudança afetar uma API
   Win32 observável;
4. atualização da documentação técnica na pasta `docs/` e da matriz de
   compatibilidade;
5. validação reproduzível no Linux x86-64;
6. um commit próprio antes do início da etapa seguinte.

Uma exportação resolvida nunca pode ser tratada como suporte funcional. Quando a
operação ainda não existir, o runtime deve retornar uma falha controlada,
atualizar o erro correspondente (`GetLastError`) e registrar a limitação no
diagnóstico.

---

## Fila de trabalho atual

### R25 — Definir chave de host de teste e contrato de assinatura para o PuTTY

**Contexto:** A etapa R24 concluiu a seleção nominal de algoritmos comuns do
`SSH_MSG_KEXINIT` entre o cliente PuTTY e o listener do smoke. O próximo passo do
transporte SSH exige que o servidor forneça uma chave de host válida para o
algoritmo negociado e prepare a troca Diffie-Hellman (`SSH_MSG_KEXDH_INIT`). O
fixture deve manter a execução controlada sem introduzir cifras reais,
autenticação ou código específico do PuTTY no runtime.

**Tarefas:**

- [ ] definir uma chave de host de teste determinística compatível com os
  algoritmos comuns negociados em R24 (ex.: `rsa-sha2-256` ou `ssh-ed25519`);
- [ ] estruturar a resposta do servidor do smoke para empacotar o blob de chave
  pública de host com enquadramento SSH válido;
- [ ] receber e validar o primeiro pacote `SSH_MSG_KEXDH_INIT` (tipo 30) emitido
  pelo cliente PuTTY após o processamento da chave de host;
- [ ] responder com encerramento controlado via `SSH_MSG_DISCONNECT` e verificar
  a exibição do diálogo de erro esperado;
- [ ] atualizar os contratos em `docs/arquitetura/aplicativos-bloqueios.md` e a
  matriz em `docs/compatibilidade-aplicativos.md`.

**Critério de aceite:** O listener deve validar o banner, trocar `KEXINIT` com os
algoritmos selecionados, fornecer a chave de host de teste, observar o pacote
`SSH_MSG_KEXDH_INIT` do PuTTY e encerrar com desconexão controlada. O smoke deve
confirmar o diálogo fatal do PuTTY no X11 e manter a limitação reproduzível
`guest-timeout 72` sem regressões.

---

### R26 — Despacho polimórfico e ampliação de cleanups C++ FH4

**Contexto:** As etapas R6 e R7 implementaram decodificação host-side de
cabeçalhos FH4, despacho de catches tipados e execução do prefixo seguro de até
quatro cleanups de término observados no Notepad++. O aplicativo possui uma
cadeia mais longa de destruição observada (até 13 estados) que atualmente aciona
o limite protetivo `fh4-cleanup-limit`.

**Tarefas:**

- [ ] analisar os estados subsequentes da cadeia de cleanups do frame-alvo com
  fixture reproduzível de C++ EH;
- [ ] validar com segurança os funclets de destruição seguintes, assegurando o
  alinhamento estrito de 16 bytes da pilha e a preservação de registradores não-voláteis;
- [ ] estender o limite seguro de cleanups de 4 para a profundidade observada,
  mantendo a proteção contra ciclos infinitos e mapas corrompidos;
- [ ] verificar a integridade da stack no retorno via `catchret` e a transição
  estável para o fluxo seguinte do Notepad++;
- [ ] registrar os novos eventos e limites em `docs/arquitetura/unwinding-x64.md`
  e `docs/diagnostico-runtime.md`.

**Critério de aceite:** O smoke `notepadpp_fh4_headless_smoke` deve executar a
cadeia estendida de cleanups sem atingir `fh4-cleanup-limit`, mantendo exit code
`0`, ausência de `guest-signal 71` e sem corrupção do frame do chamador.

---

### R27 — Expansão do modelo de controles comuns Win32 (`SysListView32` / `COMCTL32`)

**Contexto:** O 7-Zip File Manager (`7zFM_x64.exe`) e outros utilitários Win32
dependem de controles comuns de lista para exibição e navegação de itens. O
runtime possui modelo lógico para `SysTreeView32`, toolbars e caixas de diálogo,
mas a interação avançada de listagem exige tratamento das mensagens básicas
`LVM_*` para evitar consultas sem resposta ou ponteiros nulos.

**Tarefas:**

- [ ] inventariar as mensagens `LVM_*` consumidas pelo 7-Zip File Manager no fluxo
  de visualização de pastas (`LVM_GETITEMCOUNT`, `LVM_GETITEMW`, `LVM_SETITEMSTATE`);
- [ ] implementar tratamento controlado dessas mensagens no controle lógico de
  lista do runtime (`src/runtime/gui_controls.cpp`);
- [ ] assegurar que cópias de texto e estruturas `LVITEMW` utilizem a primitiva
  segura de memória convidada (`write_guest_memory`);
- [ ] adicionar testes unitários cobrindo o controle em `tests/test_gui_controls.cpp`;
- [ ] atualizar a documentação de controles em `docs/arquitetura/gui-x11.md`.

**Critério de aceite:** O controle de lista responde às mensagens de contagem e
obtenção de item sem expor memória inválida do host; o smoke visual do 7-Zip sob
Xvfb continua terminando com sucesso (`exit 0`).

---

## Fora desta rodada

Continuam estritamente fora do escopo desta rodada:

- otimizações sem benchmark reproduzível;
- grandes refatorações estruturais ou reescrita de subsistemas estáveis;
- suporte a binários PE32 de 32 bits (x86), ARM ou WOW64;
- emulação de drivers de kernel, serviços Windows, anticheat e DirectX nativo;
- inclusão de DLLs binárias ou shims proprietários fora de `compat/apps/<app-id>/`.

---

## Referências

- [PROJETO.md](PROJETO.md) — Missão, escopo e limites do produto;
- [docs/compatibilidade.md](docs/compatibilidade.md) — Matriz geral de compatibilidade;
- [docs/compatibilidade-runtime.md](docs/compatibilidade-runtime.md) — Contratos do runtime;
- [docs/arquitetura/aplicativos-bloqueios.md](docs/arquitetura/aplicativos-bloqueios.md) — Histórico de bloqueios e evidências de rede/GUI;
- [docs/arquitetura/unwinding-x64.md](docs/arquitetura/unwinding-x64.md) — Contrato de unwinding e C++ EH;
- [feitos/ROADMAP-R1-R24.md](feitos/ROADMAP-R1-R24.md) — Registro das etapas concluídas R1 a R24;
- [feitos/ROADMAP-LEGADO.md](feitos/ROADMAP-LEGADO.md) — Histórico legado do projeto.
