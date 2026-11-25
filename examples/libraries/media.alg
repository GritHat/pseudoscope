// VisuAlg: os dados vêm de um arquivo em vez do teclado
algoritmo "media"
arquivo "dados.txt"
var a, b: inteiro
    m: real
inicio
    leia(a)
    leia(b)
    m <- (a + b) / 2
    escreval("media ", m, " ", quad(a), " ", raizq(64), " ", compr("teste"))
    escreval(maiusc("ok"), " ", copia("algoritmo", 1, 4), " ", pos("go", "algoritmo"))
fimalgoritmo
