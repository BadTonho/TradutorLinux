# WinTrust: cadeia explícita de certificados

A entrega de confiança implementa `WINTRUST.dll!WinVerifyTrust` e a travessia
limitada pelos três `WTHelper*`, com `WINTRUST_DATA` no formato
`WTD_CHOICE_BLOB`. Ela existe para testar uma política reproduzível de cadeia,
sem consultar a loja de certificados do Windows ou afirmar compatibilidade com
Authenticode.

## Contrato

O `WINTRUST_DATA` precisa ter `cbStruct` correto, `dwUIChoice=WTD_UI_NONE`,
`fdwRevocationChecks=WTD_REVOKE_NONE`, `dwUnionChoice=WTD_CHOICE_BLOB` e os
campos de UI, URL e assinatura nulos. A ação aceita é somente
`WINTRUST_ACTION_GENERIC_VERIFY_V2`. `WTD_STATEACTION_IGNORE` mantém a
consulta sem estado; `WTD_STATEACTION_VERIFY` cria um estado transitório e
`WTD_STATEACTION_CLOSE` o encerra.

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

A fronteira de memória fotografa a ação, `WINTRUST_DATA`,
`WINTRUST_BLOB_INFO` e o payload com transferências protegidas antes da
validação. `VERIFY` e `CLOSE` publicam ou limpam `state_data` por escrita
protegida; nenhum campo arbitrário do convidado é desreferenciado diretamente.

## Consulta da cadeia

Depois de `VERIFY`, `WTHelperProvDataFromStateData` devolve o registro de
provedor associado ao `state_data`. A travessia limitada aceita somente
`idxSigner=0`, sem contra-assinante, e `idxCert=0` (folha) ou `idxCert=1`
(raiz), por meio de `WTHelperGetProvSignerFromChain` e
`WTHelperGetProvCertFromChain`. Os registros são válidos até `CLOSE`; o
`pCert` de cada certificado aponta para um `CERT_CONTEXT` DER compatível com o
subconjunto de `CertGetNameStringW`. Outros estados, stores, contra-assinantes
e índices retornam nulo.

## Evidência e limites

`tl_trust.exe` valida uma cadeia real folha→raiz, rejeita uma política com UI e
rejeita a cadeia com raiz incorreta. O teste de runtime também exige o evento
`wintrust` no `stderr`. O export separado `CRYPT32.dll!CertGetNameStringW`
agora aceita somente um `CERT_CONTEXT` explícito com estrutura DER e os tipos de nome
cobertos por `tl_crypt32.exe`; ele não consulta a loja nem as extensões SAN.
Demais APIs `CRYPT32.dll`, `WinVerifyTrust` para `WTD_CHOICE_FILE`, loja Windows,
revogação e verificação Authenticode permanecem fora do contrato; essas
fixtures não tornam o Rockstar suportado.
