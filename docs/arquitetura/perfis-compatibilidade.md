# Perfis de compatibilidade por aplicativo

## Objetivo

Cada aplicativo cadastrado pode ter arquivos auxiliares do TradutorLinux no
próprio prefixo, sem misturá-los aos arquivos reais do convidado e sem alterar
o comportamento de outro aplicativo.

```text
<prefixo>/
├── drive_c/       # arquivos reais visíveis como C:\
└── compat/
    ├── profile.json
    └── files/     # fontes dos arquivos auxiliares
```

`compat/` não é um drive Windows e não fica visível automaticamente ao
convidado. Nesta versão, o perfil é colocado manualmente no prefixo. A
exposição dos arquivos para caminhos de `drive_c` pertence à B14.3.

## Formato v1

O arquivo `compat/profile.json` é um objeto JSON UTF-8 com os campos abaixo:

```json
{
  "schema": 1,
  "app_id": "meu-aplicativo",
  "app_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "app_version": "1.2.3",
  "files": [
    {
      "source": "config.dat",
      "target": "C:\\Program Files\\Meu Aplicativo\\config.dat"
    }
  ]
}
```

`schema` e `app_id` são obrigatórios. `app_sha256` e `app_version` são
opcionais; quando presentes, precisam corresponder aos dados registrados para
o executável no catálogo. O `source` é relativo a `compat/files/`, usa `/` e
não pode conter `.` ou `..`. O `target` é um arquivo dentro de `C:\`, que o
runtime valida como pertencente a `drive_c`.

A v1 aceita somente esses campos. Não aceita regras de API, comandos, scripts,
DLLs arbitrárias ou outros mecanismos executáveis.

## Fallback e diagnóstico

`app run` consulta o perfil do aplicativo cadastrado. Um perfil ausente,
inválido ou incompatível não impede a execução: o runtime emite um aviso em
`stderr`, registra o evento `compat-profile` no trace quando habilitado e
preserva o exit code produzido pelo convidado.

Um perfil é inválido quando há JSON malformado, schema desconhecido, campo
desconhecido ou repetido, identidade incompatível, hash/versão divergente,
arquivo de origem ausente, caminho fora de `compat/files/` ou destino fora de
`drive_c`. O perfil inteiro é ignorado; não há aplicação parcial.

Exemplos de estados:

- **Ausente:** não existe `compat/profile.json`; segue o comportamento genérico.
- **Válido:** todos os campos e arquivos declarados passam pela validação.
- **Inválido:** por exemplo, um campo não reconhecido ou uma origem ausente.
- **Incompatível:** `app_id`, SHA-256 ou versão não correspondem ao catálogo.

A ausência ou a rejeição do perfil não altera o resultado normal do programa.
O arquivo `compat/` continua sendo apenas uma área de dados; a materialização
ou o mapeamento de `files/` será implementado e testado separadamente.
