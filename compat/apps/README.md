# Extensões isoladas por aplicativo

Esta árvore é reservada para futuros shims ou DLLs específicos de um
aplicativo. Ela não contém DLLs neste momento.

Regras:

- o runtime geral permanece em `src/runtime/` e não depende desta árvore;
- cada aplicativo terá um diretório próprio, identificado por um ID estável;
- qualquer shim futuro será construído como alvo separado e instalado somente
  no prefixo do aplicativo correspondente;
- uma extensão específica precisa de manifesto, teste de integração e entrada
  na matriz de compatibilidade antes de ser usada em produção;
- não serão adicionadas DLLs binárias geradas ou baixadas diretamente ao
  repositório.

O diretório `7zip/` é a primeira extensão host-side implementada. Seu alvo
`tradutorlinux_7zip` é composto explicitamente apenas pelo executável CLI e
não altera o comportamento do `tradutorlinux_core`. O perfil schema 4 precisa
selecionar `"extension": "7zip"`; não há fallback quando uma extensão
declarada não está registrada.

O diretório `_template/` documenta a forma reservada para novos aplicativos,
sem representar suporte ou implementação funcional.
