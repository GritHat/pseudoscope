// Portugol Studio: a drawing made with the Graficos library, and a file
// written with Arquivos. In the interface, d shows the drawing while it grows.
programa
{
    inclua biblioteca Graficos --> g
    inclua biblioteca Matematica --> mat
    inclua biblioteca Arquivos --> a
    inclua biblioteca Texto --> t

    funcao inicio()
    {
        inteiro i, x, arq
        cadeia linha

        g.iniciar_modo_grafico(verdadeiro)
        g.definir_dimensoes_janela(80, 40)
        g.definir_cor(g.COR_AZUL)
        g.limpar()
        g.desenhar_retangulo(2, 2, 76, 36, falso, falso)
        g.definir_cor(g.COR_AMARELO)
        g.desenhar_elipse(50, 6, 20, 12, verdadeiro)
        g.definir_cor(g.criar_cor(0, 160, 60))
        para (i = 0; i < 6; i++)
        {
            x = 8 + i * 10
            g.desenhar_linha(x, 34, x + 5, 34 - mat.potencia(i, 2))
        }
        g.definir_cor(g.COR_VERMELHO)
        g.desenhar_texto(6, 6, "Pseudoscope")
        g.renderizar()

        arq = a.abrir_arquivo("quadrados.txt", a.MODO_ESCRITA)
        para (i = 1; i <= 4; i++)
        {
            a.escrever_linha("quadrado " + i * i, arq)
        }
        a.fechar_arquivo(arq)

        arq = a.abrir_arquivo("quadrados.txt", a.MODO_LEITURA)
        enquanto (nao a.fim_arquivo(arq))
        {
            linha = a.ler_linha(arq)
            escreva(t.caixa_alta(linha), " ", t.numero_caracteres(linha), "\n")
        }
        a.fechar_arquivo(arq)
        escreva(mat.maior_numero(3, 7), " ", mat.raiz(81.0, 2.0), "\n")
    }
}
