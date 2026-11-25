algoritmo "basico"
var
   v: vetor[1..5] de inteiro
   i, soma: inteiro
   media: real
   nome: caractere
   achou: logico

funcao maior(a: inteiro; b: inteiro): inteiro
inicio
   se a > b entao
      retorne a
   senao
      retorne b
   fimse
fimfuncao

procedimento troca(var a: inteiro; var b: inteiro)
var
   aux: inteiro
inicio
   aux <- a
   a <- b
   b <- aux
fimprocedimento

inicio
   leia(nome)
   escreval("Olá, ", nome)
   para i de 1 ate 5 faca
      v[i] <- i * i
      soma <- soma + v[i]
   fimpara
   media <- soma / 5
   escreval("soma ", soma, " media ", media:6:2)
   troca(v[1], v[5])
   escreval(v[1], " ", v[5], " ", maior(v[2], v[3]))
   i <- 10
   enquanto i > 3 faca
      i <- i - 4
   fimenquanto
   repita
      i <- i + 1
   ate i >= 5
   achou <- (i = 5) e nao (i > 6)
   escreval(achou, " ", 17 div 5, " ", 17 mod 5, " ", 17 \ 5)
   escolha i
   caso 1, 2
      escreval("pouco")
   caso 5
      escreval("cinco")
   outrocaso
      escreval("muito")
   fimescolha
   escreva("fim")
fimalgoritmo
