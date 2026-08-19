# Registro genérico

O subconjunto `ADVAPI32.dll` modela uma árvore por caminho de chave e valores
tipados. As variantes A armazenam bytes e as variantes W convertem nomes UTF-16
para UTF-8, preservando os bytes do valor. O armazenamento é persistido em
`$APPDATA/tradutorlinux-registry.db`; `TL_REGISTRY_FILE` permite isolar uma
execução de teste.

Não há hive real, ACL, segurança, políticas de máquina ou comportamento de
registro Windows. O contrato existe para fixtures independentes e não contém
nomes de aplicações específicas.
