# WinTrust: cadeia explícita de certificados

A primeira entrega de confiança implementa somente
`WINTRUST.dll!WinVerifyTrust` com `WINTRUST_DATA` no formato
`WTD_CHOICE_BLOB`. Ela existe para testar uma política reproduzível de cadeia,
sem consultar a loja de certificados do Windows ou afirmar compatibilidade com
Authenticode.

## Contrato

O `WINTRUST_DATA` precisa ter `cbStruct` correto, `dwUIChoice=WTD_UI_NONE`,
`fdwRevocationChecks=WTD_REVOKE_NONE`, `dwUnionChoice=WTD_CHOICE_BLOB`,
`dwStateAction=WTD_STATEACTION_IGNORE` e os campos de UI, estado, URL e
assinatura nulos. A ação aceita é somente
`WINTRUST_ACTION_GENERIC_VERIFY_V2`.

`WINTRUST_BLOB_INFO.pbMemObject` aponta para o protocolo de fixture `TLTC`:

```text
"TLTC" | version:u32le=1 | count:u32le=2 |
leaf_length:u32le | leaf_DER | root_length:u32le | root_DER
```

O runtime copia e valida os dois DER, carrega `libcrypto` dinamicamente,
constrói uma `X509_STORE` com a raiz fornecida e verifica a assinatura, a
validade temporal e as restrições de CA da cadeia. A raiz é explícita e não há
revogação, intermediários adicionais, EKU de Authenticode, catálogo, arquivo
PE, política de hostname ou loja do sistema.

## Evidência e limites

`tl_trust.exe` valida uma cadeia real folha→raiz, rejeita uma política com UI e
rejeita a cadeia com raiz incorreta. O teste de runtime também exige o evento
`wintrust` no `stderr`. `WTHelper*`, `CRYPT32.dll`, `WinVerifyTrust` para
`WTD_CHOICE_FILE` e a verificação Authenticode permanecem fora do contrato;
essa fixture não torna o Rockstar suportado.
