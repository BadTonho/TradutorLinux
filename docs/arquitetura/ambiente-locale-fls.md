# Ambiente, locale e FLS por processo

Este contrato define o subconjunto determinístico das Fases 13.6 e 13.7. Ele
vale para uma execução PE32+ AMD64 no Linux AMD64 e não consulta nem altera o
locale do Linux durante a execução do convidado.

## Ambiente Win32

Ao iniciar a primeira imagem de um processo convidado, o runtime copia o
ambiente do hospedeiro para um mapa case-insensitive e o sobrepõe com os
caminhos do prefixo ativo: `APPDATA`, `LOCALAPPDATA`, `USERPROFILE`, `TEMP`,
`TMP`, `HOMEDRIVE` e `HOMEPATH`. Todas as alterações posteriores ficam nesse
mapa; `setenv`, `putenv` e o ambiente do launcher nunca são modificados.

`CreateThread` compartilha esse ambiente de processo. `CreateProcessW` cria
um processo Linux filho por `fork`, portanto recebe uma cópia do mapa já
alterado, ainda isolada tanto do pai Linux quanto de outras execuções.

| API | Contrato suportado |
|---|---|
| `GetEnvironmentVariableA/W` e `msvcrt!getenv` | Consultam o mapa por nome case-insensitive. Consulta de tamanho inclui o NUL; cópia bem-sucedida retorna o comprimento sem o NUL; variável ausente retorna `0` e `ERROR_ENVVAR_NOT_FOUND`. |
| `SetEnvironmentVariableW` | Nome sem `=`; valor `NULL` remove, valor vazio continua definido. A alteração não toca o host. |
| `GetEnvironmentStringsW` | Devolve uma cópia UTF-16 `NOME=VALOR\0...\0\0`, ordenada case-insensitivamente e pertencente ao runtime. |
| `FreeEnvironmentStringsW` | Só aceita uma cópia ainda viva de `GetEnvironmentStringsW`; `NULL`, ponteiro estranho ou segundo free retornam `FALSE` e `ERROR_INVALID_PARAMETER`. |
| `ExpandEnvironmentStringsW` | Expande `%NOME%` sem diferenciar maiúsculas/minúsculas; variável desconhecida fica literal. Consulta e buffer curto retornam o tamanho necessário incluindo NUL; sucesso também retorna esse tamanho. |

`__getmainargs` e o dado importado `__initenv` recebem a representação ANSI do
mesmo mapa, não o vetor `environ` do Linux.

## FLS

`FlsAlloc`, `FlsFree`, `FlsGetValue` e `FlsSetValue` possuem até 128 índices
por processo. A alocação e o callback são comuns ao processo; os valores são
armazenados por thread convidada. Um callback precisa apontar para uma página
executável da imagem PE ativa e é chamado pela ABI Microsoft x64 uma única vez
para cada valor não nulo: no término da thread ou quando `FlsFree` libera o
índice. O valor é limpo antes do callback.

Índice inválido/callback inválido retorna `ERROR_INVALID_PARAMETER`; exaustão
retorna `FLS_OUT_OF_INDEXES` e `ERROR_NOT_ENOUGH_MEMORY`. Fibras do runtime
ainda não trocam contexto real: nesta etapa FLS tem semântica por thread, não
por fibra.

## Locale e code pages

O locale é sempre `en-US` (`LCID 0x0409`), independente do host. `GetACP`
retorna `1252` e `GetOEMCP`, `437`. `GetCPInfo` aceita ACP/1252, OEM/437 e
UTF-8; `MultiByteToWideChar` e `WideCharToMultiByte` implementam CP1252,
CP437 completa e UTF-8.

`GetLocaleInfoW` aceita `0x0409`, `LOCALE_USER_DEFAULT` e
`LOCALE_SYSTEM_DEFAULT`; `GetLocaleInfoEx` aceita também `en-US`
case-insensitive (ou o nome nulo para o padrão). Ambos fornecem idioma, país,
ISO, decimal, milhar, moeda, AM/PM e code page, inclusive
`LOCALE_RETURN_NUMBER` para os tipos numéricos cobertos.

`IsValidCodePage` aceita somente ACP/1252, OEM/437 e UTF-8. `IsValidLocale`
aceita o locale fixo para as flags `LCID_INSTALLED` e `LCID_SUPPORTED`.
`EnumSystemLocalesW` chama uma única vez um callback Microsoft x64 validado da
imagem convidada, com a string `0409`, para essas mesmas flags; callback
ausente, flags não cobertas ou endereço não executável falham de forma
controlada.

`GetStringTypeW` cobre `CT_CTYPE1` para caracteres ASCII e Latin-1.
`GetDateFormatW` e `GetTimeFormatW` aceitam um `SYSTEMTIME` explícito válido e
os formatos estáticos en-US `M/d/yyyy`, `Weekday, Month d, yyyy` e
`h:mm:ss AM/PM` (ou 24 h/sem segundos pelas flags documentadas). Todas seguem
consulta de tamanho, NUL e `ERROR_INSUFFICIENT_BUFFER`.

`LCMapStringW` e `LCMapStringEx` aceitam somente `en-US` e a transformação
upper ou lower de ASCII/Latin-1. Sort keys, formatação customizada,
normalização, largura, CJK, locale do host e flags restantes falham
controladamente.

## Diagnóstico e limites

As operações bem-sucedidas relevantes emitem eventos `runtime` `environment`,
`fls` e `locale` em `stderr`; a saída padrão continua pertencendo ao
convidado. O contrato não inclui `SetThreadLocale`,
`GetUserDefaultUILanguage`, locale configurável por prefixo, fibras reais, GUI,
ACL ou rede.
