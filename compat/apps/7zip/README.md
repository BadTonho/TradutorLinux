# Extensão host-side do 7-Zip

Este diretório contém a extensão GUI específica do 7-Zip File Manager. Ela é
compilada como o alvo separado `tradutorlinux_7zip` e só é registrada pelo
executável principal; `tradutorlinux_core` não depende deste diretório.

A extensão não fornece DLL, não é copiada para o prefixo e só é ativada por
um perfil schema 4 que declare `"extension": "7zip"`. Sem esse campo, a
classe `7-Zip::FM` segue o caminho genérico. O estado visual e de navegação é
host-only e pertence ao `GuestContext` da execução.
