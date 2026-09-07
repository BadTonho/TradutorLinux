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
| `PuTTY` | GUI inicia `PuTTYTimerWindow`, mas permanece em execução até o timeout controlado | `/tmp/tl-matrix-c2-x11/*/6/stderr`; `xdpyinfo` confirmou Xvfb `:99` antes dos testes; sem `x11/connect-failed` | não promover como suporte concluído; tratar como cenário interativo ainda sem critério de encerramento |
| `Notepad++` | SIGSEGV controlado em endereço nulo após `startup-info` wide, mesmo com X11 válido | `/tmp/tl-matrix-c2-x11/*/5/stderr`; `xdpyinfo` confirmou Xvfb `:99` antes dos testes | classificar como bloqueio real do runtime/aplicativo; investigar a primeira operação após o startup, sem relaxar isolamento |

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
antes de iniciar o convidado. A rodada C2 confirmou esse requisito: PuTTY e
Notepad++ foram executados com X11 funcional, preservando os mesmos resultados
em Rust ON e C++ OFF. Portanto, o timeout do PuTTY e o SIGSEGV do Notepad++ não
devem ser classificados como falhas de conexão X11.

Nenhuma API nova ou mudança de loader foi justificada por esta triagem. A C2
confirmou o comportamento sob display válido; a hipótese de corrupção de heap
associada a `SHGetFolderPathW` ainda exige uma fixture mínima antes de qualquer
alteração, porque `tl_shell` já cobre o caminho básico e passa. Não se deve
relaxar W^X, isolamento ou limites para contornar os bloqueios.
