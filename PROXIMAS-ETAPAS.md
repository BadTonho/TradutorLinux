# Próximas etapas — continuidade no Linux real

Atualizado em 2026-09-04.

Este arquivo registra a sequência de trabalho para retomar o projeto no Linux real. Nesta etapa, foi salvo apenas este plano; não foi feito novo build, alteração de código ou commit.

## Estado atual

- Último commit: `1001f06 feat: add Linux network and session subset`.
- O alvo continua sendo PE32+ x86-64 em Linux x86-64.
- Já foram implementados e testados no build Debug Linux os subconjuntos de `IPHLPAPI`, `WTSAPI32` e parte de `CRYPT32`, além das correções de forwarders, TLS genérico e parser de manifests MSIX.
- A validação executada até aqui cobriu fixtures próprias, testes unitários direcionados e traces de `tl_worker_rsl` e `tl_powr`.
- Isso ainda não autoriza declarar suporte completo a Roblox, Worker/RSL ou qualquer aplicativo comercial. A matriz de compatibilidade deve continuar refletindo o resultado real de execução ponta a ponta.
- Dependências que foram necessárias no ambiente Linux anterior: `zlib1g-dev` e `xvfb`. No Linux real, confirme também CMake, compilador C++20, ferramentas PE/MinGW e Qt/X11 exigidos pelo projeto.

## Ordem de execução

### 1. Reproduzir o baseline no Linux real

No diretório do projeto:

```bash
git status --short
git log -5 --oneline
```

Reutilize o build Debug existente, se ele já estiver configurado. Se não estiver, configure-o conforme os presets e instruções do projeto. Não crie um preset novo sem necessidade.

Compile somente os alvos inicialmente afetados, com baixo paralelismo:

```bash
cmake --build build/debug --target tradutorlinux_unit_tests tradutorlinux_samples --parallel 2
```

Rode os testes da etapa atual:

```bash
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='IphlpapiTest.*:WtsApiTest.*:Crypt32Test.CertContextAndStoreManagement:MsixParserTest.*'
ctest --test-dir build/debug -R '^(fixture_tl_powr_metadata|runtime_tl_powr_matches_readobj|fixture_tl_worker_rsl_metadata|runtime_tl_worker_rsl_matches_readobj)$' --output-on-failure
```

### 2. Validar definitivamente o caminho X11 e os popups

Depois de confirmar que `xvfb` está instalado, reconfigure o build para que o CMake detecte o executável:

```bash
cmake -S . -B build/debug
cmake --build build/debug --target x11_popup_smoke --parallel 2
ctest --test-dir build/debug -R '^x11_popup_smoke$' --output-on-failure
ctest -N | grep -E 'runtime_gui_smoke|simple_todo_gui_smoke|x11_popup_smoke'
```

Se os testes de GUI aparecerem na lista, execute-os também. Só rode a variante `sanitize` se ela já existir configurada; não a configure automaticamente em uma máquina limitada.

Após a validação, atualizar `ANALISE-CRITICA.md`, `ROADMAP.md` e `docs/compatibilidade.md` caso ainda indiquem que esta validação está pendente.

### 3. Fechar a validação do parser MSIX/DEFLATE

```bash
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='MsixParserTest.*'
ctest -N | grep -Ei 'msix|affinity|package'
ctest --test-dir build/debug -R 'msix|affinity|package' --output-on-failure
```

O objetivo desta etapa é inspecionar manifests e dependências com segurança. Não instalar nem executar .NET/MSIX como se isso fosse suporte de execução.

### 4. Validar forwarders, imports dinâmicos e delay-import

```bash
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='ModuleTest.*:ImportResolverTest.*'
ctest -N | grep -Ei 'delay|dynload|report'
ctest --test-dir build/debug -R 'delay_import|dynload|report.*delay' --output-on-failure
```

Confirmar especialmente:

- forwarder encadeado, forwarder ausente e ciclo terminam com diagnóstico controlado;
- a política atual de delay-import continua explícita e testada;
- `GetProcAddress` não seja tratado como busca global correta quando a identidade da DLL for necessária;
- o trace informe módulo, símbolo, mecanismo e limitação conhecida.

### 5. Validar TLS genérico

```bash
ctest --test-dir build/debug -R '^(fixture_tl_tls_generic_metadata|runtime_tl_tls_generic_matches_readobj|report_tl_tls_generic_support)$' --output-on-failure
./build/debug/tests/tradutorlinux_unit_tests --gtest_filter='Win32ConcurrencyTest.PointerBackedTlsSlotIsZeroInitializedAndReleased'
```

Se tudo passar no Linux real, atualizar a pendência de TLS em `ANALISE-CRITICA.md`, `ROADMAP.md` e na documentação correspondente. A variante `sanitize` só deve ser usada se já estiver configurada.

### 6. Reexecutar o Worker/RSL real com trace

Primeiro localizar o executável disponível no checkout:

```bash
find Aplicativos_Windows_Populares -type f -iname '*worker*.exe' -o -type f -iname '*rsl*.exe'
```

Então executar somente após as validações anteriores:

```bash
./build/debug/src/tradutorlinux --trace=pe,loader,imports,runtime,process CAMINHO_DO_EXECUTAVEL.exe
```

Os canais válidos do CLI devem ser conferidos em `--help`; referências antigas a `ws2` ou `crypt` não devem ser copiadas sem verificar o parser atual. Registrar o caminho exato, stdout, exit code e o primeiro bloqueio reproduzível.

Não mudar o estado de compatibilidade de um aplicativo comercial apenas por resolver imports ou iniciar o processo. Para cada nova API necessária, seguir o ciclo: aplicativo-alvo ou fixture, implementação mínima, teste de regressão, trace, atualização da matriz e documentação.

### 7. Manutenção após fechar os bloqueios funcionais

Somente depois das validações acima:

- dividir `tests/test_win32.cpp` em suítes por domínio, preservando nomes e cobertura;
- eliminar helpers e constantes duplicados;
- auditar APIs marcadas como `ExportSupport::Stub`, adicionando testes de retorno, `LastError`, buffers de saída e trace;
- revisar limites de recursos do processo convidado, se a decisão de escopo for aprovada;
- corrigir contradições históricas em `docs/compatibilidade.md`, especialmente linhas que dizem “suportado” enquanto a matriz atual registra `execution-failed`;
- atualizar medições e o catálogo de aplicativos somente com evidência reproduzível.

## Regra para cada etapa concluída

1. Alterar somente o código, testes e documentos diretamente relacionados.
2. Validar no Linux real com paralelismo máximo 2.
3. Rodar `git diff --check` e revisar `git status`.
4. Criar um commit separado por etapa concluída, sem incluir artefatos gerados.
5. Não fazer push sem solicitação explícita.

