# Template de extensão por aplicativo

Use este diretório como referência para uma futura extensão isolada:

```text
compat/apps/<app-id>/
├── README.md
├── CMakeLists.txt       # somente se houver uma extensão implementada
└── src/                 # código específico, nunca o runtime geral
```

Este template não cria DLL, não é ligado ao `tradutorlinux_core` e não altera
o comportamento de nenhum aplicativo.
