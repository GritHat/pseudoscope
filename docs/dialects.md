# Other pseudocodes

The extension of the file says which, or `--dialect`:

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
reference, texts and their functions. What is not: files, screens and colours, graphics,
libraries. Longer programs, with what they print, are in
[tests/dialects](../tests/dialects).
