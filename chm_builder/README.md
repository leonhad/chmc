# Gerador CHM em C++

Este diretório contém uma implementação autocontida de:

- compressor **LZX** no perfil usado por CHM (`lzx.h` / `lzx.cpp`);
- montador de container **ITSF v3 / ITSP v1** com diretório PMGL/PMGI (`chm.h` / `chm.cpp`);
- CLI que lê um projeto `.hhp` e os arquivos `.hhc` / `.hhk` pré-existentes (`main.cpp`).

## Pré-requisitos

Antes de executar o gerador, **todos** os arquivos listados no `[FILES]` do `.hhp`, além do
`.hhc`, do `.hhk` e do tópico inicial, já devem existir em disco. O gerador não cria HTML,
imagens nem arquivos de projeto — ele apenas os empacota.

Um projeto mínimo é:

```text
projeto.hhp
sumario.hhc
indice.hhk
index.htm
topic1.htm
estilo.css
logo.png
```

Veja o exemplo completo em `../exemplo/`.

## Compilação

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    main.cpp chm.cpp lzx.cpp -o chm_builder
```

No Windows com LLVM-MinGW, o mesmo comando funciona usando `g++` do PATH.

Também há um `Makefile` na raiz do repositório:

```bash
make                 # compila ../chm_builder.exe
make run             # gera ../exemplo/projeto.chm
make clean           # remove objetos e executáveis
make test LIBMSPACK=/caminho/para/libmspack
```

## Uso

```bash
chm_builder <projeto.hhp> [arquivo_saida.chm]
```

Se o segundo argumento for omitido, o nome de saída vem do campo `Compiled file=` do `.hhp`.

Exemplo:

```bash
cd exemplo
../chm_builder.exe projeto.hhp
```

O comando acima gera `exemplo/projeto.chm`.

## Opções do `.hhp` reconhecidas

| Seção       | Chave                   | Uso                                                   |
|-------------|-------------------------|-------------------------------------------------------|
| `[OPTIONS]` | `Title`                 | Título do CHM                                         |
| `[OPTIONS]` | `Compiled file`         | Nome do `.chm` gerado                                 |
| `[OPTIONS]` | `Contents file`         | Caminho do `.hhc` (sumário)                           |
| `[OPTIONS]` | `Index file`            | Caminho do `.hhk` (índice)                            |
| `[OPTIONS]` | `Default topic`         | Tópico exibido ao abrir o CHM                         |
| `[OPTIONS]` | `Default Window`        | Nome da janela em `#WINDOWS`                          |
| `[OPTIONS]` | `Default Font`          | Fonte padrão em `#STRINGS`/`#IDXHDR`                 |
| `[OPTIONS]` | `Language`              | LCID (`0x416` = pt-BR, `0x409` = en-US)               |
| `[FILES]`   | `arquivo=...`           | Arquivos a incluir na seção comprimida                |

Referências `src=`, `value=` e `url(...)` encontradas em HTML/CSS são adicionadas
automaticamente quando o arquivo referenciado existe.

## Perfil LZX gerado

| Parâmetro                    | Valor                                      |
|------------------------------|--------------------------------------------|
| Janela                       | 64 KiB (`windowBits = 16`, `windowSize = 2`) |
| Tamanho do frame             | 32768 bytes (`0x8000`)                     |
| `resetInterval`              | 2 frames (0x10000 bytes)                   |
| Preprocessing E8             | desabilitado (bit 0 no início do intervalo) |
| Tipo de bloco                | VERBATIM, um bloco por frame de 32 KiB     |
| Árvore principal             | fixa: 16 símbolos de 8 bits + 480 de 9 bits |
| Árvore de comprimentos       | fixa: 7 símbolos de 7 bits + 242 de 8 bits |
| Árvore de offsets alinhados  | não gravada (somente usada em blocos ALIGNED) |

Cada frame é preenchido com zeros até 32768 bytes e realinhado em word de 16 bits. As árvores
são transmitidas em todos os blocos, mas os deltas são relativos ao bloco anterior; o estado
é zerado a cada intervalo de reset. Ao final, 4 bytes zero satisfazem a leitura do frame extra
pelo decodificador.

## Validação

Os fontes `test_lzx.cpp` e `test_chm.cpp` usam o `lzxd.c` e o `chmd.c` do
[libmspack](https://github.com/kyz/libmspack) como decodificadores independentes.

```bash
# Supondo libmspack em ../../libmspack (ajuste LIBMSPACK conforme necessário)
LIBMSPACK=/caminho/para/libmspack
gcc -I "$LIBMSPACK" -c "$LIBMSPACK/lzxd.c" -o lzxd.o
gcc -I "$LIBMSPACK" -c "$LIBMSPACK/chmd.c" -o chmd.o
gcc -I "$LIBMSPACK" -c "$LIBMSPACK/system.c" -o mspack_system.o

g++ -std=c++17 -O2 -I "$LIBMSPACK" \
    lzx.cpp test_lzx.cpp lzxd.o mspack_system.o -o test_lzx
./test_lzx /tmp/lzx_teste

g++ -std=c++17 -O2 -I "$LIBMSPACK" \
    test_chm.cpp chmd.o lzxd.o mspack_system.o -o test_chm
./test_chm ../exemplo/projeto.chm ../exemplo
```

A saída esperada é `OK` para o teste LZX e `OK <arquivo>` para cada arquivo do exemplo,
com `falhas=0` ao final. Além disso, o CHM gerado foi aberto com sucesso no
Visualizador de Ajuda do Windows (`hh.exe`), exibindo a janela `HH Parent` e o título
`Exemplo CHM`.

> **Importante:** os quickrefs dos chunks PMGL/PMGI devem ser gravados em ordem inversa e
> relativos ao início da área de entradas. O `libmspack` ignora essa tabela, mas o `hh.exe`
> a utiliza; uma tabela invertida incorretamente faz o visualizador rejeitar o arquivo.

## Limitações conhecidas

- `$FIftiMain`, `$OBJINST` e `$WW*` são apenas stubs de tamanho zero no diretório. O `hh.exe`
  abre o CHM, mas a busca full-text e o índice binário de palavras-chave não funcionam.
- Os IDs de `#URLTBL` são determinísticos (FNV-1a), diferentes dos do HHC; a navegação usa o
  índice da tabela e não é afetada.
- A seção `[WINDOWS]` do `.hhp` não é interpretada. O gerador grava a janela padrão
  compatível com o `hh.exe`; o exemplo omite essa seção porque estilos explícitos podem
  fazer o visualizador fechar imediatamente.
- O parser de `.hhc` / `.hhk` reconhece `<UL>`, `<LI>` e `<OBJECT type="text/sitemap">`
  com parâmetros `Name` e `Local`; não é um parser HTML completo.
- Árvores Huffman fixas são usadas em vez de árvores ótimas por frequência. Isso aumenta
  ligeiramente o tamanho comprimido, mas evita bugs de codificação delta do pretree e foi
  validado com `lzxd.c` em arquivos de múltiplos frames.
- Não há suporte a ActiveX, macros, *embedded windows* personalizadas nem scripts
  de navegação especiais.

Para detalhes binários, consulte `../formato_chm.md` e `../formato_lzx.md`.
