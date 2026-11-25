# Other pseudocodes

The extension of the file says which, or `--dialect`; without one, the first words do
(`Algoritmo`, `programa {`, `DECLARE`...):

| | Extension | `--dialect` | Language |
|---|---|---|---|
| P-like | `.plike` | `plike` | [language.md](language.md) |
| [PSeInt](https://pseint.sourceforge.net) | `.psc` | `pseint` | Spanish |
| [VisuAlg](https://visualg3.com.br) | `.alg` | `visualg` | Portuguese |
| Portugol Studio | `.por` | `portugol` | Portuguese |
| Cambridge International (IGCSE, A Level) | `.pse` | `cambridge` | English |

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
