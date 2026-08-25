# WinINet HTTPS de loopback

## Finalidade

O primeiro subconjunto de `WININET.dll` existe para validar um cliente HTTPS
de protocolo reproduzível, sem transformar o runtime em um cliente Web geral.
Ele atende apenas conexões diretas a um servidor no loopback dentro do processo
convidado isolado. A fixture `tl_wininet.exe` e o teste
`runtime_tl_wininet_https_loopback` iniciam um servidor TLS local com CA própria
e verificam a requisição, o cabeçalho, a resposta e a leitura parcial do corpo.

## Escopo suportado

- `InternetOpenW` aceita somente `INTERNET_OPEN_TYPE_DIRECT`, sem proxy.
- `InternetConnectW` aceita somente `INTERNET_SERVICE_HTTP`, `localhost` ou
  `127.0.0.1`, porta numérica e sem credenciais.
- `HttpOpenRequestW` aceita `GET`, `HEAD` ou `POST` sobre `HTTPS`, com
  `INTERNET_FLAG_SECURE`, caminho absoluto e versão nula ou `HTTP/1.1`.
- `HttpAddRequestHeadersW` e os cabeçalhos opcionais de `HttpSendRequestW`
  aceitam linhas ASCII limitadas a 16 KiB. `Host`, `Cookie`, `Authorization` e
  cabeçalhos `Proxy-*` são recusados.
- `HttpSendRequestW`, `InternetReadFile`, `InternetQueryDataAvailable`,
  `HttpQueryInfoW`, `InternetSetOptionW`, `InternetCloseHandle` e
  `InternetCrackUrlW` cobrem o fluxo da fixture. O corpo é limitado a 16 MiB
  e os cabeçalhos de resposta a 16 KiB.

Os handles de sessão, conexão e requisição são tokens opacos. Fechar um handle
fecha também seus descendentes. Ponteiros, strings UTF-16, buffers e tamanhos
do convidado são validados antes de serem acessados.

## Transporte e confiança

O backend carrega `libcurl` dinamicamente; se ele estiver indisponível,
`HttpSendRequestW` falha com `ERROR_MOD_NOT_FOUND`. O transporte desativa proxy
e redirecionamento, força verificação de certificado e de hostname e só admite
o CA file indicado por `TL_WININET_CA_FILE`. Essa variável é consumida pelo
host e removida do ambiente Win32 exposto ao convidado.

O CA file permite somente a fixture local. Ele não constitui loja de
certificados, política de cadeia, revogação, WinTrust ou confiança para a
Internet. Não há cookies persistentes, cache global, credenciais, proxy, DNS
externo ou conexão fora de loopback. O estado de handles não é compartilhado
entre execuções; o trace usa apenas `scope="prefix"`, nunca caminhos do host.

## Erros e diagnóstico

Hosts fora de loopback retornam `ERROR_INTERNET_NAME_NOT_RESOLVED`; URL fora
de HTTPS retorna `ERROR_INTERNET_INVALID_URL`; certificado ausente, inválido ou
rejeitado retorna `ERROR_INTERNET_SEC_CERT_INVALID`. Timeout, conexão e DNS
do backend são convertidos para os erros WinINet correspondentes.

O componente `runtime` emite `wininet` com operação, estado, porta, bytes
quando aplicável, `scheme="https"`, `destination="loopback"` e
`scope="prefix"`. O trace não inclui URL completa, cabeçalhos, certificado ou
caminho do CA.
