# Formato de arquivo `.hhc` — HTML Help Contents (Sumário)

> Parte da família de formatos de código-fonte do **Microsoft HTML Help 1.x**, usados para gerar
> arquivos `.chm`. Veja também `formato_chm.md`, `formato_hhk.md` e `formato_hhp.md`.

---

## 1. O que é

O arquivo `.hhc` (**HTML Help Contents**) é um **arquivo HTML usado como fonte de dados**, não como
página para navegador. Ele define a árvore de tópicos exibida na aba **Sumário** (*Contents*) do
painel de navegação do visualizador CHM.

- É um dos chamados **"sitemap files"** — formato proprietário da Microsoft (proposto ao W3C,
  nunca padronizado) baseado em HTML com `<OBJECT>`/`<PARAM>`.
- É **compilado para dentro do `.chm`** pelo compilador (`hhc.exe`);
  depois da compilação, o `.hhc` normalmente não precisa ser distribuído.
- Codificação de caracteres: geralmente a página de código do sistema (ANSI/DBCS); versões
  modernas podem usar UTF-8 com `<meta charset>`.
- Um `.hhc` por projeto é o normal; é possível mesclar sumários de outros CHMs com a sintaxe
  `arquivo.chm::/outro.hhc`.

---

## 2. Estrutura geral

```html
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD>
<BODY>
  <!-- 1) Propriedades GLOBAIS do sumário -->
  <OBJECT type="text/site properties">
      <param name="Window Styles" value="0x800627">
      <param name="ImageType" value="Folder">
      <param name="Font" value="Arial,9,0">
  </OBJECT>

  <!-- 2) Árvore de entradas -->
  <UL>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Título do capítulo">
        <param name="Local" value="capitulo1.htm">
        <param name="ImageNumber" value="1">
    </OBJECT>
      <UL>
        <LI><OBJECT type="text/sitemap">
            <param name="Name" value="Tópico filho">
            <param name="Local" value="capitulo1\topico.htm">
        </OBJECT></LI>
      </UL>
    </LI>
  </UL>
</BODY>
</HTML>
```

### Elementos

| Elemento | Função |
|----------|--------|
| `<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">` | doctype clássico exigido pelo formato sitemap |
| `<!-- Sitemap 1.0 -->` | comentário de versão; identifica o formato para o compilador |
| `<meta name="GENERATOR" ...>` | informação do gerador (HTML Help Workshop, HelpNDoc, Doxygen, Sphinx etc.) |
| `<HEAD>` / `<BODY>` | estrutura HTML padrão |
| `<OBJECT type="text/site properties">` | **uma vez**, no início do `<BODY>`: propriedades globais |
| `<UL>` / `<LI>` | hierarquia: um `<UL>` filho dentro de um `<LI>` cria um nó expandível |
| `<OBJECT type="text/sitemap">` | **uma por entrada** da árvore; contém os `<param>` da entrada |
| `<param name="..." value="...">` | atributos da entrada (nome, arquivo, ícone etc.) |

> **Importante:** a hierarquia é puramente estrutural. Um `<LI>` que contém um `<UL>` aninhado
> torna-se um "capítulo"/pasta; um `<LI>` sem filhos é um tópico folha.

---

## 3. Propriedades globais (`text/site properties`)

Ordem canônica observada (CHM Spec):

```
FrameName, WindowName, Information Types, ImageList, Image Width, Color Mask,
Background, Foreground, ExWindow Styles, Window Styles, ImageType, Font
```

| Parâmetro | Exemplo | Significado |
|-----------|---------|-------------|
| `FrameName` | `left` | Frame HTML onde os tópicos devem abrir |
| `WindowName` | `main` | Janela definida no `[WINDOWS]` do `.hhp` onde abrir |
| `Information Types` | — | Metadados de audiência/tipo (SaveType, Type, TypeDesc etc.) |
| `ImageList` | `icones.bmp` | BMP com vários ícones do mesmo tamanho em uma faixa horizontal |
| `Image Width` | `16` | Largura de cada ícone no `ImageList` (o HHW ignora o valor na leitura e mostra 20) |
| `Color Mask` | `0xFF` | Máscara de cor (usada por ferramentas como FAR; afeta Background/Foreground) |
| `Background` | `0xffffff` | Cor de fundo da árvore (padrão branco) |
| `Foreground` | `0x0` | Cor do texto da árvore (padrão preto) |
| `ExWindow Styles` | `0x6300` | DWORD de estilos estendidos do Win32 (`WS_EX_*`) |
| `Window Styles` | `0x800627` | DWORD de estilos de janela do Win32 (`WS_*`) |
| `ImageType` | `Folder` | `Folder` usa ícones de pasta; sem ele, usa livro/página com `?` |
| `Font` | `Arial,9,0` | Fonte, tamanho, charset |
| `Auto Generated` | `Yes` | Emitido quando o HHW gera o sumário automaticamente (`Auto TOC` no HHP) |

---

## 4. Parâmetros de cada entrada (`text/sitemap`)

Ordem canônica: `Merge`, `Name`, `Type`, `Local`, `URL`, `FrameName`, `WindowName`, `Comment`, `New`, `ImageNumber`.

| Parâmetro | Obrigatório? | Significado |
|-----------|--------------|-------------|
| `Name` | sim | Texto exibido na árvore do Sumário |
| `Local` | sim* | Caminho relativo do arquivo de tópico (ex.: `htm\topico.htm`) |
| `URL` | não | Alternativa a `Local` para abrir uma URL externa (ex.: `http://...`) |
| `ImageNumber` | não | Ícone da entrada (ver tabela abaixo) |
| `New` | `Yes` | Marca a entrada como "nova" (adiciona o sufixo *New* ao ícone) |
| `Comment` | não | Comentário associado à entrada |
| `FrameName` | não | Abre o tópico em um frame específico |
| `WindowName` | não | Abre o tópico em uma janela específica |
| `Type` | não | Tipo de informação (audiência) da entrada |
| `Merge` | não | Referência para sumário de outro CHM |

\* `Local` ou `URL` deve estar presente para que o clique leve a algum lugar; entradas apenas com `Name`
funcionam como "capítulo" (nó sem destino).

### 4.1 Valores de `ImageNumber` (ícones padrão)

Numeração 1-based (extraível de `hxvz.dll`):

| Nº | Ícone | Nº | Ícone |
|----|-------|----|-------|
| 1 | Livro fechado | 2 | Livro aberto |
| 3 | Livro fechado "New" | 4 | Livro aberto "New" |
| 5 | Pasta fechada | 6 | Pasta aberta |
| 7 | Pasta fechada "New" | 8 | Pasta aberta "New" |
| 9 | Página com `?` (query) | 10 | Query "New" |
| 11 | Texto | 12 | Texto "New" |
| 13 | Documento web | 14 | Documento web "New" |
| 15 | Link web | 16 | Link web "New" |
| 17 | Info | 18 | Info "New" |
| 19+ | Link, Booklet, E-mail, Imagem, Áudio, Vídeo, Índice, Ideia, Nota, Ferramenta... | | |

Quando `ImageType=Folder` está definido globalmente, pastas/livros usam ícones de pasta.

### 4.2 Caminhos e âncoras

- Caminhos são **relativos ao diretório do projeto** (`.hhp`), usando `\` ou `/`.
- Âncoras HTML são permitidas: `Local="topic.htm#secao2"`.
- Links externos: `Local="http://www.exemplo.com/"` ou via `URL`.
- Mesclar outro CHM: `Merge="outro.chm::/indice.hhc"`.

---

## 5. Regras de compilation / compatibilidade

O compilador HTML Help 4.74.8702 é sensível à formatação:

1. `<LI>` e `<OBJECT>` devem ficar **na mesma linha**.
2. Cada `<param ...>` deve ficar em **uma linha separada**.
3. `<OBJECT>` de uma entrada deve ser fechado antes do `<UL>` filho.
4. Valores de atributos devem ter `&`, `<`, `>` e `"` escapados (`&amp;`, `&lt;`, `&gt;`, `&quot;`).
5. Linhas muito longas são problemáticas no HHW (o `.hhp` tem limite de 1024 caracteres;
   aplica-se cautela semelhante aqui).

Geradores automáticos (Doxygen, Sphinx, HelpNDoc, FAR HTML) seguem essas convenções.

---

## 6. Exemplo completo (com hierarquia e ícones)

```html
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD><BODY>
<OBJECT type="text/site properties">
    <param name="Window Styles" value="0x800627">
    <param name="ImageType" value="Folder">
    <param name="Font" value="Tahoma,9,0">
</OBJECT>
<UL>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Guia do Usuário">
      <param name="Local" value="index.htm">
      <param name="ImageNumber" value="5">
  </OBJECT>
    <UL>
      <LI><OBJECT type="text/sitemap">
          <param name="Name" value="Instalação">
          <param name="Local" value="instalacao.htm">
          <param name="ImageNumber" value="11">
      </OBJECT></LI>
      <LI><OBJECT type="text/sitemap">
          <param name="Name" value="Primeiros passos">
          <param name="Local" value="inicio.htm#passos">
          <param name="ImageNumber" value="11">
          <param name="New" value="Yes">
      </OBJECT></LI>
    </UL>
  </LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Referência online">
      <param name="URL" value="https://exemplo.com/docs">
      <param name="ImageNumber" value="15">
  </OBJECT></LI>
</UL>
</BODY></HTML>
```

---

## 7. Observações

- Um navegador comum **não renderiza** os `<param>` — por isso um `.hhc` aberto diretamente
  mostra apenas marcadores. Ele é um formato de dados, não uma página.
- `Binary TOC=Yes` no `.hhp` gera uma representação binária do sumário no CHM (necessária para
  certos recursos, mas incompatível com mesclagem e ícones personalizados).
- O `.hhc` é referenciado pela chave `Contents file=` no `[OPTIONS]` do `.hhp`.

---

## 8. Referências

- CHM Spec — Sitemap formats: https://chmspec.nongnu.org/latest/Sitemap.html
- Microsoft Learn — Create a Table of Contents File
- Microsoft HTML Help Workshop 4.1 — documentação online (sumário, índice, projeto)
- Helpware / FAR HTML — HHC item properties
