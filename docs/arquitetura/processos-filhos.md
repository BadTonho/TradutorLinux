# Processos filhos convidados

`CreateProcessA/W` aceita PE32+ x86-64 por caminho relativo ou por caminho
absoluto `C:\...` no prefixo ativo. Um `current_directory` não nulo é aceito
somente quando resolve para um diretório dentro de `drive_c`.
O runtime cria um processo Linux filho, que lê o arquivo, executa `parse_pe`,
`prepare_process`, relocations e resolução de imports antes de chamar o entry
point. O pai recebe um handle opaco e um pipe com o código de saída.

`WaitForSingleObject`, `GetExitCodeProcess` e `TerminateProcess` operam sobre
esse handle. O filho herda o prefixo ativo, a cópia atual do ambiente Win32 do
pai e, se informado, inicia no `current_directory`; os demais parâmetros de
ambiente explícito, herança de handles, flags de criação e thread de
inicialização separada permanecem fora do subconjunto e falham com
`ERROR_INVALID_PARAMETER`. Falha de parse/import não executa o entry point e
vira código de falha do processo filho.

O contexto de console da Fase 13.8 é comum às threads de um processo, mas cada
processo convidado começa com stdin/stdout/stderr associados aos descritores
Linux herdados no `fork`. Como a herança Win32 explícita ainda não é aceita,
alterações feitas por `SetStdHandle` no pai não são promovidas a contrato de
`CreateProcessA/W` nesta fase.
