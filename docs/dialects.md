# Other pseudocodes

The extension of the file says which, or `--dialect`; without one, the first words do
(`Algoritmo`, `programa {`, `DECLARE`, a procedure in capitals with its body further
right...):

| | Extension | `--dialect` | Language |
|---|---|---|---|
| P-like | `.plike` | `plike` | [language.md](language.md) |
| [PSeInt](https://pseint.sourceforge.net) | `.psc` | `pseint` | Spanish |
| [VisuAlg](https://visualg3.com.br) | `.alg` | `visualg` | Portuguese |
| Portugol Studio | `.por` | `portugol` | Portuguese |
| Cambridge International (IGCSE, A Level) | `.pse` | `cambridge` | English |
| Cormen, Leiserson, Rivest, Stein: *Introduction to Algorithms* | `.clrs` | `cormen` | English |

Case and accents do not matter in keywords (`Según`, `segun`). All that the tool does
works with all of them: running, going back, the levels, the translation to C.

```
Algoritmo Suma                    algoritmo "soma"              DECLARE Total : INTEGER
    total <- 0                    var total, i: inteiro         Total <- 0
    Para i <- 1 Hasta 5 Hacer     inicio                        FOR I <- 1 TO 5
        total <- total + i        para i de 1 ate 5 faca            Total <- Total + I
    FinPara                          total <- total + i         NEXT I
    Escribir total                fimpara                       OUTPUT Total
FinAlgoritmo                      escreva(total)
                                  fimalgoritmo
```

Variables that are never declared get a type from how they are used. Arrays follow the
rules of each: from 0 in PSeInt and Portugol, with the declared bounds in VisuAlg and
Cambridge.

What is there: variables and arrays, input and output, conditions, choices (`Segun`,
`escolha`, `CASE`), the loops, functions and procedures with parameters by value and by
reference, texts, and the libraries below. Longer programs, with what they print, are in
[tests/dialects](../tests/dialects) and [examples/libraries](../examples/libraries).

## Libraries, files, drawings

| | |
|---|---|
| PSeInt | `rc` `abs` `ln` `exp` `sen` `cos` `tan` `asen` `acos` `atan` `trunc` `redon` `azar` `aleatorio` `Longitud` `Mayusculas` `Minusculas` `Subcadena` `Concatenar` `ConvertirANumero` `ConvertirATexto` `PI` |
| VisuAlg | `abs` `arcsen` `arccos` `arctan` `cos` `sen` `tan` `exp` `int` `log` `logn` `quad` `raizq` `grauprad` `radpgrau` `randi` `asc` `carac` `caracpnum` `numpcarac` `compr` `copia` `maiusc` `minusc` `pos` `pi`; `arquivo "dados.txt"` |
| Portugol Studio | `inclua biblioteca` `Matematica`, `Texto`, `Tipos`, `Util`, `Arquivos`, `Graficos` |
| Cambridge | `LENGTH` `LCASE` `UCASE` `LEFT` `RIGHT` `MID` `SUBSTRING` `INT` `ASC` `CHR` `NUM_TO_STR` `STR_TO_NUM`; `OPENFILE` `READFILE` `WRITEFILE` `CLOSEFILE` `EOF` |

```
programa {
    inclua biblioteca Graficos --> g
    funcao inicio() {
        g.iniciar_modo_grafico(verdadeiro)
        g.definir_dimensoes_janela(80, 40)
        g.definir_cor(g.COR_AZUL)
        g.desenhar_elipse(20, 10, 40, 20, verdadeiro)
        g.renderizar()
    }
}
```

In the interface `d` shows what has been drawn so far, as the program goes on (and
back); `pseudoscope exec --canvas=picture.ppm` saves it. Files are written to the disk
when `exec` ends, never by the interface. `Esperar`, `aguarde`, `limpatela` and the like
are accepted and do nothing: time here is made of steps.

Not there: the keyboard, the mouse, sounds and the network of Portugol Studio, and its
calendar. What comes from these libraries runs here and is not translated to other
languages, except for the functions on numbers and the plainest ones on texts.

## Cormen

The pseudocode of *Introduction to Algorithms*, as the book prints it:

```
INSERTION-SORT(A, n)
    for i = 2 to n
        key = A[i]
        j = i - 1
        while j > 0 and A[j] > key
            A[j + 1] = A[j]
            j = j - 1
        A[j + 1] = key
```

Nothing is declared: what is a number and what is an array is worked out from how it is
used. A block is what stands further right, and the numbers the book puts before the
lines may stay. `r` and `R` are two names. The statements outside the procedures are the
program.

| | |
|---|---|
| Procedures | `NAME(A, n)`, with hyphens in the name; `return`, with one value, several (`return (q, t)`, taken with `(a, b) = F(x)`) or none |
| Statements | `for … to`, `downto`, `by`; `for each x in A`; `while`; `repeat … until`; `if`, `elseif`, `else`; the `do` and `then` of the older editions |
| Assignment | `=` or `←`, also `i = j = e`; `exchange A[i] with A[j]` |
| Arrays | `A[i]`, `A[i, j]`; `let A[1:n] be a new array`, `let L[0:n] and R[0:m] be new arrays`, from 0 or from 1; `A.length`, as the third edition writes, or the length passed, as the fourth does |
| Objects | `x.key`, `x.next`, `L.head`, `T.root`, `NIL`; `let x be a new node` makes one |
| Expressions | `==`, `≠`, `≤`, `≥`, `and`, `or`, `not` (which stop as soon as they know), `mod`, `⌊ ⌋`, `⌈ ⌉`, `∞`, `TRUE`, `FALSE` |
| Others | `print`, `error "…"`, comments after `//` or `▷` |

Nothing says what an object is made of: there is one kind of object, with every
attribute that the program names, and what an attribute holds (a number, a text,
another object) is worked out from what it is given. So lists, trees and whatever is
made of nodes that point to one another run, are drawn with their arrows, and are
translated: see `examples/cormen/structures.clrs`. At most 256 objects are made in a
run.

In LaTeX, the `codebox` environment of the `clrscode` packages, with which the book is
typeset, is read too: `\Procname`, `\li`, `\For`, `\Do`, `\End`, `\attrib` and the
others.

It is shown, and quoted in errors, as it was written; it runs in a rewriting, so a
message may name a word as another pseudocode would (`'endwhile'`), and a procedure is
listed with `_` where its name has `-`. A procedure that asks for `A.length` is listed
with one more thing it takes, `A_length`. Not read yet, and refused saying so: sets
(`∪`, `∈`, `∅`) and `for each` over anything but an array, so the graph algorithms
with `G.V` and `G.Adj[u]`; arrays inside objects (`x.c[i]`, of the B-trees).
