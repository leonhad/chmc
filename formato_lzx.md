# Formato de Compressão LZX (Microsoft LZX / LZXD)

Documentação técnica detalhada do algoritmo e do formato de fluxo LZX, conforme usado em CAB, CHM,
WIM, NTFS (WOF) e na extensão LZX DELTA (MS-PATCH).

---

## 1. Visão geral

| Propriedade | Valor |
|-------------|-------|
| Nome | LZX (Microsoft LZX); variante estendida: **LZXD / LZX DELTA** |
| Criadores | Jonathan Forbes e Tomi Poutanen; adaptado e proprietarizado pela Microsoft |
| Família | LZ77 + codificação de Huffman canônica (semelhante ao Deflate em espírito, mas incompatível) |
| Janela deslizante | Clássico: 2¹⁵ a 2²¹ bytes (32 KiB – 2 MiB); LZXD: 2¹⁷ a 2²⁵ (128 KiB – 32 MiB) |
| Tamanho de match | 2 a 257 bytes (clássico); até 33 024 bytes no LZX DELTA |
| Frame | 32 768 bytes (0x8000) no LZX com reset (CHM) |
| Ordem de bits | Bits são lidos do **MSB** para o LSB; bytes agrupados em **words little-endian de 16 bits** |
| Usado em | `.cab`, `.chm`, `.wim`, NTFS "System Compressed" (WOF), `.lit`, patchs MS-PATCH |

O LZX **não é** Deflate, LZMA nem LZW. É um esquema próprio com três tipos de bloco, múltiplas
árvores de Huffman adaptativas e um pré-processamento opcional para código x86.

> O formato é parcialmente proprietário e nunca foi totalmente publicado pela Microsoft no passado.
> A especificação mais completa e corrigida é **[MS-PATCH] LZX DELTA Compression and
> Decompression**; a implementação de referência mais difundida é a `lzxd.c` do libmspack
> (Stuart Caie), que documenta divergências entre a spec antiga do CAB SDK e a implementação real.

---

## 2. Camadas do fluxo

```
Dados originais
   │
   ├─ (opcional) E8 call translation → converte CALLs relativos x86 em offsets "absolutos"
   │
   ├─ Janela deslizante (dictionary) → DAGMA: literais + matches (offset, length)
   │
   ├─ Tokens codificados com 4 árvores de Huffman canônicas:
   │      Main Tree, Length Tree, Aligned Offset Tree, Pre-Trees
   │
   └─ Bitstream: words little-endian de 16 bits, bits MSB-first
         │
         └─ (em CHM) fragmentado em frames de 32 KiB com realinhamento e reset de estado
```

### 2.1 Parâmetros externos (não armazenados no fluxo)

O decodificador **precisa receber do contêiner**:

| Parâmetro | Origem típica |
|-----------|---------------|
| Tamanho da janela (`window_bits`) | CAB: `typeCompress >> 8 & 31` do CFHEADER/FOLDER; CHM: `ControlData` (32 ou 64 KiB); WIM: cabeçalho do recurso |
| Intervalo de reset | CHM: `ControlData` (em blocos de 0x8000); 0 = sem reset (CAB) |
| Tamanho do arquivo para E8 | CAB: `cbFile` do CFFILE; LZXD: header de 33 bits do fluxo |
| Tamanho total de saída | CAB: `cbFile`; CHM: diretório/ResetTable |

---

## 3. Bitstream

- Os bits são empacotados **do mais significativo para o menos significativo** dentro de cada
  word de 16 bits.
- As words são gravadas em **little-endian** (byte baixo primeiro).
- Em streams com reset (CHM), ao final de cada frame de 32 KiB o fluxo é **realinhado para uma
  fronteira de 16 bits**. O `ResetTable` do CHM guarda os offsets de cada fronteira.
- Blocos do tipo *uncompressed* também exigem alinhamento antes dos campos R0/R1/R2.

Exemplo (da spec MS-PATCH): sequência de bits `a,b,c,...,A,B,C,D` gera bytes com a word
`0x...` cujo bit 15 é o primeiro bit lido.

---

## 4. Estrutura geral

```
[ header E8 (opcional, 1 bit + 32 bits) ]
[ bloco 1 ][ bloco 2 ] ... [ bloco N ]

Bloco:
  ┌─ 3 bits  : tipo (1=verbatim, 2=aligned, 3=uncompressed)
  ├─ 24 bits : tamanho dos dados DESCOMPRIMIDOS representados (máx. 0xFFFFFF)
  └─ dados específicos do tipo
```

### 4.1 Tipos de bloco

| Valor | Nome | Descrição |
|-------|------|-----------|
| 0 | inválido | — |
| **1** | `VERBATIM` | Tokens Huffman; offsets gravados diretamente |
| **2** | `ALIGNED` | Igual a verbatim, com árvore de offset alinhado extra |
| **3** | `UNCOMPRESSED` | Dados literais + estados R0/R1/R2 |
| 4–7 | inválido | — |

### 4.2 Observação sobre o campo de tamanho

Existe uma discrepância histórica documentada:

- **[MS-PATCH]/LZXD:** 3 bits de tipo + **24 bits** de tamanho (MSB primeiro).
- **Spec antiga do CAB SDK / libyal:** 3 bits de tipo + 1 bit "default block size" (32 KiB)
  + 16 bits de tamanho, com um byte extra quando a janela > 32 KiB.

A implementação real do CAB (libmspack) usa o campo de 24 bits. Vale conferir a ferramenta de
referência do contêiner em caso de dúvida.

---

## 5. Blocos comprimidos (VERBATIM e ALIGNED)

### 5.1 Cabeçalho do bloco

Ordem exata dos campos (conforme implementação):

1. **Aligned Offset Tree** (somente tipo 2): 8 códigos de **3 bits** cada = 24 bits.
2. **Main Tree, parte 1** (literais): Pré-árvore de 20×4 bits + comprimentos codificados para
   os símbolos 0–255.
3. **Main Tree, parte 2** (matches): Pré-árvore de 20×4 bits + comprimentos para os símbolos
   `256 .. 255 + 8*NUM_POSITION_SLOTS` (ex.: 256 + 8×30 = **496 símbolos** com janela de 32 KiB).
4. **Length Tree**: Pré-árvore de 20×4 bits + comprimentos para **249 símbolos**.
5. **Sequência de tokens** (literais e matches) até completar o tamanho do bloco.

> A árvore alinhada vem **antes** das demais na implementação, embora a spec antiga do CAB dissesse
> depois — outra divergência apontada pelo libmspack.

### 5.2 Pré-árvores (Pre-Trees) e codificação dos comprimentos

Cada árvore é transmitida indiretamente: primeiro uma **pré-árvore** de 20 símbolos com
comprimentos fixos de 4 bits; depois os comprimentos reais são codificados com essa pré-árvore.

Símbolos da pré-árvore:

| Símbolo | Significado |
|---------|-------------|
| 0–16 | Comprimento direto: `(comprimento_anterior - símbolo + 17) mod 17` |
| 17 | RLE de zeros: ler 4 bits extras → repetir `4 + valor` zeros |
| 18 | RLE de zeros: ler 5 bits extras → repetir `20 + valor` zeros |
| 19 | RLE de um mesmo comprimento: ler 1 bit (4 ou 5 repetições) + outro símbolo de pré-árvore (4 bits) que dá o valor absoluto |

Os comprimentos são **diferenciais em relação ao bloco anterior** — o estado das árvores
persiste entre blocos (no primeiro bloco, o "anterior" é zero). Isso é uma das fontes de
compactação das tabelas.

Uma árvore com todos os comprimentos zero é permitida para a **Length Tree** (quando o bloco não
contém matches longos), mas não para a Main Tree.

### 5.3 Árvores de Huffman canônicas

Dado o vetor de comprimentos (em bits) por símbolo:

- Ordena-se por comprimento crescente; em caso de empate, por número de símbolo crescente.
- Os códigos são atribuídos sequencialmente (o código de comprimento *n+1* é o código *n*
  incrementado e deslocado à esquerda).
- Regra de validade: se um nó tem filhos, todos os nós à sua direita com o mesmo comprimento
  também devem ter filhos (evita códigos "furados").

---

## 6. Tokens: literais e matches

### 6.1 Main Tree — decodificação do símbolo

```
main_element = main_tree.decode()

se main_element < 256:
    → LITERAL: emitir byte main_element
senão:
    m = main_element - 256
    length_header = m & 7          ; 3 bits
    position_slot = m >> 3         ; número do slot de offset
```

### 6.2 Comprimento do match

| `length_header` | Comprimento |
|-----------------|-------------|
| 0 | 2 |
| 1 | 3 |
| 2 | 4 |
| 3 | 5 |
| 4 | 6 |
| 5 | 7 |
| 6 | 8 |
| **7** | `9 + símbolo_lido_da_Length_Tree` (9 … 257) |

A **Length Tree** tem 249 símbolos; o símbolo `i` representa comprimento `i + 9`
(logo, 248 → 257). Matches de 2 a 8 bytes usam apenas o cabeçalho — sem bits extras.

> No LZX DELTA, o comprimento 257 sinaliza uma etapa extra que permite matches de até 33 024 bytes.

### 6.3 Offsets: slots, footer e deslocamentos repetidos

O offset de match é convertido em **formatted offset**:

```
se offset == R0 → 0
se offset == R1 → 1
se offset == R2 → 2
senão           → offset + 2
```

O formatted offset é dividido em **position slot** (base) + **position footer** (bits extras):

```
extra_bits(slot) = 0                       se slot < 4
                 = min(17, slot/2 - 1)     caso contrário
offset = position_base[slot] + footer - 2
```

Tabela `position_base` para janela de 32 KiB (30 slots):

| Slot | Base | Extra | Slot | Base | Extra |
|------|------|-------|------|------|-------|
| 0 | 0 | 0 | 15 | 192 | 6 |
| 1 | 1 | 0 | 16 | 256 | 7 |
| 2 | 2 | 0 | 17 | 384 | 7 |
| 3 | 3 | 0 | 18 | 512 | 8 |
| 4 | 4 | 1 | 19 | 768 | 8 |
| 5 | 6 | 1 | 20 | 1024 | 9 |
| 6 | 8 | 2 | 21 | 1536 | 9 |
| 7 | 12 | 2 | 22 | 2048 | 10 |
| 8 | 16 | 3 | 23 | 3072 | 10 |
| 9 | 24 | 3 | 24 | 4096 | 11 |
| 10 | 32 | 4 | 25 | 6144 | 11 |
| 11 | 48 | 4 | 26 | 8192 | 12 |
| 12 | 64 | 5 | 27 | 12288 | 12 |
| 13 | 96 | 5 | 28 | 16384 | 13 |
| 14 | 128 | 6 | 29 | 24576 | 13 |

Slots ≥ 30 existem para janelas maiores (32, 34, 36, 38, 42, 50 slots para 2¹⁶…2²¹).
A regra real é "primeiro slot cuja base ≥ tamanho da janela", o que explica valores como 42/50
em implementações, apesar da tabela antiga do SDK dizer 40/42.

### 6.4 Offsets repetidos R0, R1, R2

- Slots **0, 1 e 2** reutilizam os três offsets mais recentes.
- Após usar R1 ou R2, há um **swap com R0** (não é uma fila LRU completa — é um quirk deliberado):

```
slot 0 → offset = R0                    (swap R0↔R0 = no-op)
slot 1 → offset = R1;  troca R0 ↔ R1
slot 2 → offset = R2;  troca R0 ↔ R2
```

- Após um offset **explícito** (slot ≥ 3): `R2 = R1; R1 = R0; R0 = offset`.
- O slot 3 (formatted offset 3) corresponde ao offset real **1**.
- Os valores R0/R1/R2 são reiniciados em blocos do tipo *uncompressed* (vêm gravados no header).

### 6.5 Aligned Offset Tree (blocos ALIGNED)

Quando `extra_bits ≥ 3`, os bits extras são divididos:

- Os **3 bits de menor ordem** vêm da **Aligned Offset Tree** (8 símbolos, 3 bits de comprimento
  cada no cabeçalho).
- Os bits restantes (`extra_bits - 3`) são lidos diretamente do fluxo, MSB primeiro.

Para `extra_bits = 1` ou `2`, os bits são lidos diretamente (sem árvore alinhada).
Essa divisão melhora a compressão porque offsets em código x86 tendem a ser múltiplos de 8.

---

## 7. Blocos UNCOMPRESSED

Quando os dados não comprimem, o bloco armazena bytes crus:

1. Após os 3 bits de tipo e o tamanho, o fluxo é preenchido com zeros até fronteira de **16 bits**.
2. Se já estava alinhado, insere-se uma **word zero completa** (1–16 bits de padding).
3. Gravam-se **R0, R1 e R2** como DWORDs little-endian (12 bytes).
4. Em seguida vêm os bytes literais do bloco; se o total de bytes for ímpar, adiciona-se 1 byte zero.

Exemplo da especificação (compressão de `"abc"` com R0=R1=R2=1):

```
Bits/valor                                   Hex
16: 0x0014  chunk size = 20 bytes            14 00
 1: 0       E8 desabilitado                  --
 3: 3       bloco uncompressed               --
24: 0x000003 block size = 3 bytes            00 30 00  (bits)
 4: 0000    padding p/ word-align            --
 4 bytes: R0 = 1                             01 00 00 00
 4 bytes: R1 = 1                             01 00 00 00
 4 bytes: R2 = 1                             01 00 00 00
 3 bytes: "abc"                              61 62 63
 1 byte : padding                            00

Sequência: 14 00 00 30 30 00 01 00 00 00 01 00 00 00 01 00 00 00 61 62 63 00
```

---

## 8. E8 Call Translation (pré-processamento x86)

Objetivo: melhorar a compressão de código 32-bit x86, onde o opcode `0xE8` (CALL relativa) é
seguido por um deslocamento de 32 bits. Como o deslocamento muda se o código é realocado, o LZX
converte esse valor em algo próximo de um endereço absoluto — o que costuma repetir-se mais.

### 8.1 Ativação

No **primeiro bit do primeiro frame/chunk** do fluxo:

| Bit | Significado |
|-----|-------------|
| 0 | E8 translation desabilitada |
| 1 | Habilitada; seguem dois campos de 16 bits (high word, low word) com `E8_file_size` |

A tradução é desabilitada automaticamente após o chunk 32 768 (1 GiB de dados).
Não se deve traduzir ocorrências de 0xE8 nos últimos 6–10 bytes de um frame.

### 8.2 Pré-processamento (compressão)

Pseudocódigo conforme [MS-PATCH]:

```
se chunk_offset < 0x40000000 e chunk_size > 10:
  para i de 0 até chunk_size - 11:
    se chunk_byte[i] == 0xE8:
      current_pointer = chunk_offset + i
      displacement    = LE32(chunk_byte[i+1 .. i+4])
      target          = current_pointer + displacement
      se 0 <= target < E8_file_size + current_pointer:
        se target >= E8_file_size:
          target = displacement - E8_file_size
        gravar LE32(chunk_byte[i+1..i+4], target)
      i += 4
```

### 8.3 Pós-processamento (descompressão — inverso)

```
value = LE32(chunk_byte[i+1 .. i+4])
se value >= -current_pointer e value < E8_file_size:
  se value >= 0:
    displacement = value - current_pointer
  senão:
    displacement = value + E8_file_size
  gravar LE32(chunk_byte[i+1..i+4], displacement)
```

---

## 9. Estado persistente e frames

| Estado | Comportamento |
|--------|---------------|
| `window[]` (janela deslizante) | Persiste entre blocos; offsets podem referenciar dados anteriores |
| R0, R1, R2 | Persistem; substituídos por valores gravados em blocos uncompressed |
| Comprimentos das árvores | Persistem entre blocos (codificação diferencial) |
| Árvores reconstruídas | A cada bloco comprimido, a partir dos comprimentos |
| Reset (CHM) | A cada `reset_interval` frames de 32 KiB (2 no perfil implementado): realinha bitstream, **reinicia todo o estado LZX** (tabelas Huffman e histórico LZ), relê o header E8 |

Esse reset completo é a principal diferença entre o LZX de CAB (sem reset) e o de CHM (com reset),
e é o que permite **acesso semi-aleatório** a seções comprimidas dentro de um CHM.

---

## 10. Pseudocódigo de decodificação (blocos comprimidos)

```
enquanto bytes_restantes_do_bloco > 0:
    main = main_tree.decode()

    se main < 256:
        window[pos++] = main
        continuar

    m = main - 256
    len_header = m & 7
    slot       = m >> 3

    se len_header == 7:
        length = 9 + length_tree.decode()
    senão:
        length = len_header + 2

    se slot < 3:
        offset = [R0,R1,R2][slot]
        trocar([R0,R1,R2][slot], R0)
    senão:
        extra  = extra_bits(slot)
        base   = position_base[slot] - 2
        se tipo == ALIGNED e extra >= 3:
            footer = (aligned_tree.decode()) | (read_bits(extra-3) << 3)
        senão:
            footer = read_bits(extra)
        offset = base + footer
        R2 = R1; R1 = R0; R0 = offset

    copiar `length` bytes de window[pos-offset] para window[pos]
    pos += length
```

---

## 11. Variantes por contêiner

| Contêiner | Janela | Reset | E8 | Observações |
|-----------|--------|-------|----|-------------|
| **CAB** | 2¹⁵–2²¹ (`typeCompress`) | não | opcional (tamanho do arquivo no CFFILE) | Blocos podem cruzar CFDATA; árvores persistem por folder |
| **CHM** | 2¹⁵ ou 2¹⁶ (32/64 KiB) | sim, intervalo em `ControlData` | geralmente desabilitado (HTML) | `ResetTable` guarda offsets dos frames; padded a 0x8000 |
| **WIM** | 2¹⁵–2²¹ | não | sim, com `E8_file_size = 12000000` para arquivos WIM | Usa `IMAGE_RESOURCE` com `WIM_COMPRESSED` |
| **NTFS WOF** | 32 KiB | — | — | "System Compressed" do Windows 10+ |
| **LZX DELTA (LZXD)** | 2¹⁷–2²⁵ | chunked | sim, com header de 33 bits | Suporta dados de referência externos e matches longos |

### 11.1 Diferenças do LZX DELTA

- Fluxo dividido em **chunks** com prefixo de 16 bits (tamanho do chunk).
- Primeiro bit do primeiro chunk ativa E8 e traz `E8_file_size` (2×16 bits).
- Janela mínima 128 KiB, máxima 32 MiB.
- Match de comprimento 257 ativa campo extra → até 33 024 bytes.
- Offsets podem apontar para **dados de referência** fornecidos pelo chamador (além do já decodificado).

---

## 12. Constantes principais

```c
LZX_MIN_MATCH              = 2
LZX_MAX_MATCH              = 257
LZX_NUM_CHARS              = 256
LZX_NUM_PRIMARY_LENGTHS    = 7      /* comprimentos 2..8 diretos */
LZX_NUM_SECONDARY_LENGTHS  = 249    /* símbolos da Length Tree */
LZX_PRETREE_NUM_ELEMENTS   = 20
LZX_ALIGNED_NUM_ELEMENTS   = 8
LZX_BLOCKTYPE_VERBATIM     = 1
LZX_BLOCKTYPE_ALIGNED      = 2
LZX_BLOCKTYPE_UNCOMPRESSED = 3
LZX_FRAME_SIZE             = 32768
```

---

## 13. Aplicação prática em CHM

Em um CHM (ver `formato_chm.md`):

1. `::DataSpace/Storage/MSCompressed/ControlData` informa `LZXC`, versão, `resetInterval`,
   `windowSize` (em blocos de 0x8000 na v2) e `windowSize`/cache.
2. `.../Transform/{7FC28940-...}/InstanceData/ResetTable` fornece os offsets de cada fronteira
   de frame no fluxo comprimido e os tamanhos comprimido/descomprimido.
3. A cada intervalo de `resetInterval` frames de 32 KiB o estado LZX é **reiniciado** — é por
   isso que um único tópico HTML pode ser extraído sem descomprimir a seção inteira.

---

## 14. Perfil implementado em `chm_builder/lzx.cpp`

O compressor incluído neste repositório usa o perfil mais restritivo e interoperável para CHM:

| Item                         | Valor adotado                                                   |
|------------------------------|-----------------------------------------------------------------|
| Janela                       | `2^16` = 65536 bytes (`ControlData.windowSize = 2`)             |
| Frame                        | 32768 bytes (`0x8000`), sempre zero-padded                      |
| `resetInterval`              | 2 frames (0x10000 bytes entre resets)                           |
| Reset                        | estado LZX zerado no início de cada intervalo de 2 frames       |
| E8                           | desabilitado; bit 0 no início de cada intervalo                  |
| Bloco                        | VERBATIM (`1`), um por frame de 32 KiB                          |
| Árvores                      | transmitidas em cada bloco; deltas relativos ao bloco anterior  |
| Pretree                      | fixo: 12 códigos de 4 bits + 8 de 5 bits                        |
| Main tree                    | fixa: 16 códigos de 8 bits + 480 de 9 bits                      |
| Length tree                  | fixa: 7 códigos de 7 bits + 242 de 8 bits                       |
| Aligned offset tree          | ausente (só existe em blocos ALIGNED)                           |
| Tokenização                  | casamentos podem cruzar frames, mas nunca o limite do intervalo |
| Alinhamento                  | `align16()` ao final de cada frame de 32 KiB                    |
| Ordenação dos bits           | MSB-first dentro de words de 16 bits, gravados little-endian    |

Alguns detalhes confirmados com `lzxd.c` são fáceis de errar:

1. Os bits não são gravados como bytes diretos. O leitor monta words de 16 bits em
   little-endian e consome os bits de MSB para LSB; escrever bytes MSB-first gera
   `bad block type` já no primeiro bloco.
2. O `lzxd.c` realinha a entrada no fim de **cada frame de 32 KiB**. Sem `align16()` por
   frame, o bloco seguinte começa em um bit deslocado e a decodificação falha com
   `ERR11` / `bad block type`.
3. Cada frame começa com seu próprio bloco VERBATIM e transmite as árvores, mas os deltas
   são relativos ao bloco anterior. O estado das árvores e do histórico LZ é zerado no
   início do intervalo de reset (2 frames).
4. Um bloco único de 65536 bytes com janela de 64 KiB é rejeitado com `erro=11`; por isso
   a seção é dividida em blocos de 32768 bytes.
5. Após o último frame de dados, o decodificador ainda processa um frame de 32 KiB com
   tamanho zero. O gerador acrescenta 4 bytes zero para esse cabeçalho final. Sem eles,
   `lzxd_decompress()` termina com `MSPACK_ERR_DECRUNCH` mesmo tendo produzido todos os
   bytes esperados.
6. `resetOffsets` contém offsets de **início de intervalo** e um sentinela apontando para
   o fim do fluxo comprimido. Usar offsets de fim de intervalo faz o `chmd.c` perder a
   sincronização ao extrair arquivos posicionados após uma fronteira de reset.

### 14.1 Validação

O compressor foi testado contra `lzxd_init(..., 16, 2, ...)` / `lzxd_decompress()` do libmspack:

- `make test-lzx` gera e decodifica 131.072 bytes (4 frames), com `erro=0` e
  `decoded=131072 esperado=131072`;
- o CHM de exemplo é extraído por `test_chm.exe` e `dump_all.exe` sem erros;
- o `hh.exe` abre o CHM gerado, confirmando que o perfil LZX e a `ResetTable` são aceitos
  pelo visualizador nativo do Windows.

---

## 15. Referências

- **[MS-PATCH] LZX DELTA Compression and Decompression** (Microsoft Open Specs):
  https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/
- **E8 Call Translation** (MS-PATCH):
  https://learn.microsoft.com/en-us/openspecs/exchange_server_protocols/ms-patch/e6c8366d-a8c3-4f34-aa5d-56f44f938a09
- **libmspack — lzxd.c** (Stuart Caie): https://github.com/kyz/libmspack/blob/master/libmspack/mspack/lzxd.c
- **libyal — Compression methods**: https://github.com/libyal/libfwnt/blob/main/documentation/Compression%20methods.asciidoc
- **LZXFMT — Microsoft LZX Data Compression Format** (cópia arquivada)
- **ReactOS `dll/win32/itss/lzx.c`** — decodificador usado pelo ITSS.DLL

---

*Documento gerado em 22/11/2026. Detalhes derivados de especificação pública e engenharia reversa;
divergências históricas entre a spec do CAB SDK e a implementação real foram registradas quando
relevantes.*
