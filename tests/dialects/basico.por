programa
{
    const inteiro TAM = 5
    inteiro total = 0

    funcao inicio()
    {
        inteiro v[TAM], i, x = 3
        cadeia nome
        leia(nome)
        escreva("Olá, ", nome, "\n")
        para (i = 0; i < TAM; i++) {
            v[i] = quadrado(i)
            total += v[i]
        }
        escreva("total ", total, "\n")
        dobra(x)
        escreva(x, " ", 7 / 2, " ", 7.0 / 2, " ", 7 % 2, "\n")
        se (x > 5 e nao (x == 7)) escreva("grande\n")
        senao se (x == 5) { escreva("cinco\n") }
        senao { escreva("pequeno\n") }
        enquanto (x > 0) { x = x - 2 }
        faca { x++ } enquanto (x < 3)
        escolha (x) {
            caso 1:
                escreva("um\n")
                pare
            caso 3:
                escreva("tres\n")
                pare
            caso contrario:
                escreva("?\n")
        }
        inteiro w[] = {4, 1, 3}
        escreva(soma(w, 3), " ", verdadeiro, "\n")
        para (inteiro k = 3; k > 0; k--) escreva(k, " ")
    }

    funcao inteiro quadrado(inteiro n) { retorne n * n }
    funcao vazio dobra(inteiro &n) { n = n * 2 }
    funcao inteiro soma(inteiro a[], inteiro n)
    {
        inteiro s = 0
        para (inteiro i = 0; i < n; i++) s += a[i]
        retorne s
    }
}
