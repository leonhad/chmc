# Formato de arquivo `.hhp` — HTML Help Project (Projeto CHM)

> Parte da família de formatos de código-fonte do **Microsoft HTML Help 1.x**, usados para gerar
> arquivos `.chm`. Veja também `formato_chm.md`, `formato_hhc.md` e `formato_hhk.md`.

---

## 1. O que é

O arquivo `.hhp` (**HTML Help Project**) é o **arquivo de projeto** usado pelo compilador da
Microsoft (`hhc.exe`, com `hha.dll`) para produzir um `.chm`.

- É um **arquivo INI em texto puro**: seções `[NOME]`, linhas `chave=valor`, comentários com `;`.
- Codificação de quebras de linha: **CRLF** (padrão INI do Windows).
- Todas as seções são opcionais na teoria, mas `[OPTIONS]` e `[FILES]` aparecem em todo projeto
  real. `[WINDOWS]` é necessário janelas personalizadas.
- Caminhos são **relativos ao diretório que contém o `.hhp`**.
- O HTML Help Workshop (HHW) falha se qualquer linha tiver **1024 ou mais caracteres** (sem contar CRLF).

Compilação típica:

```cmd
"C:\Program Files (x86)\HTML Help Workshop\hhc.exe" meuProjeto.hhp
```

O resultado é `Compiled file=` (ex.: `meuProjeto.chm`) e um log (se `Error log file=` estiver definido).

---

## 2. Seções

| Seção | Finalidade |
|-------|------------|
| `[OPTIONS]` | Configurações gerais do projeto e do compilador |
| `[WINDOWS]` | Definições das janelas do visualizador (tamanho, botões, abas, painel) |
| `[FILES]` | Lista de arquivos a incluir no CHM (HTML, CSS, imagens etc.) |
| `[MERGE FILES]` | Lista de outros `.chm` cujo índice/busca serão mesclados em tempo de execução |
| `[ALIAS]` | Mapeamento de nomes simbólicos (IDH_*) para tópicos |
| `[MAP]` | Definição dos números de contexto (`#define IDH_* 1001`) |
| `[TEXT POPUPS]` | Arquivos de texto/cabeçalho com textos para pop-ups do ActiveX |
| `[INFOTYPES]` | Tipos de informação/audiência (pouco suporte; HHW não gera) |
| `[SUBSETS]` | Subconjuntos para MSDN Collections (não usado em CHM normal) |

---

## 3. Seção `[OPTIONS]`

Chaves mais comuns (a ordem alfabética é a usada pelo HHW):

| Chave | Exemplo | Significado |
|-------|---------|-------------|
| `Compatibility` | `1.1 or later` | Versão compatível do HTML Help |
| `Compiled file` | `meu.chm` | Nome do `.chm` de saída |
| `Contents file` | `meu.hhc` | Arquivo de sumário (deixe vazio para não ter aba Sumário) |
| `Index file` | `meu.hhk` | Arquivo de índice (vazio = sem aba Índice) |
| `Default Window` | `main` | Janela definida em `[WINDOWS]` usada por padrão |
| `Default topic` | `index.htm` | Página exibida ao abrir o CHM |
| `Default Font` | `Tahoma,9,0` | Fonte padrão (nome, tamanho, charset) |
| `Language` | `0x409 English (US)` | LCID do idioma (ex.: `0x416` Português BR, `0x415` PT-PT) |
| `Title` | `Ajuda do Meu App` | Título da janela/ajuda |
| `Full-text search` | `Yes` / `No` | Habilita aba Busca (índice de texto integral) |
| `Full text search stop list file` | `stopwords.stp` | Lista de palavras ignoradas na busca |
| `Binary TOC` | `Yes` / `No` | Sumário binário (necessário para certos recursos; quebra mesclagem/ícones) |
| `Binary Index` | `Yes` / `No` | Índice binário (padrão `Yes`) |
| `Auto Index` | `Yes` / `No` | Coleta automaticamente palavras-chave dos tópicos |
| `Display compile progress` | `Yes` | Mostra progresso na compilação |
| `Display compile notes` | `Yes` | Mostra notas de compilação |
| `Error log file` | `meu.log` | Arquivo de log de erros/avisos |
| `Create CHI file` | `No` | Gera arquivo `.chi` (para coleções MSDN) |
| `Flat` | `No` | Achata a estrutura de diretórios |
| `ImageType` | `Folder` / `Picture` | Estilo padrão de ícones do sumário |

> LCIDs úteis: `0x409` en-US, `0x416` pt-BR, `0x415` pt-PT, `0x804` zh-CN, `0x411` ja-JP.

---

## 4. Seção `[WINDOWS]`

Cada linha define uma janela e tem o formato posicional (argumentos separados por vírgulas,
entre aspas quando necessário):

```ini
[WINDOWS]
main="Ajuda do Meu App","meu.hhc","meu.hhk","index.htm","index.htm",,,,,0x23520,222,0x104E,[10,10,640,480],0xB0000,,,,,,0
```

### Argumentos (índice 0 a 19)

| # | Exemplo | Significado |
|---|---------|-------------|
| 0 | `main` | Nome/tipo da janela |
| 1 | `"Ajuda..."` | Texto da barra de título |
| 2 | `"meu.hhc"` | Arquivo de sumário |
| 3 | `"meu.hhk"` | Arquivo de índice |
| 4 | `"index.htm"` | Tópico principal |
| 5 | `"index.htm"` | Tópico inicial alternativo |
| 6–9 | vazios | Reservados / parâmetros adicionais |
| 10 | `0x63520` | Bit field de estilos do painel de navegação |
| 11 | `220` | Largura do painel de navegação (pixels) |
| 12 | `0x10384e` | Bit field dos botões da barra de ferramentas |
| 13 | `[10,10,640,480]` | Posição inicial `[left, top, right, bottom]` |
| 14 | `0xB0000` | Estilos Win32 (`SetWindowLong` / `CreateWindow`) |
| 15 | `0x0` | Estilos estendidos Win32 (`CreateWindowEx`) |
| 16–18 | vazios | Reservados |
| 19 | `0` | Estado/parâmetro final (flags diversas) |

### 4.1 Bit field de botões (argumento 12)

Valores definidos em `htmlhelp.h` (nem todos funcionam):

| Valor | Botão |
|-------|-------|
| `0x000002` | Mostrar/ocultar painel |
| `0x000004` | Voltar |
| `0x000008` | Avançar |
| `0x000010` | Parar |
| `0x000020` | Atualizar |
| `0x000040` | Início |
| `0x000200` | Notas |
| `0x000400` | Sumário |
| `0x000800` | Localizar |
| `0x001000` | Opções |
| `0x002000` | Imprimir |
| `0x004000` | Índice |
| `0x008000` | Busca |
| `0x010000` | Histórico |
| `0x020000` | Favoritos |
| `0x040000` / `0x080000` | Jump 1 / Jump 2 |
| `0x100000` | Zoom/Fonte |
| `0x200000` / `0x400000` | Próximo tópico / Tópico anterior |

Exemplo: `0x104E` = `0x1000` + `0x40` + `0x8` + `0x4` + `0x2` → Opções + Sumário + Avançar + Voltar + Painel.

### 4.2 Estilos do painel de navegação (argumento 10)

| Valor | Significado |
|-------|-------------|
| `0x00000001` | Ocultar/mostrar painel automaticamente conforme o foco |
| `0x00000002` | Manter janela sempre no topo |
| `0x00000004` | Sem barra de título |
| `0x00000008` | Não usar estilos padrão (`dwStyles`) |
| `0x00000010` | Não usar estilos estendidos padrão (`dwExStyles`) |
| `0x00000020` | Usar janela de três painéis (tri-pane) — recomendado |

---

## 5. Seção `[FILES]`

Lista de arquivos (um por linha) que devem ser compilados para dentro do CHM:

```ini
[FILES]
index.htm
instalacao.htm
htm\opcoes.htm
css\estilo.css
img\logotipo.png
meu.hhc
meu.hhk
```

- O compilador também descobre automaticamente arquivos referenciados por links no sumário,
  índice e páginas HTML — mas incluir explicitamente é mais seguro (especialmente CSS/imagens).
- Caminhos relativos ao diretório do `.hhp`.

---

## 6. Seções `[MAP]` e `[ALIAS]` — ajuda sensível ao contexto

Usadas quando um aplicativo chama `HtmlHelp(..., HH_HELP_CONTEXT, 1001)` para abrir um tópico
a partir de um ID numérico associado a um controle/diálogo.

```ini
[ALIAS]
IDH_HomePage=default.htm
IDH_Instalacao=htm\instalacao.htm
IDH_Opcoes=mychm::htm\opcoes.htm

[MAP]
#define IDH_HomePage 1000
#define IDH_Instalacao 1001
#define IDH_Opcoes 1002
```

Também é possível usar arquivos externos:

```ini
[ALIAS]
#include meuhelp.ali

[MAP]
#include meuhelp.h
```

> Ao usar `#include`, lembre-se de adicionar os arquivos incluídos também à `[FILES]`.

---

## 7. Outras seções

### `[MERGE FILES]`
Lista de CHMs que serão mesclados em tempo de execução (índice e busca combinados):

```ini
[MERGE FILES]
contatos.chm
procedimentos.chm
```

Limite prático: sabe-se que o HHW 4.74.8702 trava com mais de ~1148 entradas; o formato binário
interno armazena no máximo 1004.

### `[TEXT POPUPS]`
Lista de arquivos de texto/cabeçalho com os textos exibidos pelo comando `Popup` do
HTML Help ActiveX control.

### `[INFOTYPES]` / `[SUBSETS]`
Pouco documentadas; o HHW atual não as gera. Relacionadas a tipos de audiência e coleções MSDN.

---

## 8. Exemplo completo e mínimo

```ini
;-----------------------------------------------------------
; Exemplo de projeto HTML Help 1.x
;-----------------------------------------------------------
[OPTIONS]
Compatibility=1.1 or later
Compiled file=MeuApp.chm
Contents file=MeuApp.hhc
Index file=MeuApp.hhk
Default Window=main
Default topic=index.htm
Default Font=Tahoma,9,0
Language=0x416 Portuguese (Brazil)
Title=Ajuda do MeuApp
Full-text search=Yes
Full text search stop list file=stopwords.stp
Binary TOC=No
Binary Index=Yes
Auto Index=Yes
Display compile progress=Yes
Error log file=MeuApp.log

[WINDOWS]
main="Ajuda do MeuApp","MeuApp.hhc","MeuApp.hhk","index.htm","index.htm",,,,,0x63520,220,0x10384E,[10,10,1024,768],0xB0000,,,,,,0

[FILES]
index.htm
instalacao.htm
opcoes.htm
css\estilo.css
img\logo.png
MeuApp.hhc
MeuApp.hhk
stopwords.stp

[MAP]
#define IDH_Inicio 1000
#define IDH_Instalacao 1001

[ALIAS]
IDH_Inicio=index.htm
IDH_Instalacao=instalacao.htm
```

---

## 9. Fluxo de compilação

```
páginas .htm/.html ─┐
arquivo .hhc (sumário) ─┼──> hhc.exe + hha.dll ──> arquivo .chm
arquivo .hhk (índice) ─┤         (usa o .hhp)
imagens/CSS/scripts ───┘
```

O compilador:
1. lê `[OPTIONS]` e `[WINDOWS]`;
2. abre `.hhc` e `.hhk` e resolve os tópicos;
3. inclui os arquivos de `[FILES]` e os referenciados por links;
4. monta o diretório ITSF/ITSP (ver `formato_chm.md`);
5. comprime o conteúdo com LZX e grava o `.chm`;
6. registra avisos/erros em `Error log file`.

---

## 10. Referências

- CHM Spec — INI formats / HHP: https://www.gnu.org/non-gnu/chmspec/latest/INI.html
- Helpware — .HHP File Sections: http://www.helpware.net/FAR/help/dlg_hhpedit_sec.htm
- Microsoft Learn — Create a Help Project / Compile a Help Project (HTML Help 1.x)
- `htmlhelp.h` — constantes `HHWIN_*`, botões e estilos
- Sphinx `htmlhelp` builder — template de `.hhp` e bit fields de botões
