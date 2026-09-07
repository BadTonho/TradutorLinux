# Testes específicos por aplicativo

Smokes, fixtures e cenários de interação de aplicativos reais ficam nesta
árvore. Eles exercitam o runtime geral, mas não fazem parte da implementação
de produção nem criam DLLs específicas.

Cada cenário deve declarar o aplicativo, a ação segura, o critério de término,
os limites externos e a evidência ON/OFF quando aplicável.

O `7zip/7z_cli_smoke.cpp` é executado somente quando recebe explicitamente o
runtime e o `7z_x64.exe` do corpus. O cenário cobre arquivos `stored`,
`DEFLATE`, `7z/LZMA2` normal e protegido por senha, rejeição de senha incorreta
sem payload válido, caminhos relativos e o filtro stdin/stdout (`-si`/`-so`).
O CMake registra o teste automaticamente quando `TL_POPULAR_APPS_DIR` aponta
para `Aplicativos_Windows_Populares/`.

O mesmo parâmetro registra `popular_apps_report_matrix`, que analisa os 26 PE
selecionados e o MSIX do corpus e verifica os exit codes esperados sem mapear
ou executar os arquivos. O caso é repetido no build Rust ON e no baseline C++
OFF.

`popular_apps_native_matrix` executa somente os cinco PE32+ com ação direta já
documentada: 7-Zip, os dois binários WinRAR, Rockstar e Rufus. Cada caso usa
prefixo temporário, `--timeout 3`, limites de CPU/memória e verifica o exit code
e o diagnóstico esperado; instaladores, DLLs e cenários interativos de GUI
permanecem nos testes específicos.

`popular_apps_install_matrix` cobre somente os quatro candidatos aprovados
para instalação controlada: Roblox, G HUB, o alias byte-a-byte e Affinity. Cada
caso usa `HOME`, `XDG_CONFIG_HOME`, `APPDATA` e prefixo temporários, verifica o
estágio/erro esperado e exige ausência de arquivos, extração e cadastro após a
rejeição. Instaladores PE32/x86 permanecem fora.

Com o mesmo parâmetro e Xvfb disponível, os smokes GUI reais de 7-Zip File
Manager, PuTTY, WinRAR SFX e Notepad++ também são registrados separadamente no
CTest. Cada cenário cria seu próprio Xvfb/staging e mantém seu resultado
limitado ao comportamento documentado na matriz de compatibilidade.
