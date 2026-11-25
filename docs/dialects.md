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
| Arrays | `A[i]`, `A[i, j]`; `let A[1:n] be a new array`, `let L[0:n] and R[0:m] be new arrays`, from 0 or from 1; `A.length`, as the third edition writes, or the length passed, as the fourth does; the numbers an array has beside it: `A.heap-size`, `S.top`, `Q.head`, `Q.tail` |
| Objects | `x.key`, `x.next`, `L.head`, `T.root`, `NIL`; `let x be a new node` makes one, `let a, b, c be new vertices` several |
| Sets | `∅`, `{a, b}`, `∪`, `∩`, `−` (or `\\`), `∈`, `∉` (or `in`, `not in`), `⊆`, `⊂`, `⊇`, `⊃`, `\|S\|`, `==`, `≠`; `S = S ∪ {x}`; of whole numbers, reals, texts, objects, pairs (`{(u, v)}`, `(u, v) ∈ G.E`) and other sets (`{{1, 2}, {3}}`); `print S` writes it whole |
| Going through | `for each x in S`, `for each vertex v ∈ G.Adj[u]`, `for each edge (u, v) ∈ G.E` |
| Indexed by anything | `G.Adj[u]`, `x.key[i]`, `x.c[i]`, `w[u, v]` or `w(u, v)`, `d[v]`, `age["ann"]`: by numbers, texts or objects, one index or two |
| In words | `sort the edges of G.E into nondecreasing order by weight w`, `sort list B[i]`, `create a single list of the edges in G.E`, `for each edge (u, v) taken from the sorted list in order`, `for each edge (u, v) ∈ G.E, taken in nondecreasing order by weight`, `insert x into list L`, `add x to S`, `remove x from S`, `if the call of RELAX decreased v.d` |
| Queues and stacks | `ENQUEUE(Q, x)`, `DEQUEUE(Q)`, `PUSH(S, x)`, `POP(S)`, `INSERT(Q, x)`, `EXTRACT-MIN(Q)`, `EXTRACT-MAX(Q)`, `STACK-EMPTY(S)`, `QUEUE-EMPTY(Q)`, when the program has no procedure of that name |
| Expressions | `==`, `≠`, `≤`, `≥`, `and`, `or`, `not` (which stop as soon as they know), `mod`, `⌊ ⌋`, `⌈ ⌉`, `∞`, `TRUE`, `FALSE`; a word in capitals that nothing is ever put in stands for itself: `WHITE`, `GRAY`, `BLACK` |
| Others | `print`, `error "…"`, comments after `//` or `▷` |

Nothing says what an object is made of: there is one kind of object, with every
attribute that the program names, and what an attribute holds (a number, a text,
another object, a set) is worked out from what it is given. So lists, trees and whatever
is made of nodes that point to one another run, are drawn with their arrows, and are
translated: see `examples/cormen/structures.clrs`.

Sets, lists, queues, and what is indexed by anything are collections: of numbers, texts,
objects, pairs of them, or other collections. A set is a value, as in mathematics:
`Q = G.V` makes `Q` another set with the same things in it, and taking from `Q` leaves
`G.V` as it was; `S = S ∪ {x}` and `S = S − {x}` change `S` where it is, and a procedure
that is handed a collection works on that one. What is in a collection keeps the order
in which it came in, which is the order `for each` goes through it and `print` writes it.
`EXTRACT-MIN` takes the least number, or the object with the least `key` (with the least
`d`, if no object has a `key`): it looks through them all each time, so `DECREASE-KEY`
has nothing to do. A name that one procedure reads before putting anything in it, and
another fills in, is shared by them: the `time` of the depth-first search. So the graph
algorithms run as the book writes them, with `G.V` and `G.Adj[u]`, and so do B-trees,
with `x.key[i]` and `x.c[i]`: see `examples/cormen/graphs.clrs` and
`examples/cormen/btree.clrs`. The data panel shows a collection as mathematics writes
it, `{s, t}`, `{(s, t): 10}`, naming an object by its `name` or its `key`; so does
`print`.

A number an array has beside it (`A.heap-size`, `S.top`) is kept in a name of its own,
listed as `A_heap_size`, `S_top`, which goes wherever the array goes: so the array stays
an array, drawn as one, and the stacks, queues and heaps of the book run as written: see
`examples/cormen/heap.clrs`.

Some lines of the book are in words. Those listed in the table are read, because what
they mean is one thing only: sorting a collection (by what each thing is, by an
attribute of the objects, or `by weight w`, where `w` keeps a number for each), making
the list of the edges and going through it in order, putting something in a collection
or taking it out, asking whether a call made a value smaller. `examples/cormen/kruskal.clrs`
has Kruskal's algorithm in the words of both editions. Any other line in words is not
run: a warning says so, and where the program gets there it is listed as something left
to do.

Objects and collections are records of the base language, taken from a store, and what
is done with them are procedures written in it and added to the program: that is how
they run here and in all seven languages, and why a translation has those procedures in
it and not the lists or the sets of its own language. The store has room for 256
objects, 512 collections and 2048 elements in all (those taken out are used again); a
run that asks for more stops saying so.

In LaTeX, the `codebox` environment of the `clrscode` packages, with which the book is
typeset, is read too: `\Procname`, `\li`, `\For`, `\Do`, `\End`, `\attrib` and the
others.

It is shown, and quoted in errors, as it was written; it runs in a rewriting, so a
message may name a word as another pseudocode would (`'endwhile'`), and a procedure is
listed with `_` where its name has `-`. A procedure that asks for `A.length` is listed
with one more thing it takes, `A_length`. Not read, and refused saying so: the other signs of mathematics; a number beside an array
that is handed on as something worked out and not by its name; a name shared by
procedures that is an array; sorting an array in words; sorting by two different things
in one program. Reals are single precision, as everywhere here: a real that is not
exactly representable (0.1) is printed with more or fewer digits by the different
languages.
