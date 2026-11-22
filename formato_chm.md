# Formato de Arquivo CHM (Compiled HTML Help)

Documentação técnica detalhada do formato `.chm`, incluindo estrutura binária, diretório interno,
compressão, arquivos constituintes e considerações de segurança.

---

## 1. Visão geral

| Propriedade          | Valor                                                                 |
|----------------------|-----------------------------------------------------------------------|
| Nome completo        | Compiled HTML Help (Ajuda HTML Compilada)                             |
| Extensão             | `.chm`                                                                |
| MIME type            | `application/vnd.ms-htmlhelp`                                         |
| Criador              | Microsoft (introduzido com o Windows 98 / HTML Help 1.x)              |
| Sucessor de          | `.hlp` (Windows Help 3.x / 4.x)                                       |
| Assinatura mágica    | `ITSF` (bytes `49 54 53 46` no início do arquivo)                     |
| Ordem de bytes       | Little-endian (Intel), exceto onde indicado                           |
| Compressão           | LZX (algoritmo da família LZ77, também usado em arquivos CAB)         |
| Visualizador         | `hh.exe` / `hhctrl.ocx` (HTML Help Viewer, nativo do Windows)         |

Um arquivo CHM é, em essence, **um pequeno sistema de arquivos compactado** que contém páginas
HTML, imagens, scripts e metadados de navegação (índice, sumário, busca), mantidos em "seções"
de conteúdo. A compressão é transparente para o visualizador.

> **Nota histórica:** especula-se que o formato deriva de `IStorage` (estruturas de armazenamento
> COM documentadas no Platform SDK), embora a documentação pública não confirme isso. O formato
> foi reverse-engineered; a referência clássica é o trabalho de Matthew T. Russotto (2001–2003).

---

## 2. Estrutura física geral

```
+--------------------------------------------------+  offset 0x0000
|  ITSF initial header (0x38 = 56 bytes)           |
|  assinatura, versão, timestamp, LCID, 2 GUIDs    |
+--------------------------------------------------+  offset 0x0038
|  Header Section Table (2 entradas de 16 bytes)   |
|  [offset, length] para a seção 0 e para o dir.   |
+--------------------------------------------------+  offset 0x0058
|  8 bytes extras: offset da content section 0     |
|  (ausentes em arquivos versão 2)                 |
+--------------------------------------------------+  offset 0x0060 (v3)
|  Header Section 0: informações gerais            |
|  (tamanho total do arquivo, dados desconhecidos)|
+--------------------------------------------------+
|  Header Section 1: diretório (ITSP + chunks      |
|  PMGL / PMGI)                                    |
+--------------------------------------------------+
|  Content Section 0 (dados + seções aninhadas)    |
|  - HTML, imagens, ::DataSpace/* etc.             |
+--------------------------------------------------+  fim do arquivo
```

O cabeçalho inteiro (initial header + tabela de seções + campo de conteúdo) mede **0x60 = 96 bytes**
na versão 3. As duas "header sections" são apontadas por offsets absolutos na tabela.

---

## 3. Cabeçalho inicial (ITSF)

Localizado no offset `0x0000`. Tamanho: `0x38` bytes (56 bytes) antes da tabela de seções.

| Offset | Tamanho | Tipo       | Descrição                                                                 |
|--------|---------|------------|---------------------------------------------------------------------------|
| 0x00   | 4       | char[4]    | Assinatura: `"ITSF"`                                                      |
| 0x04   | 4       | DWORD      | Versão (normalmente `3`; versões 1 e 2 existem)                            |
| 0x08   | 4       | DWORD      | Comprimento total do cabeçalho, incluindo tabela de seções e dados extras  |
| 0x0C   | 4       | DWORD      | Desconhecido (`1` em muitos arquivos)                                     |
| 0x10   | 4       | DWORD      | Timestamp (derivado do `GetFileTime` do arquivo, com um +42 aplicado)      |
| 0x14   | 4       | DWORD      | LCID do usuário que compilou o CHM (ex.: `0x0409` = inglês EUA)            |
| 0x18   | 16      | GUID       | GUID do diretório: `{7C01FD10-7BAA-11D0-9E0C-00A0-C922-E6EC}`             |
| 0x28   | 16      | GUID       | GUID do fluxo de dados: `{7C01FD11-7BAA-11D0-9E0C-00A0-C922-E6EC}`         |

### Tabela de seções (offset 0x38)

Duas entradas de 16 bytes (`QWORD offset absoluto`, `QWORD comprimento`):

| Entrada | Offset na tabela | Aponta para                          |
|---------|------------------|--------------------------------------|
| Seção 0 | 0x38             | Header Section 0 (info. gerais)      |
| Seção 1 | 0x48             | Header Section 1 (diretório ITSP)    |

### Campo extra (offset 0x58, apenas versão 3)

| Offset | Tamanho | Descrição                                              |
|--------|---------|--------------------------------------------------------|
| 0x58   | 8       | `QWORD`: offset da Content Section 0 dentro do arquivo |

Na versão 2 esse campo não existe e o conteúdo começa imediatamente após o diretório.

---

## 4. Header Section 0

Contém basicamente o tamanho total do arquivo:

| Offset | Tamanho | Tipo  | Descrição                              |
|--------|---------|-------|----------------------------------------|
| 0x00   | 4       | DWORD | Desconhecido (tipicamente `0x01FE`)    |
| 0x04   | 4       | DWORD | Desconhecido (`0`)                     |
| 0x08   | 8       | QWORD | Tamanho total do arquivo CHM em bytes  |
| 0x10   | 4       | DWORD | Desconhecido (`0`)                     |
| 0x14   | 4       | DWORD | Desconhecido (`0`)                     |

---

## 5. Header Section 1 — o diretório (ITSP)

É a estrutura central do CHM: um índice de todos os "arquivos" internos.

### 5.1 Cabeçalho do diretório

| Offset | Tamanho | Tipo    | Descrição                                                                 |
|--------|---------|---------|---------------------------------------------------------------------------|
| 0x00   | 4       | char[4] | Assinatura: `"ITSP"`                                                      |
| 0x04   | 4       | DWORD   | Versão (`1`)                                                              |
| 0x08   | 4       | DWORD   | Comprimento deste cabeçalho (`0x54` = 84 bytes)                           |
| 0x0C   | 4       | DWORD   | Desconhecido (`0x0A`)                                                     |
| 0x10   | 4       | DWORD   | Tamanho de cada chunk do diretório (`0x1000` = 4096 bytes, típico)        |
| 0x14   | 4       | DWORD   | Densidade da área quickref (geralmente `2`)                               |
| 0x18   | 4       | DWORD   | Profundidade da árvore de índice (`1` = só PMGL; `2` = um nível de PMGI)   |
| 0x1C   | 4       | DWORD   | Número do chunk raiz do índice (`-1` se não houver)                       |
| 0x20   | 4       | DWORD   | Número do primeiro chunk PMGL                                             |
| 0x24   | 4       | DWORD   | Número do último chunk PMGL                                               |
| 0x28   | 4       | DWORD   | Desconhecido (`-1`)                                                       |
| 0x2C   | 4       | DWORD   | Número total de chunks do diretório                                       |
| 0x30   | 4       | DWORD   | LCID do compilador (do ITSS.DLL, não do arquivo .hhp)                      |
| 0x34   | 16      | GUID    | `{5D02926A-212E-11D0-9DF9-00A0C922E6EC}`                                   |
| 0x44   | 4       | DWORD   | Comprimento novamente (`0x54`)                                            |
| 0x48   | 4       | DWORD   | Desconhecido (`-1`)                                                       |
| 0x4C   | 4       | DWORD   | Desconhecido (`-1`)                                                       |
| 0x50   | 4       | DWORD   | Desconhecido (`-1`)                                                       |

### 5.2 Chunks de listagem (PMGL)

Imediatamente após o cabeçalho ITSP vêm os chunks. Cada chunk ocupa o tamanho definido em
`0x10` do ITSP (normalmente 4096 bytes).

| Offset | Tamanho | Descrição                                                                 |
|--------|---------|---------------------------------------------------------------------------|
| 0x00   | 4       | Assinatura: `"PMGL"`                                                      |
| 0x04   | 4       | Comprimento da área livre/quickref no fim do chunk                        |
| 0x08   | 4       | Sempre `0`                                                                |
| 0x0C   | 4       | Número do chunk PMGL anterior (`-1` se for o primeiro)                    |
| 0x10   | 4       | Número do chunk PMGL seguinte (`-1` se for o último)                      |
| 0x14   | ...     | Entradas de listagem, ordenadas por nome (ordenação case-insensitive)     |

Cada **entrada de listagem** tem o formato:

```
ENCINT  comprimento do nome
BYTEs   nome (UTF-8)
ENCINT  número da seção de conteúdo
ENCINT  offset dentro da seção (após descompressão)
ENCINT  comprimento do arquivo (após descompressão)
```

**Quickref:** escrito de trás para frente no final do chunk. Existe uma entrada de quickref a
cada `n = 1 + (1 << densidade)` entradas (com densidade 2, `n = 5`). No fim do chunk:

```
ChunkLen-0x02: WORD  número de entradas no chunk
ChunkLen-0x04: WORD  offset da última entrada indexada (n, 2n, ...), relativo ao início da área de entradas
ChunkLen-0x06: WORD  offset do grupo anterior
... (ordem inversa; o primeiro quickref gravado é o das entradas 4, 9, 14, ...)
```

O offset é medido a partir do byte `0x14` do chunk (20 bytes de cabeçalho), e não do início
do arquivo. Essa tabela é ignorada pelo `libmspack`, mas obrigatória para o `hh.exe`.

### 5.3 Chunks de índice (PMGI)

Opcionais; omitidos quando há apenas um chunk PMGL.

| Offset | Tamanho | Descrição                                                    |
|--------|---------|--------------------------------------------------------------|
| 0x00   | 4       | Assinatura: `"PMGI"`                                         |
| 0x04   | 4       | Comprimento da área quickref/livre no final                  |
| 0x08   | ...     | Entradas de índice                                           |

Entrada de índice:

```
ENCINT  comprimento do nome
BYTEs   nome (UTF-8)
ENCINT  número do chunk de listagem que começa com esse nome
```

Com profundidade ≥ 3, os níveis superiores apontam para chunks PMGI inferiores em vez de PMGL.

### 5.4 ENCINT — inteiros codificados

Inteiro de comprimento variável. O bit alto (bit 7) de cada byte indica "continua no próximo
byte". Os grupos de 7 bits são armazenados do mais significativo para o menos significativo
(orange big-endian por grupo). Exemplo:

```
EA 15  ->  ((0xEA & 0x7F) << 7) | 0x15  =  0x3515
```

### 5.5 Convenção de nomes no diretório

- Nomes iniciados por `/` → **dados do usuário** (páginas HTML, imagens, CSS, scripts etc.).
- Nomes iniciados por `::` → **arquivos internos de formato** (metadados do sistema).

---

## 6. Content Sections e compressão

### 6.1 Seções de conteúdo

- A **Content Section 0** começa no offset indicado no cabeçalho ITSF (v3) e contém todos os
  dados "normais" (HTML, imagens etc.) **e** as outras seções aninhadas dentro de si.
- As **demais seções** são armazenadas dentro da seção 0, em arquivos especiais
  `::DataSpace/Storage/<NomeDaSeção>/Content`.

Os nomes de seção conhecidos são:

| Nome           | Significado                                  |
|----------------|----------------------------------------------|
| `Uncompressed` | Dados brutos, sem compressão                  |
| `MSCompressed` | Dados comprimidos com LZX                     |

### 6.2 `::DataSpace/NameList`

Arquivo na seção 0 que lista os nomes de todas as seções:

```
0000: WORD  comprimento do arquivo, em words
0002: WORD  número de entradas

Para cada entrada:
0000: WORD  comprimento do nome em words (sem o terminador nulo)
0002: WORD  caracteres UTF-16LE
xxxx: WORD  0  (terminador)
```

### 6.3 `ControlData` — parâmetros LZX

Arquivo `::DataSpace/Storage/<Seção>/ControlData` (0x1C = 28 bytes):

| Offset | Tamanho | Descrição                                                          |
|--------|---------|--------------------------------------------------------------------|
| 0x00   | 4       | Número de DWORDs seguintes ao `"LZXC"` (deve ser 6 na v2)           |
| 0x04   | 4       | ASCII `"LZXC"` — identificador do tipo de compressão               |
| 0x08   | 4       | Versão (≤ 2)                                                        |
| 0x0C   | 4       | Intervalo de reset do LZX                                          |
| 0x10   | 4       | Tamanho da janela                                                   |
| 0x14   | 4       | Tamanho do cache                                                    |
| 0x18   | 4       | Desconhecido (`0`)                                                  |

Na versão 2, intervalo de reset, janela e cache são expressos em blocos de `0x8000` bytes;
na versão 1, em bytes. O CHM compilado pelo HTML Help Workshop que serviu de referência usa
janela de **64 KiB** (`windowSize = 2` blocos de 0x8000, equivalente a `windowBits = 16` em
notação LZX) e `resetInterval = 2` frames, ou seja, um reset a cada 0x10000 bytes gerados.

### 6.4 `ResetTable` — tabela de reinício

Arquivo:
`::DataSpace/Storage/<Seção>/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable`

| Offset | Tamanho | Descrição                                                       |
|--------|---------|-----------------------------------------------------------------|
| 0x00   | 4       | `2` (provável número de versão)                                  |
| 0x04   | 4       | Número de entradas na tabela                                     |
| 0x08   | 4       | Tamanho de cada entrada (`8`)                                    |
| 0x0C   | 4       | Comprimento do cabeçalho da tabela (`0x28`)                      |
| 0x10   | 8       | Comprimento dos dados descomprimidos                             |
| 0x18   | 8       | Comprimento dos dados comprimidos                                |
| 0x20   | 8       | Tamanho de bloco (`0x8000`)                                      |
| 0x28   | 8       | Entrada 0 (zeroth)                                               |
| 0x30   | 8       | Posição no fluxo comprimido do 1º limite de bloco               |
| ...    | 8       | ...demais limites                                                |

### 6.5 Particularidades do LZX em CHM

- O fluxo de bits é **realignado em limites de 16 bits** a cada `0x8000` bytes de saída.
- O fluxo descomprimido é preenchido (padded) até um múltiplo de `0x8000` bytes.
- Após cada intervalo de reset, **todo o estado LZX é reiniciado** (tabelas de Huffman *e*
  histórico Lempel-Ziv), ao contrário do LZX de CAB. Isso permite acesso semi-aleatório:
  é possível começar a leitura em qualquer fronteira de intervalo usando a ResetTable.

### 6.6 Outros arquivos internos de seção

| Arquivo                                            | Conteúdo                                                        |
|----------------------------------------------------|-----------------------------------------------------------------|
| `::DataSpace/Storage/<Seção>/Content`              | Dados brutos/compactados da seção                                |
| `::DataSpace/Storage/<Seção>/ControlData`          | Parâmetros de compressão (LZXC)                                  |
| `::DataSpace/Storage/<Seção>/SpanInfo`             | QWORD com o tamanho descomprimido da seção                        |
| `::DataSpace/Storage/<Seção>/Transform/List`       | Tentativa de lista de GUIDs de transformação (geralmente quebrada)|
| `::DataSpace/Storage/<Seção>/Transform/{GUID}/InstanceData/ResetTable` | Tabela de reset LZX (ver 6.4)               |

---

## 7. Arquivos típicos dentro de um CHM

Além dos arquivos `::DataSpace/*`, um CHM gerado por HTML Help Workshop contém:

| Arquivo interno              | Função                                                                 |
|------------------------------|------------------------------------------------------------------------|
| `/default.htm` (ou `index.htm`) | Página inicial exibida ao abrir o CHM                                |
| `/*.htm`, `/*.html`          | Tópicos da ajuda (páginas de conteúdo)                                 |
| `/*.hhc`                     | **HTML Help Contents** — sumário/árvore de navegação (formato HTML com tags `<OBJECT>`, `<PARAM>` e `<UL>/<LI>`) |
| `/*.hhk`                     | **HTML Help Keyword Index** — índice de palavras-chave                 |
| `/*.hhp`                     | Arquivo de projeto (geralmente **não** é compilado para dentro do CHM) |
| `/*.gif`, `/*.png`, `/*.jpg` | Imagens                                                                |
| `/*.css`                     | Folhas de estilo                                                       |
| `/*.js`                      | Scripts (use com cautela — vetor de ataques)                           |
| `::DataSpace/*`              | Metadados de formato (ver seção 6)                                     |

O visualizador usa os arquivos `.hhc`/`.hhk` para montar as abas **Sumário**, **Índice** e **Busca**,
enquanto o índice de texto completo é construído a partir do conteúdo das páginas.

### 7.1 Como o CHM é gerado

1. O autor escreve páginas HTML + um arquivo de projeto `.hhp` (configurações: título, página
   inicial, arquivos incluídos, opções de compilação).
2. O compilador (HTML Help Workshop, HelpNDoc, Help & Manual, Sandcastle etc.) gera `.hhc` e `.hhk`.
3. O compilador monta a estrutura ITSF/ITSP, cria as seções de conteúdo, comprime com LZX e
   grava o diretório PMGL/PMGI — produzindo o `.chm` final.

---

## 8. Leitura e manipulação programática

| Ferramenta / biblioteca       | Linguagem      | Uso                                                    |
|-------------------------------|----------------|--------------------------------------------------------|
| `chmlib` (jedwing)            | C              | Leitura/extração; base de muitos leitores livres        |
| `libmspack` (CHM decoder)     | C              | Decodificador CHM do projeto libmspack ( Samba )        |
| Apache Tika (`chm` parser)    | Java           | Extração de texto e metadados                          |
| `pychm`                       | Python         | Binding de chmlib para Python                          |
| `xCHM`, `KchmViewer`, `ChmSee`| apps GUI       | Visualização em Linux/macOS                             |
| `hh.exe -decompile`           | Windows        | Descompila CHM via ferramenta nativa: `hh.exe -decompile <dir> <arquivo.chm>` |
| 7-Zip / PeaZip                | multi          | Abrem CHM como arquivo (não reconstruem navegação)      |

Exemplo (Windows):

```cmd
hh.exe -decompile C:\tmp\meuchm meuhelp.chm
```

---

## 9. Segurança e limitações

- **Mark of the Web (MOTZ):** o Windows bloqueia CHMs baixados da internet. Para abrir, é preciso
  desbloquear o arquivo (botão direito → Propriedades → Desbloquear) ou remover o fluxo ADS
  `Zone.Identifier`.
- **Scripts/ActiveX:** páginas HTML dentro do CHM podem executar scripts e controles ActiveX;
  historicamente foram usados para malware. Por isso o bloqueio de segurança existe.
- O visualizador nativo `hh.exe` é específico do Windows; em outros sistemas são necessários
  leitores de terceiros, com suporte parcial a recursos de navegação.
- A busca de texto completo e o sumário dependem de `.hhc`/`.hhk` e de índices gerados na
  compilação; descompilar loses esses metadados de navegação se apenas os arquivos brutos
  forem extraídos.

---

## 10. Exemplo ilustrativo de hex dump (início de um CHM v3)

```
00000000  49 54 53 46 03 00 00 00  60 00 00 00 01 00 00 00  |ITSF....`.......|
00000010  xx xx xx xx 09 04 00 00  10 FD 01 7C AA 7B D0 11  |...........|.{...|
00000020  9E 0C 00 A0 C9 22 E6 EC  11 FD 01 7C AA 7B D0 11  |.....".....|.{...|
00000030  9E 0C 00 A0 C9 22 E6 EC  58 00 00 00 00 00 00 00  |....."..X.......|
00000040  18 00 00 00 00 00 00 00  70 00 00 00 00 00 00 00  |........p.......|
00000050  80 02 00 00 00 00 00 00  88 02 00 00 00 00 00 00  |................|
00000060  54 49 54 50 01 00 00 00  54 00 00 00 0A 00 00 00  |ITSP....T.......|
00000070  00 10 00 00 02 00 00 00  02 00 00 00 FF FF FF FF  |................|
...
```

Legenda: `ITSF` → versão 3 → tamanho do header 0x60 → LCID 0x0409 → GUIDs →
tabela de seções (seção 0 @0x58 len0x18; diretório @0x70 len0x280) → offset do conteúdo
@0x288 → `ITSP` com chunk size 0x1000, densidade 2, profundidade 2.

---

## 11. Resumo em camadas

```
CHM (ITSF)
├── Header (ITSF + tabela de seções + offset de conteúdo)
├── Header Section 0 ............ tamanho do arquivo
├── Header Section 1 (ITSP) ..... diretório
│   ├── PMGL (chunks de listagem) → entradas (nome/seção/offset/tamanho)
│   └── PMGI (chunks de índice)   → árvore de busca por nome
└── Content Section 0
    ├── /arquivos.htm, /imagens, /default.htm ...
    ├── /.hhc (sumário), /.hhk (índice)
    └── ::DataSpace/... (NameList, Storage/<Seção>/{Content,ControlData,
                         SpanInfo,Transform/.../ResetTable})
```

---

## 12. Implementação de referência (`chm_builder/`)

O diretório `chm_builder/` contém uma implementação completa em C++17 que gera um CHM ITSF v3
a partir de arquivos pré-existentes. O fluxo é:

1. ler `.hhp`, `.hhc` e `.hhk`;
2. montar os metadados internos (`#SYSTEM`, `#STRINGS`, `#TOCIDX`, `#TOPICS`, `#URLTBL`,
   `#URLSTR`, `#IDXHDR`, `#WINDOWS`);
3. comprimir a seção 1 em LZX com janela de 64 KiB e `resetInterval = 2` frames;
4. gravar a seção 0 (`NameList`, `ControlData`, `SpanInfo`, `ResetTable`, `Content`);
5. construir o diretório PMGL/PMGI e os cabeçalhos ITSF/ITSP.

A seção `[WINDOWS]` do `.hhp` é interpretada para janelas padrão (`HH_WINTYPE`): nome,
título, TOC/índice, tópico padrão/home, estilos do painel, largura, botões, posição,
estilos Win32/estendidos, estado de exibição e campos de navegação comuns. O gerador
calcula `fsValidMembers` quando não informado. *Embedded windows* avançadas/tipos
especiais não são suportados.

### 12.1 Compilação e uso

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    chm_builder/main.cpp chm_builder/chm.cpp chm_builder/lzx.cpp -o chm_builder

chm_builder exemplo/projeto.hhp          # usa Compiled file= do .hhp
chm_builder exemplo/projeto.hhp saida.chm
```

### 12.2 Validação

O fluxo foi validado com os decodificadores reais do libmspack (`lzxd.c` e `chmd.c`) e com o
`hh.exe` do Windows:

- `make test-lzx` decodifica 131.072 bytes (4 frames) sem erros;
- `make test-chm` recupera todos os 7 arquivos do exemplo (7.002 bytes de seção comprimida);
- `dump_all.exe` extrai o CHM gerado com `err=0` para todas as entradas;
- `hh.exe` abre `exemplo/projeto.chm`, exibindo a janela `HH Parent` com o título `Exemplo CHM`;
- os offsets da `ResetTable` apontam para o **início** de cada intervalo de reset, com um
  sentinela final no stream;
- o fluxo LZX termina com 4 bytes zero, necessários para o decodificador processar o frame
  extra de 32 KiB e alinhar o último word.

A correção decisiva para o `hh.exe` foi gravar os **quickrefs dos chunks PMGL/PMGI em ordem
inversa e relativos ao início da área de entradas**. O libmspack ignora essa tabela, mas o
visualizador nativo a utiliza; com os quickrefs errados o arquivo era rejeitado com
`Não é possível abrir o arquivo`, embora todos os bytes internos fossem válidos.

### 12.3 Blobs internos de índice

Os arquivos internos de índice não são mais stubs. O gerador produz:

- `$OBJINST`: dois objetos COM e a tabela de 256 × 10 bytes de `chm_builder/objinst_table.h`.
  O resultado tem 2.715 bytes e é **byte-idêntico** ao blob de referência do HHC.
- `$WWKeywordLinks/{BTree,Data,Map,Property}`: índice binário construído a partir dos pares
  `Name`/`Local` do `.hhk`. O BTree usa blocos de 2.048 bytes com palavras UTF-16LE em ordem,
  cada entrada de dados tem 13 bytes, o Map registra os blocos de listagem e o Property
  contém os oito DWORDs esperados quando existem links.
- `$WWAssociativeLinks/Property`: quatro bytes zero, como no HHC de referência.
- `$FIftiMain`: índice de texto integral gerado das seções `<title>` e `<body>` dos HTMLs.
  As palavras são minúsculas, limitadas a 99 bytes e ordenadas por `CompareStr`, com a forma
  de título à frente da forma de corpo. As folhas e os nós de índice têm 4.096 bytes; as
  listas de ocorrências usam WLC com escalas `2` e raízes `1`, `1` e `5`, gravadas em bits
  MSB-first. Os inteiros comprimidos das folhas usam grupos de 7 bits em ordem little-endian.
- A entrada 4 de `#SYSTEM` passa a anunciar `full-text` e `KLinks` quando os blobs correspondentes
  existem.

No exemplo, os quatro blobs `$WWKeywordLinks` e `$OBJINST` são idênticos aos do HHC de
referência, e o `$FIftiMain` gerado foi percorrido com um decodificador que segue a semântica do
leitor do FPC: 32 palavras, tópicos 0..2 e posições consistentes.

### 12.4 Limitações atuais

- Os IDs gravados em `#URLTBL` são determinísticos (FNV-1a), diferentes dos gerados pelo HHC.
  A navegação usa o índice da tabela, então isso não impede a abertura.
- O parser de `.hhc`/`.hhk` cobre apenas a estrutura usual com `<UL>`, `<LI>` e
  `<OBJECT type="text/sitemap">` com `Name`/`Local`.
- As árvores Huffman são fixas e completas, não adaptativas por frequência. Isso foi uma
  decisão deliberada: elas são válidas para o perfil CHM e eliminaram divergências na
  codificação delta do pretree em arquivos multi-frame.
- Não há suporte a ActiveX, scripts de navegação nem a *embedded windows* avançadas.
  A seção `[WINDOWS]` do `.hhp` tem suporte básico para janelas padrão.

---

## 13. Referências

- Matthew T. Russotto — *Microsoft's HTML Help (.chm) format*: http://www.russotto.net/chm/chmformat.html
- CHM Spec (GNU Savannah / nongnu): https://chmspec.nongnu.org/latest/ITSF.html
- Apache Tika — `org.apache.tika.parser.microsoft.chm` (estruturas `ChmItsfHeader`, `ChmItspHeader` etc.)
- ReactOS `sdk/tools/hhpcomp/chmc/chm.h` (definições ITSF, PMGL, PMGI, LZXC)
- libchm / chmlib — https://github.com/jedwing/chmlib

---

*Documento gerado em 22/11/2026. Conteúdo baseado em engenharia reversa documentada publicamente;
campos marcados como "desconhecido" permanecem sem significado público conhecido.*
