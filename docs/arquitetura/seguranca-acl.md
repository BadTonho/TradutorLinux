# Identidade virtual e DACLs por prefixo

## Finalidade

A Fase 13.10 fornece a parte observável de identidade e DACL necessária às
fixtures do runtime. Ela mantém, para cada prefixo, um SID artificial não
elevado e descritores de segurança para objetos existentes em `C:\`. Não é
uma integração com a identidade Linux, nem um mecanismo de autenticação ou
sandbox.

## Estado persistente

O arquivo versionado `.tradutorlinux-security.bin`, na raiz do prefixo, guarda
o SID do usuário virtual e as DACLs explicitamente gravadas. Seu SID tem a
forma `S-1-5-21-<a>-<b>-<c>-1000`; os três valores são criados uma vez e nunca
vêm de UID, grupos ou ACLs do hospedeiro. O token retornado para o processo
atual contém somente esse usuário e informa `TokenElevation = 0`.

Um arquivo sem registro recebe implicitamente o descritor padrão: owner e
group iguais ao SID virtual, mais uma ACE `ACCESS_ALLOWED` de `GENERIC_ALL`
para ele. O registro é indexado por caminho canônico relativo a `drive_c`, não
por caminho Linux absoluto. `Z:\`, objetos inexistentes, raiz de `C:\` e
caminhos que escapem por symlink são rejeitados. Renomear leva o registro junto;
excluir remove o registro; uma cópia recebe novamente o descritor padrão.

O arquivo de metadados aceita somente sua versão atual, limites de tamanho e
registros bem formados. Erro de leitura ou gravação não cria um estado parcial;
a API retorna o erro Win32 controlado.

## Subconjunto de API

`ADVAPI32.dll` expõe `OpenProcessToken` somente para `GetCurrentProcess()` com
`TOKEN_QUERY`, `GetTokenInformation` para `TokenUser` e `TokenElevation`, e as
operações `AllocateAndInitializeSid`, `CopySid`, `FreeSid`, `GetLengthSid`,
`EqualSid`, `IsValidSid`, `CreateWellKnownSid`, `CheckTokenMembership` e
`BuildTrusteeWithSidW`. Os SIDs conhecidos disponíveis são World e Builtin
Administrators; o usuário virtual é membro de si mesmo e não de Administrators.

`InitializeSecurityDescriptor`, `SetSecurityDescriptorDacl`,
`SetEntriesInAclW`, `GetNamedSecurityInfoW`, `SetNamedSecurityInfoW` e
`SetFileSecurityW` lidam com descritores absolutos e DACLs. `GetNamedSecurityInfoW`
aloca um único bloco independente, liberável por `LocalFree`; owner, group e
DACL retornados apontam para dentro dele. As escritas preservam owner/group
fixos do prefixo e persistem apenas a DACL.

`SetEntriesInAclW` aceita trustees cujo `TrusteeForm` é SID, ACEs allow/deny e
os modos `GRANT`, `SET`, `DENY` e `REVOKE`. `SET`/`REVOKE` substituem/removem as
ACEs daquele SID; `DENY` é mantida antes das ACEs allow. Herança não nula,
trustees por nome, múltiplos trustees, SACL, auditoria e tipos de ACE fora
desse conjunto retornam `ERROR_NOT_SUPPORTED`.

## ABI e validação

Todos os ponteiros recebidos do convidado são validados antes de leitura ou
escrita, inclusive tamanhos indicados por estruturas variáveis. Os layouts
Microsoft x64 publicados ficam em [`abi-x64.md`](abi-x64.md): SID variável,
`TOKEN_USER`, `TOKEN_ELEVATION`, `SECURITY_DESCRIPTOR`, ACL, `TRUSTEE_W` e
`EXPLICIT_ACCESS_W`. `GetTokenInformation` segue o protocolo de buffer: a
primeira chamada retorna `ERROR_INSUFFICIENT_BUFFER` e o tamanho necessário;
classes fora de `TokenUser`/`TokenElevation` retornam `ERROR_NOT_SUPPORTED`.

## Limites deliberados

As DACLs são metadados de compatibilidade. Elas não bloqueiam `CreateFile`,
não implementam `AccessCheck`, não chamam `chmod` nem ACL POSIX e não revelam
caminhos ou identidade do host. SACL, privilégio/elevation, certificados,
ACL de rede, herança complexa, `LookupPrivilegeValueW` e
`AdjustTokenPrivileges` continuam fora do contrato.

O componente de trace `runtime` emite o evento `security` para abertura e
consulta de token, leitura/gravação de descritor e merge de ACL. Ele informa
apenas operação, resultado, detalhe lógico e `scope="prefix"`.
