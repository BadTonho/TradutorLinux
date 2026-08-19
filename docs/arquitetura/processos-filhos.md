# Processos filhos convidados

`CreateProcessW` aceita somente caminhos relativos sem drive e PE32+ x86-64.
O runtime cria um processo Linux filho, que lê o arquivo, executa `parse_pe`,
`prepare_process`, relocations e resolução de imports antes de chamar o entry
point. O pai recebe um handle opaco e um pipe com o código de saída.

`WaitForSingleObject`, `GetExitCodeProcess` e `TerminateProcess` operam sobre
esse handle. O contrato atual não implementa ambiente, diretório de trabalho,
herança de handles, thread de inicialização separada ou execução de programas
Windows pelo loader do Linux. Falha de parse/import não executa o entry point e
vira código de falha do processo filho.
