# Formato de arquivo `.hhk` — HTML Help Keyword Index (Índice)

> Parte da família de formatos de código-fonte do **Microsoft HTML Help 1.x**, usados para gerar
> arquivos `.chm`. Veja também `formato_chm.md`, `formato_hhc.md` e `formato_hhp.md`.

---

## 1. O que é

O arquivo `.hhk` (**HTML Help Keyword Index**) é o arquivo-fonte que define as entradas da aba
**Índice** (*Index*) do visualizador CHM. É uma lista de palavras-chave, cada uma apontando para
um tópico HTML.

- Assim como o `.hhc`, é um **sitemap file**: HTML usado como contêiner de dados
  (`<OBJECT type="text/sitemap">` + `<PARAM>`).
- É compilado para dentro do `.chm` pelo `hhc.exe` e referenciado por `Index file=` no `.hhp`.
- Diferente do `.hhc`, o `.hhk` é uma **lista plana** — não há hierarquia por `<UL>` aninhados;
  a ordenação alfabética é feita pelo visualizador.
- Codificação: tradicionalmente a ANSI/DBCS do sistema; UTF-8 é usado por geradores modernos.

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
  <!-- Propriedades globais (opcionais) -->
  <OBJECT type="text/site properties">
      <param name="FrameName" value="main">
      <param name="WindowName" value="main">
      <param name="Font" value="Arial,9,0">
  </OBJECT>

  <!-- Entradas de índice -->
  <UL>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Instalação">
        <param name="Local" value="instalacao.htm">
    </OBJECT></LI>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Instalação">
        <param name="Name" value="Requisitos do sistema">
        <param name="Local" value="instalacao.htm#requisitos">
    </OBJECT></LI>
    <LI><OBJECT type="text/sitemap">
        <param name="Name" value="Atalhos de teclado">
        <param name="Local" value="atalhos.htm">
    </OBJECT></LI>
  </UL>
</BODY>
</HTML>
```

### Elementos

| Elemento | Função |
|----------|--------|
| `<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">` | doctype exigido pelo formato sitemap |
| `<!-- Sitemap 1.0 -->` | marca a versão do formato |
| `<meta name="GENERATOR" ...>` | identifica quem gerou o arquivo |
| `<OBJECT type="text/site properties">` | propriedades globais do índice (opcional) |
| `<UL>` | lista única, contendo todas as entradas |
| `<LI>` | uma entrada de índice |
| `<OBJECT type="text/sitemap">` | dados da entrada |
| `<param name="Name" ...>` | palavra-chave (pode aparecer mais de uma vez) |
| `<param name="Local" ...>` | tópico de destino |

---

## 3. Propriedades globais (`text/site properties`)

A ordem canônica segundo o CHM Spec é:

```
FrameName, WindowName, Font
```

| Parâmetro | Exemplo | Significado |
|-----------|---------|-------------|
| `FrameName` | `main` | Frame HTML onde o tópico deve abrir |
| `WindowName` | `main` | Janela definida em `[WINDOWS]` do `.hhp` |
| `Font` | `Tahoma,9,0` | Fonte usada no painel de índice (nome, tamanho, charset) |

---

## 4. Parâmetros de entrada (`text/sitemap`)

| Parâmetro | Obrigatório? | Significado |
|-----------|--------------|-------------|
| `Name` | sim | Palavra-chave exibida na aba Índice |
| `Local` | sim | Caminho relativo do tópico HTML de destino |
| `URL` | não | Alternativa externa (ex.: `https://...`) |
| `FrameName` | não | Frame de destino |
| `WindowName` | não | Janela de destino |
| `Comment` | não | Comentário associado |
| `ImageNumber` | não | Ícone da entrada (mesma numeração do `.hhc`) |

### 4.1 Múltiplos parâmetros `Name`

Um `<OBJECT>` pode conter **mais de um `Name`**. O primeiro é a palavra-chave principal
(exibida na coluna da esquerda); os seguintes funcionam como **sub-entradas** exibidas recuadas
abaixo da palavra-chave, apontando para o mesmo `Local`.

```html
<LI><OBJECT type="text/sitemap">
    <param name="Name" value="Janela">
    <param name="Name" value="Redimensionar janela">
    <param name="Name" value="Fechar janela">
    <param name="Local" value="janelas.htm">
</OBJECT></LI>
```

Entradas com a mesma palavra-chave são **mescladas automaticamente** pelo visualizador; a ordem
de aparição no arquivo não é preservada — o índice é ordenado Alfabeticamente
(case-insensitive) na compilação/exibição.

### 4.2 Caminhos e âncoras

- Caminhos relativos ao diretório do projeto (`.hhp`), com `\` ou `/`.
- Âncoras: `Local="opcoes.htm#idioma"`.
- Destino externo: `Local="https://exemplo.com/faq"` ou `URL="..."`.

---

## 5. Regras de formatação

As mesmas restrições do compilador HTML Help 4.74.8702 se aplicam:

1. `<LI>` e `<OBJECT>` na **mesma linha**.
2. Cada `<param ...>` em uma **linha separada**.
3. Escapar `&`, `<`, `>` e `"` nos valores.
4. Não usar `<UL>` aninhado — o índice é plano.

---

## 6. Exemplo completo

```html
<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<HTML>
<HEAD>
<meta name="GENERATOR" content="Microsoft&reg; HTML Help Workshop 4.1">
<!-- Sitemap 1.0 -->
</HEAD><BODY>
<OBJECT type="text/site properties">
    <param name="WindowName" value="main">
    <param name="Font" value="Tahoma,9,0">
</OBJECT>
<UL>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Backup">
      <param name="Name" value="Restaurar backup">
      <param name="Local" value="backup.htm">
  </OBJECT></LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Configuração">
      <param name="Name" value="Idioma">
      <param name="Local" value="config.htm#idioma">
  </OBJECT></LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Configuração">
      <param name="Name" value="Senha">
      <param name="Local" value="config.htm#senha">
      <param name="ImageNumber" value="19">
  </OBJECT></LI>
  <LI><OBJECT type="text/sitemap">
      <param name="Name" value="Suporte técnico">
      <param name="URL" value="mailto:suporte@exemplo.com">
  </OBJECT></LI>
</UL>
</BODY></HTML>
```

---

## 7. Relação com `Auto Index` e `Binary Index`

- **`Auto Index=Yes`** no `.hhp` faz o compilador coletar automaticamente palavras-chave contidas
  nas páginas HTML (por meio do HTML Help ActiveX control / metatags) e acrescentá-las ao índice.
- **`Binary Index=Yes`** gera uma representação binária do índice dentro do CHM; é o padrão e
  necessário para alguns recursos, mas pode afetar a mesclagem de CHMs.
- A aba **Busca** (full-text search) é independente do `.hhk` — ela indexa o texto integral das
  páginas, controlada por `Full-text search=Yes` no `.hhp`.

---

## 8. Observações

- Como o `.hhc`, o `.hhk` é um formato de dados: aberto diretamente no navegador, exibe apenas
  marcadores, não uma lista formatada.
- Ferramentas como HelpNDoc, FAR HTML, Doxygen e Sphinx geram `.hhk` automaticamente a partir de
  anotações no código-fonte ou das páginas.
- Palavras-chave duplicadas com destinos diferentes são permitidas; o visualizador agrupa as
  entradas sob o mesmo termo.

---

## 9. Referências

- CHM Spec — Sitemap formats (seção 5.7.3, HHK): https://chmspec.nongnu.org/latest/Sitemap.html
- Microsoft Learn — Create an Index File (HTML Help 1.x)
- Microsoft HTML Help Workshop 4.1 — documentação online
- Helpware / FAR HTML — propriedades de itens de índice
