# Triagem dos bloqueios do corpus de aplicativos

Data: 2026-09-07

Este documento registra a triagem C1 da matriz em
`ROADMAP-APLICATIVOS.md`. Os resultados foram obtidos com Rust ON
(`build/debug-rust`) e C++ OFF (`build/debug`), usando os arquivos temporários
`/tmp/tl-matrix-b1`, `/tmp/tl-matrix-b2` e `/tmp/tl-matrix-b3`.

## Decisões

| Aplicativo | Primeiro bloqueio observado | Evidência mínima | Decisão |
| --- | --- | --- | --- |
| `Rufus_x64.exe` | loader: `entry point fora de uma página executável` | `UPX1` tem `rwx`; `map-failed` exit `4` em ambos os backends; fixtures `ImageMapperTest.DowngradesWritableExecutableSectionToReadWrite` e `ImageMapperTest.RejectsEntryPointOutsideExecutablePage` | manter W^X e a rejeição; não criar página `RWX` nem desempacotar em runtime nesta etapa |
| `HWiNFO64.exe` | parser PE: export RVA sem intervalo file-backed | `PeReaderTest.RejectsExportDirectoryWithoutFileBackedSection`; `malformed` exit `4` em ambos | manter rejeição até existir fase explícita de desempacotamento |
| `Rockstar-Games-Launcher.exe` | convidado: `ExitProcess(3)` explícito | loader, imports, TLS e contexto inicial registrados como sucesso antes do término; stdout vazio e exit `3` em ambos | não converter código do convidado em sucesso e não alterar o loader por enquanto |
| `PuTTY` | ambiente GUI: `x11/connect-failed` em `CreateWindowExA`, seguido de timeout | `/tmp/tl-matrix-b2/*/6/stderr`; o Xvfb B2 falhou antes dos aplicativos | classificar como skip ambiental; repetir com Xvfb válido antes de atribuir falha ao runtime |
| `Notepad++` | ambiente GUI não validado; SIGSEGV controlado após startup wide | `/tmp/tl-matrix-b2/*/5/stderr`; Xvfb B2 falhou antes dos aplicativos | não promover nem corrigir ainda; repetir sob Xvfb válido e isolar o primeiro evento GUI |

## Invariantes preservados

- Rust ON e C++ OFF tiveram os mesmos exit codes e stdout nos casos B2.
- Falhas de parsing e de mapeamento terminaram antes de execução do entry
  point quando essa era a causa do resultado.
- A política W^X não foi relaxada.
- Nenhum fallback Rust→C++ foi introduzido.
- Prefixos e catálogos temporários permaneceram vazios nos casos B3.

## Reprodução

Para uma nova triagem, repetir primeiro B1 e B2 com os binários já construídos.
Para casos GUI, o teste só é válido quando `xdpyinfo` confirma um Xvfb próprio
antes de iniciar o convidado. Se o servidor gráfico falhar, o resultado deve
ser registrado como skip ambiental e não como regressão funcional.

Nenhuma API nova ou mudança de loader é justificada por esta triagem. A C2
precisa de um display válido e de uma hipótese específica antes de alterar
qualquer caminho de execução.
