# Testes específicos por aplicativo

Smokes, fixtures e cenários de interação de aplicativos reais ficam nesta
árvore. Eles exercitam o runtime geral, mas não fazem parte da implementação
de produção nem criam DLLs específicas.

Cada cenário deve declarar o aplicativo, a ação segura, o critério de término,
os limites externos e a evidência ON/OFF quando aplicável.

O `7zip/7z_cli_smoke.cpp` é executado somente quando recebe explicitamente o
runtime e o `7z_x64.exe` do corpus. O CMake registra o teste automaticamente
quando `TL_POPULAR_APPS_DIR` aponta para `Aplicativos_Windows_Populares/`.
