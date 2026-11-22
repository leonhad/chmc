# Formato dos índices internos do CHM

Este documento descreve os arquivos internos usados por busca full-text e índice de palavras-chave no CHM gerado pelo `chm_builder`: `$FIftiMain`, `$OBJINST` e a família `$WW*`.

Os layouts abaixo foram inferidos e validados a partir do código do Free Pascal (`chmfiftimain.pas`, `htmlindexer.pas`), de blobs de referência do HTML Help Workshop/libmspack e do comportamento observado no `hh.exe`. Campos marcados como “desconhecido/reservado” são preservados pelo gerador com valores observados compatíveis.

## Visão geral

| Arquivo | Função | Observação |
|---|---|---|
| `$FIftiMain` | Índice full-text (Full-Text Search) | Árvore de palavras + WLC (Word Location Codes). |
| `$OBJINST` | Objetos/instâncias usados pelo índice | Blob binário fixo em estrutura, com LCID. |
| `$WWKeywordLinks/BTree` | BTree de palavras-chave | Palavras em UTF-16LE, blocos de 2048 bytes. |
| `$WWKeywordLinks/Data` | Dados de keywords | 13 bytes por entrada. |
| `$WWKeywordLinks/Map` | Mapa de blocos do BTree | Lista acumulada de entradas por bloco de listagem. |
| `$WWKeywordLinks/Property` | Propriedades do índice de keywords | 8 DWORDs quando há keywords. |
| `$WWAssociativeLinks/Property` | Propriedades de ALinks | 4 bytes zero no caso observado. |
| `$WWAssociativeLinks/` | Diretório lógico | Entrada de diretório vazia. |
| `$WWKeywordLinks/` | Diretório lógico | Entrada de diretório vazia. |

O CHM também anuncia capacidades na entrada `#SYSTEM` de código `4`: o campo de full-text é não zero quando `$FIftiMain` existe; o campo de KLinks é não zero quando há keywords.

## Convenções

- Inteiros little-endian, salvo indicado.
- `DWORD` = uint32, `WORD` = uint16, `BYTE` = uint8.
- Offsets de arquivo são absolutos dentro do stream, exceto quando indicado.
- `ENCINT-BE-7` aqui significa inteiro comprimido em grupos de 7 bits, com bit 7 como continuação, **ordem little-endian de grupos**: primeiro byte carrega bits 0..6, segundo 7..13 etc. É o formato usado por `WriteCompressedIntegerBE` do FPC para `$FIftiMain`.
- `ENCINT dir` é o inteiro comprimido do diretório CHM/PMGL (big-endian por grupos de 7 bits), **não** usado dentro do `$FIftiMain`.

## `$OBJINST`

`$OBJINST` contém uma lista de dois objetos COM e metadados usados pelo HTML Help para índice/pesquisa. No exemplo de referência, o arquivo tem 2715 bytes e o blob gerado pelo `chm_builder` é byte-idêntico ao do HHC.

### Layout geral

| Offset | Tam | Tipo | Valor/descrição |
|---:|---:|---|---|
| 0x00 | 4 | DWORD | `0x04000000` (assinatura/versão observada) |
| 0x04 | 4 | DWORD | Quantidade de objetos: `2` |
| 0x08 | 4 | DWORD | Offset do objeto 1: `24` |
| 0x0C | 4 | DWORD | Tamanho do objeto 1: `2655` |
| 0x10 | 4 | DWORD | Offset do objeto 2: `2679` |
| 0x14 | 4 | DWORD | Tamanho do objeto 2: `36` |
| 0x18 | ... | bytes | Objeto 1 |
| 0x0A77 | ... | bytes | Objeto 2 |

### Objeto 1

GUID observado: `{4662DAAF-D393-11D0-9A56-00C04FB68BF7}`.

| Offset rel. | Tam | Tipo | Valor/descrição |
|---:|---:|---|---|
| +0x00 | 16 | GUID | GUID do objeto, em formato binário Microsoft. |
| +0x10 | 4 | DWORD | `0x04000000` |
| +0x14 | 4 | DWORD | `3` |
| +0x18 | 4 | DWORD | Codepage: `1252` |
| +0x1C | 4 | DWORD | LCID do projeto (ex.: `0x0416` pt-BR, `0x0409` en-US). |
| +0x20 | 4 | DWORD | `0` |
| +0x24 | 4 | DWORD | `0` |
| +0x28 | 4 | DWORD | `0x00145555` |
| +0x2C | 4 | DWORD | `0x00000A0F` |
| +0x30 | 2 | WORD | `0x0100` |
| +0x32 | 4 | DWORD | `0x00030005` |
| +0x36 | 24 | bytes | Zeros/reservado. |
| +0x4E | 2 | WORD | `0` |
| +0x50 | 2560 | bytes | Tabela `256 x 10` bytes (`kObjInstTable`). |
| +0xA50 | 15 | bytes | Tail observado. |

A tabela `256 x 10` está em `chm_builder/objinst_table.h`. Ela é usada pelo motor de índice/pesquisa do HTML Help para classificação/mapeamento de caracteres.

### Objeto 2

GUID observado: `{4662DAB0-D393-11D0-9A56-00C04FB68B66}`.

| Offset rel. | Tam | Tipo | Valor/descrição |
|---:|---:|---|---|
| +0x00 | 16 | GUID | GUID do objeto. |
| +0x10 | 4 | DWORD | `666` |
| +0x14 | 4 | DWORD | Codepage: `1252` |
| +0x18 | 4 | DWORD | LCID do projeto. |
| +0x1C | 4 | DWORD | `10031` |
| +0x20 | 4 | DWORD | `0` |

## `$WWKeywordLinks/*`

A família `$WWKeywordLinks` implementa o índice binário de palavras-chave (`.hhk`). O gerador cria os arquivos apenas com as entradas que têm `Name` e `Local` resolvível no `#TOPICS`.

### `$WWKeywordLinks/Data`

Arquivo de registros fixos, um por keyword ordenada.

- Tamanho: `13 * N`
- Ordem: case-insensitive por palavra; empate por palavra original.

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 4 | DWORD | Valor observado `0`. |
| 0x04 | 4 | DWORD | Número total de tópicos do CHM (`#TOPICS`/contagem total). |
| 0x08 | 1 | BYTE | `0x80` observado. |
| 0x09 | 4 | bytes | Zeros observados. |

### `$WWKeywordLinks/BTree`

BTree em blocos de 2048 bytes, com header fixo de 76 bytes.

#### Header BTree (76 bytes)

| Offset | Tam | Tipo | Valor/descrição |
|---:|---:|---|---|
| 0x00 | 2 | bytes | Assinatura `3B 29` (`";)"`). |
| 0x02 | 2 | WORD | Flags `0x0104` observado. |
| 0x04 | 2 | WORD | Tamanho do bloco: `2048`. |
| 0x06 | 3 | ASCII | `"X44"` observado. |
| 0x09 | 17 | bytes | Zeros/reservado. |
| 0x1A | 4 | DWORD | Último bloco de listagem (leaf), ou 0 se só um. |
| 0x1E | 4 | DWORD | Último bloco de índice, ou 0/-1 conforme caso observado. |
| 0x22 | 4 | DWORD | `0xFFFFFFFF`. |
| 0x26 | 4 | DWORD | Número total de blocos. |
| 0x2A | 2 | WORD | Profundidade da árvore. |
| 0x2C | 4 | DWORD | Número total de entradas/keywords. |
| 0x30 | 4 | DWORD | Codepage: `1252`. |
| 0x34 | 4 | DWORD | LCID. |
| 0x38 | 4 | DWORD | `1`. |
| 0x3C | 4 | DWORD | `10031`. |
| 0x40 | 12 | bytes | Zeros/reservado até 76 bytes. |

#### Bloco de listagem (leaf)

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 2 | WORD | Espaço livre no final do bloco. |
| 0x02 | 2 | WORD | Número de entradas no bloco. |
| 0x04 | 4 | DWORD | Bloco anterior, ou `-1`. |
| 0x08 | 4 | DWORD | Próximo bloco, ou `-1`. |
| 0x0C | ... | bytes | Entradas de listagem. |
| ... | ... | bytes | Zeros até 2048. |

#### Entrada de listagem

A palavra é armazenada como string UTF-16LE terminada em `0x0000`, convertida de CP1252.

| Campo | Tam | Tipo | Descrição |
|---:|---:|---|---|
| word | variável | UTF-16LE NUL | Palavra-chave. |
| seeAlso | 2 | WORD | `0` quando não há "See Also". |
| depth | 2 | WORD | Profundidade/uso observado `0`. |
| commaCharIndex | 4 | DWORD | `0`. |
| unknown | 4 | DWORD | `0`. |
| pairCount | 4 | DWORD | Número de pares tópico/entrada; gerador usa `1`. |
| topicIndex | 4 | DWORD | Índice do tópico em `#TOPICS`. |
| always1 | 4 | DWORD | `1` observado. |
| dataOffset | 4 | DWORD | Offset no arquivo `Data`, múltiplo de 13. |

#### Bloco de índice

Usado quando há mais de um bloco de listagem. Estrutura semelhante ao leaf:

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 2 | WORD | Espaço livre. |
| 0x02 | 2 | WORD | Número de entradas. |
| 0x04 | 4 | DWORD | Primeiro bloco filho do nível (observado). |
| 0x08 | ... | bytes | Entradas de índice. |

Entrada de índice: como a entrada de listagem, mas o último DWORD aponta para o bloco filho em vez de `dataOffset`.

### `$WWKeywordLinks/Map`

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 2 | WORD | Quantidade de blocos de listagem. |
| 0x02 | N*8 | bytes | Para cada bloco: `DWORD accumulatedEntriesBefore`, `DWORD blockNumber`. |

`accumulatedEntriesBefore` é a contagem acumulada antes do bloco; o gerador calcula por distribuição linear das keywords pelos blocos.

### `$WWKeywordLinks/Property`

Quando há keywords:

| Offset | Tam | Tipo | Valor |
|---:|---:|---|---:|
| 0x00 | 4 | DWORD | 0 |
| 0x04 | 4 | DWORD | 0 |
| 0x08 | 4 | DWORD | 0 |
| 0x0C | 4 | DWORD | 0x0C |
| 0x10 | 4 | DWORD | 1 |
| 0x14 | 4 | DWORD | 1 |
| 0x18 | 4 | DWORD | 0 |
| 0x1C | 4 | DWORD | 0 |

Quando não há keywords, o arquivo pode ser 4 bytes zero.

## `$WWAssociativeLinks/*`

O `chm_builder` gera:

- `$WWAssociativeLinks/` como diretório lógico vazio.
- `$WWAssociativeLinks/Property` com 4 bytes `00 00 00 00`.

Isso é suficiente para compatibilidade com o caso de referência observado.

## `$FIftiMain`

`$FIftiMain` é o índice full-text. Ele mapeia palavras normalizadas para tópicos e posições de palavra (WLC).

### Extração e normalização de palavras

O gerador indexa texto de `<title>` e `<body>` dos tópicos HTML:

- Ignora tags e conteúdo de `<script>` e `<style>`.
- Decodifica entidades HTML básicas (`&amp;`, `&#nn;`, `&#xhh;` etc.).
- Converte para minúsculas.
- Mantém letras/dígitos/underscore e caracteres >= 128 como parte de palavra; demais viram separador.
- Limite máximo: 99 bytes por palavra.
- Cada palavra tem `IsTitle` (veio do título) ou não.
- Posições são contadas em sequência por documento/topico, começando em 0.

Ordenação das entradas:

1. `CompareStr`/ordem binária da palavra normalizada.
2. Para a mesma palavra, entrada com `IsTitle=true` vem antes de `IsTitle=false`.

### Header (1024 bytes)

O arquivo começa com um header de `0x400` bytes. Campos principais:

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 4 | bytes | Assinatura `00 00 28 00`. |
| 0x04 | 4 | DWORD | `HTMLFilesCount`: tópicos/arquivos indexados. |
| 0x08 | 4 | DWORD | Offset do nó raiz. |
| 0x0C | 4 | DWORD | Desconhecido, `0`. |
| 0x10 | 4 | DWORD | `LeafNodeCount`. |
| 0x14 | 4 | DWORD | Cópia do offset do nó raiz. |
| 0x18 | 2 | WORD | `TreeDepth` (1 = só folhas). |
| 0x1A | 4 | DWORD | Desconhecido, `7` observado. |
| 0x1E | 1 | BYTE | `DocIndexScale`, normalmente `2`. |
| 0x1F | 1 | BYTE | `DocIndexRootSize`, normalmente `1`. |
| 0x20 | 1 | BYTE | `CodeCountScale`, normalmente `2`. |
| 0x21 | 1 | BYTE | `CodeCountRootSize`, normalmente `1`. |
| 0x22 | 1 | BYTE | `LocationCodeScale`, normalmente `2`. |
| 0x23 | 1 | BYTE | `LocationCodeRootSize`, normalmente `5` em arquivos observados. |
| 0x2E | 4 | DWORD | `NodeSize`: `4096`. |
| 0x32 | 4 | DWORD | Desconhecido, `0`. |
| 0x36 | 4 | DWORD | Desconhecido, `1`. |
| 0x3A | 4 | DWORD | Desconhecido, `5`. |
| 0x3E | 4 | DWORD | `LongestWordLength`. |
| 0x42 | 4 | DWORD | `TotalWordsIndexed` (ocorrências totais, com duplicatas). |
| 0x46 | 4 | DWORD | `TotalWords` (palavras distintas, contando title/body como entradas distintas). |
| 0x4A | 4 | DWORD | `TotalWordsLengthPart1` (comprimento total com duplicatas, conforme implementação). |
| 0x4E | 4 | DWORD | `TotalWordsLengthPart2`, `0` no gerador atual. |
| 0x52 | 4 | DWORD | `TotalWordsLength` (comprimento de palavras distintas). |
| 0x56 | 4 | DWORD | `FreeSpace` da última folha escrita. |
| 0x5A | 4 | DWORD | Desconhecido, `0`. |
| 0x5E | 4 | DWORD | `HTMLFilesCount - 1` ou `0`. |
| 0x7A | 4 | DWORD | Windows codepage, `1252`. |
| 0x7E | 4 | DWORD | LCID. |

Os campos desconhecidos são gravados com valores compatíveis com FPC/arquivos observados.

### Nós

Todos os nós têm `NodeSize = 4096` bytes.

#### Nó folha (leaf)

Header da folha:

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 4 | DWORD | `NextLeafNode` absoluto; `0` na última folha. |
| 0x04 | 2 | WORD | Desconhecido, `0`. |
| 0x06 | 2 | WORD | `FreeSpace` ao final do nó. |

Entradas começam em `0x08`.

#### Entrada de folha

| Campo | Tam | Tipo | Descrição |
|---:|---:|---|---|
| wordLen | 1 | BYTE | `1 + len(suffix)`. Pode ser 1 em casos especiais; normalmente >=2. |
| prefixLen | 1 | BYTE | Quantidade de caracteres reaproveitados da palavra anterior. |
| suffix | wordLen-1 | bytes | Sufixo da palavra em CP1252/bytes normalizados. |
| isTitle | 1 | BYTE | `1` se título, senão `0`. |
| docCount | var | ENCINT-BE-7 | Número de documentos/tópicos em WLC. |
| wlcOffset | 4 | DWORD | Offset absoluto do bloco WLC no arquivo. |
| reserved | 2 | WORD | `0`. |
| wlcSize | var | ENCINT-BE-7 | Tamanho do bloco WLC em bytes. |

A palavra completa é `previousWord[0:prefixLen] + suffix`.

#### Nó índice

Header do nó índice:

| Offset | Tam | Tipo | Descrição |
|---:|---:|---|---|
| 0x00 | 2 | WORD | `FreeSpace` ao final do nó. |
| 0x02 | ... | bytes | Entradas de índice. |

Entrada de índice:

| Campo | Tam | Tipo | Descrição |
|---:|---:|---|---|
| wordLen | 1 | BYTE | `1 + len(suffix)`. |
| prefixLen | 1 | BYTE | Prefixo reaproveitado da palavra anterior no mesmo nó. |
| suffix | wordLen-1 | bytes | Sufixo da palavra. |
| childOffset | 4 | DWORD | Offset absoluto do nó filho. |
| reserved | 2 | WORD | `0`. |

A chave de índice é a última palavra do bloco filho correspondente.

#### Peculiaridade de flush do FPC

O código FPC, ao fazer flush de um nó de índice com `NewBlockNeeded=True`, escreve o bloco, zera a posição e grava um `DWORD 0` como placeholder. Isso deixa a posição em 4. O próximo `ChildIsFull` só escreve o header inicial quando `Position == 0`; portanto, o próximo bloco pode começar com:

- `WORD freeSpace` em 0
- `WORD 0` em 2 (remanescente do placeholder)
- primeira entrada em 4

O leitor do FPC considera o espaço livre pelo WORD em 0 e percorre entradas a partir de 2; na prática, a entrada em 4 fica dentro do intervalo válido. O `chm_builder` reproduz esse layout para compatibilidade com o leitor/hh.exe.

### WLC (Word Location Codes)

Cada entrada de folha aponta para um bloco WLC. O WLC contém, por documento, o delta de tópico, quantidade de ocorrências e deltas de posições.

Para cada documento, em ordem:

1. `DocDelta = topicIndex - lastDocIndex`, codificado com `ScaleRoot(DocIndexRootSize)`.
2. `NumCodes = número de posições`, codificado com `ScaleRoot(CodeCountRootSize)`.
3. Para cada posição: `LocDelta = position - lastLocCode`, codificado com `ScaleRoot(LocationCodeRootSize)`.
4. `ClearBuffer`: bits são alinhados ao próximo byte após cada documento.

Valores iniciais: `lastDocIndex = 0`, `lastLocCode = 0` para cada documento.

### Codificação ScaleRoot

`WriteScaleRootInt` (FPC) gera bits MSB-first:

- `NeededBits = bitLength(value)`.
- `PrefixBits = max(0, NeededBits - RootSize)`.
- `RootBits = max(NeededBits - 1, RootSize)`.
- `Mask` tem `RootBits` bits baixos setados.
- `Bits = ((~Mask) << 1) | (value & Mask)`, com espaço para um bit divisor zero.
- `Total = PrefixBits + 1 + RootBits`.
- Saída é escrita MSB-first no stream de bits.

Leitura (`ReadWLC`):

- Lê o bit inicial e guarda como bit mais significativo do resultado.
- Conta bits `1` consecutivos como prefixo, até encontrar `0`.
- Consome o `0` divisor.
- Lê `RootSize + max(PrefixBits - 1, 0)` bits restantes, deslocando o resultado para a esquerda.
- `ClearBuffer` descarta bits remanescentes do byte atual após cada documento.

### ENCINT-BE-7 no `$FIftiMain`

Usado para `docCount` e `wlcSize` nas folhas:

- Valor é decomposto em grupos de 7 bits, least-significant group primeiro.
- Cada byte carrega 7 bits no campo baixo; bit 7 é setado em todos os bytes exceto o último.
- Exemplo: `130 = 0x82` em 7 bits baixos + `0x01` no próximo grupo => bytes `82 01`.

Atenção: este formato é diferente do ENCINT do diretório CHM.

## Relação com `#SYSTEM`

A entrada `#SYSTEM` código 4 contém um bloco de 36 bytes com flags/campos observados. O gerador preenche:

- offset 0: LCID
- offset 4: DBCS/flag observada
- offset 8: full-text presente (`1` se `$FIftiMain` não vazio)
- offset 12: KLinks presente (`1` se há keywords)
- offset 16: ALinks (`0`)
- offset 20/24: timestamp/valores observados

## Validação prática

Para o exemplo `exemplo/projeto.chm`:

- `$OBJINST` e `$WWKeywordLinks/{BTree,Data,Map,Property}` são byte-idênticos à referência HHC.
- `$FIftiMain` é decodificável pela semântica do leitor FPC: 32 palavras, tópicos 0..2, posições coerentes.
- `hh.exe` abre o CHM gerado.
- O teste com libmspack (`make test LIBMSPACK=...`) passa.

## Referências

- Free Pascal: `packages/chm/src/chmfiftimain.pas`, `packages/chm/src/htmlindexer.pas`.
- HTML Help Workshop / hh.exe: comportamento de compatibilidade observado.
- libmspack/chmlib: validação de extração e estrutura CHM.
- `chm_builder/chm.cpp`, `chm_builder/objinst_table.h`.