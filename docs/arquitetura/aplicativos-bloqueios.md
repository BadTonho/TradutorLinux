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
| `Notepad++` | histórico C2: corrupção de heap após `startup-info` wide, observada como SIGSEGV ou SIGABRT controlado, mesmo com X11 válido | `/tmp/tl-matrix-c2-x11/*/5/stderr`, `/tmp/tl-matrix-c4/run/*/5.stderr`; ASan D1 identificou `tl_lstrcpyW` escrevendo 4 bytes em região guest de 2 bytes | corrigir a ABI das APIs `lstr*W` para UTF-16 de 16 bits; manter o aplicativo sem suporte funcional até haver interação/encerramento GUI reproduzíveis |

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
confirmou o comportamento sob display válido; a fixture D1 e o ASan localizaram
a causa na largura host incorreta das APIs `lstr*W`, não em
`SHGetFolderPathW`. A correção usa `std::uint16_t`, preserva W^X, isolamento e
limites, e foi repetida em Rust ON/C++ OFF e Sanitizer. O timeout restante é
uma limitação de interação GUI, não uma promoção de suporte.

## Evidência D2 — cenários GUI

O smoke externo do 7-Zip File Manager passou nos builds C++ OFF e Rust ON. Ele
abre a janela, seleciona `input.txt`, aciona `Copy`, verifica o arquivo em
`output/`, valida o evento de operação no trace e fecha a janela; os dois
processos terminaram com exit `0`. O harness agora usa `Xvfb -displayfd`, para
que a escolha do display não dependa do lock fixo `:99`.

O cenário PuTTY foi isolado em `tests/apps/putty/putty_smoke.cpp`. Sob Xvfb iniciado
com `-displayfd`, o trace registra `CreateDialogParamA`, `About PuTTY` e
`PuTTY Configuration`; o harness localiza a janela configurável e envia apenas
`WM_DELETE_WINDOW` por X11. O processo termina com exit `0` em `build/debug` e
`build/debug-rust`, sem depender da janela temporária `PuTTY: hidden timing
window`.

A correção necessária ficou restrita ao parsing de templates padrão/customizados
de diálogo, à criação de diálogos modeless com controles genéricos e à
invalidação da geração de alocações guest após `Heap/Global/LocalAlloc` e
liberações. `DestroyWindow` mantém o slot lógico válido durante `WM_DESTROY` e
somente depois o libera, evitando callback posterior com `GWLP` inválido.
Isso valida somente a abertura/fechamento da configuração; o fluxo SSH e a
compatibilidade GUI geral do PuTTY continuam fora da declaração de suporte.
