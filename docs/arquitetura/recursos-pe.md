# Recursos PE

O loader fornece ao runtime a base e o tamanho do diretório `IMAGE_DIRECTORY_ENTRY_RESOURCE`
da imagem convidada. A camada de API não consulta o arquivo PE original nem
aceita ponteiros fornecidos pelo convidado como endereços host.

Cada offset da árvore é validado contra o diretório de recursos; cada
`IMAGE_RESOURCE_DATA_ENTRY` é validada contra a imagem mapeada. `FindResourceW`
guarda apenas um token opaco, e `LockResource` retorna uma faixa somente leitura.
No primeiro contrato, a primeira entrada de idioma é usada e os identificadores
numéricos e nomes UTF-16 são aceitos.

Recursos fora da imagem, contagens excessivas, strings de nome inválidas e
handles de módulo desconhecidos retornam erro Win32 controlado.
