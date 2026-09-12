# Triagem de erros — registro histórico — 2026-09-12

## Estado

Esta triagem foi encerrada após a correção das falhas confirmadas. O arquivo
não é backlog: ele conserva somente as conclusões, as limitações ainda
conhecidas e a evidência que deve ser consultada antes de abrir um novo item.

## Falhas corrigidas e protegidas

As validações de loader, PE, memória, isolamento e recursos registradas como
`E1`–`E14` foram incorporadas ao código e aos testes. `E7` e `E11` continuam
parciais e aparecem abaixo.

Também foram eliminados falsos sucessos nas seguintes áreas:

- `CRYPT32!CertNameToStrW` deixou de fabricar um nome de certificado;
- `DBGHELP!SymFromAddr` e `SHELL32!SHBrowseForFolderW` passaram a rejeitar o
  caminho não implementado de forma explícita;
- `gdiplus.dll`, `COMDLG32.dll`, `version.dll`, `IMM32.dll`,
  `DWMAPI.dll` e `UxTheme.dll` passaram a publicar stubs controlados, limpar
  saídas válidas e informar erro em vez de devolver objetos, metadados ou
  sucesso inventados.

Cada grupo possui classificação de exportação, teste unitário e, quando
aplicável, fixture de integração. A matriz e os contratos atuais estão em
[`docs/compatibilidade.md`](docs/compatibilidade.md) e
[`docs/compatibilidade-runtime.md`](docs/compatibilidade-runtime.md).

## Limitações que continuam registradas

| Item | Estado atual | Evidência necessária para encerrar |
|---|---|---|
| `MPR.dll` | Pendência real: algumas APIs ainda retornam `NO_ERROR` sem executar a operação e `WNetOpenEnumW` publica um handle fictício. | Fixture de erro/sucesso observável e classificação explícita de cada export. |
| `E7` — falha de inicialização de thread/TEB | Parcialmente protegido; falta injetar uma falha real de `arch_prctl` em ambiente controlado. | Regressão que prove encerramento observável sem deixar estado de thread inválido. |
| `E11` — validação de memória convidada | Ponteiros UTF-16 desalinhados são rejeitados, mas a validação por `/proc/self/maps` ainda é uma fotografia sujeita a TOCTOU. | Desenho e teste de acesso protegido que eliminem ou delimitem essa janela. |

As antigas listas genéricas de `GetLastError`, Unicode, caminhos, build e CI
foram removidas daqui porque não eram defeitos confirmados. Elas só devem
voltar ao novo roadmap quando houver reprodução, escopo e critério de aceite.

## Regra para novas triagens

Uma falha nova só entra na documentação de estado quando houver comportamento
observável, teste de regressão e atualização da matriz. Resolução de import não
é evidência de execução nem de suporte funcional.
