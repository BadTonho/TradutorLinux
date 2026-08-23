# Processos filhos convidados

`CreateProcessA/W` aceita PE32+ x86-64 por caminho relativo ou por caminho
absoluto `C:\...` no prefixo ativo. Um `current_directory` não nulo é aceito
somente quando resolve para um diretório dentro de `drive_c`.
O runtime cria um processo Linux filho, que lê o arquivo, executa `parse_pe`,
`prepare_process`, relocations e resolução de imports antes de chamar o entry
point. O pai recebe um handle opaco e um pipe com o código de saída.

`WaitForSingleObject`, `GetExitCodeProcess` e `TerminateProcess` operam sobre
esse handle. O filho herda o prefixo ativo e, se informado, inicia no
`current_directory`; os demais parâmetros de ambiente, herança de handles,
flags de criação e thread de inicialização separada permanecem fora do
subconjunto e falham com `ERROR_INVALID_PARAMETER`. Falha de parse/import não
executa o entry point e vira código de falha do processo filho.
