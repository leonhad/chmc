# Gerador CHM em C++

Este diretório contém uma implementação autocontida de:

- compressor **LZX** no perfil usado por CHM (`chm_builder/lzx.h` / `lzx.cpp`);
- montador de container **ITSF v3 / ITSP v1** com diretório PMGL/PMGI (`chm_builder/chm.h` / `chm.cpp`);
- CLI que lê um projeto `.hhp` e os arquivos `.hhc` / `.hhk` pré-existentes (`chm_builder/main.cpp`).

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

Veja o exemplo completo em `exemplo`.

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
make test                # valida com libmspack (incluído no projeto)
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

Os fontes `chm_builder/test_lzx.cpp` e `test_chm.cpp` usam o `lzxd.c` e o `chmd.c` do
[libmspack](https://github.com/kyz/libmspack) como decodificadores independentes.
O libmspack está incluído no diretório `libmspack/` do projeto.

```bash
make test-lzx   # valida o compressor LZX
make test-chm   # valida o CHM gerado
```

A saída esperada é `OK` para o teste LZX e `OK <arquivo>` para cada arquivo do exemplo,
com `falhas=0` ao final. Além disso, o CHM gerado foi aberto com sucesso no
Visualizador de Ajuda do Windows (`hh.exe`), exibindo a janela `HH Parent` e o título
`Exemplo CHM`.

> **Importante:** os quickrefs dos chunks PMGL/PMGI devem ser gravados em ordem inversa e
> relativos ao início da área de entradas. O `libmspack` ignora essa tabela, mas o `hh.exe`
> a utiliza; uma tabela invertida incorretamente faz o visualizador rejeitar o arquivo.

## Índice binário e busca full-text

O gerador preenche os blobs internos reais:

- `$OBJINST` contém a tabela de 256 × 10 bytes de `chm_builder/objinst_table.h` e os dois objetos COM;
  no exemplo o resultado é byte-idêntico ao blob do HHC de referência (2.715 bytes).
- `$WWKeywordLinks/{BTree,Data,Map,Property}` é montado dos pares `Name`/`Local` do `.hhk`.
  BTree com blocos de 2.048 bytes, 13 bytes por entrada de Data, Map e Property com oito
  DWORDs. `$WWAssociativeLinks/Property` recebe quatro bytes zero.
- `$FIftiMain` indexa `<title>` e `<body>` dos HTMLs. As árvores usam nós de 4.096 bytes,
  WLC com escalas `2` e raízes `1`, `1`, `5`, bits MSB-first e ENCINT little-endian de 7 bits.
  A entrada 4 de `#SYSTEM` anuncia `full-text` e `KLinks` quando aplicável.

O parser `.hhk` captura `Name` e `Local`, e os índices de tópico são resolvidos por `#TOPICS`.
Como validação adicional, os blobs `$WW*` e `$OBJINST` do exemplo foram comparados byte a byte
com os do HHC, e o `$FIftiMain` foi decodificado com a semântica do leitor do FPC.

## Limitações conhecidas

- Os IDs de `#URLTBL` são determinísticos (FNV-1a), diferentes dos do HHC; a navegação usa o
  índice da tabela e não é afetada.
- A seção `[WINDOWS]` do `.hhp` é interpretada para janelas padrão (campos comuns
  de `HH_WINTYPE`: título, TOC, índice, tópico padrão/home, posição, estilos, botões
  e painel de navegação). *Embedded windows* avançadas/tipos especiais não são suportados.
- O parser de `.hhc` / `.hhk` reconhece `<UL>`, `<LI>` e `<OBJECT type="text/sitemap">`
  com parâmetros `Name` e `Local`; não é um parser HTML completo.
- Árvores Huffman fixas são usadas em vez de árvores ótimas por frequência. Isso aumenta
  ligeiramente o tamanho comprimido, mas evita bugs de codificação delta do pretree e foi
  validado com `lzxd.c` em arquivos de múltiplos frames.
- Não há suporte a ActiveX, macros, *embedded windows* personalizadas nem scripts
  de navegação especiais.

Para detalhes binários, consulte `formato_chm.md` e `formato_lzx.md`.
