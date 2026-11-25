// Tipos declarados e inferidos, ciclos, segun, funciones
Funcion r <- Factorial(n)
    Si n <= 1 Entonces
        r <- 1
    SiNo
        r <- n * Factorial(n-1)
    FinSi
FinFuncion

SubProceso Intercambiar(a Por Referencia, b Por Referencia)
    aux <- a
    a <- b
    b <- aux
FinSubProceso

Funcion Ordenar(v, n)
    Definir i, j Como Entero
    Para i <- 0 Hasta n-2 Con Paso 1 Hacer
        Para j <- 0 Hasta n-2-i Hacer
            Si v[j] > v[j+1] Entonces
                Intercambiar(v[j], v[j+1])
            Fin Si
        FinPara
    FinPara
FinFuncion

Algoritmo Basico
    Definir nombre Como Caracter
    Definir edad, año Como Entero;
    Dimension datos[5]
    Definir datos Como Entero
    Escribir "Hola, " Sin Saltar
    Leer nombre, edad
    Escribir nombre, " tiene ", edad, " años"
    año <- 2026 - edad
    Escribir "nacio en ", AÑO
    Para k<-0 Hasta 4 Hacer
        datos[k] <- (k * 3) MOD 5
    FinPara
    Ordenar(datos, 5)
    Para k<-0 Hasta 4
        Escribir Sin Saltar datos[k], " "
    FinPara
    Escribir ""
    total <- 0
    i <- 10
    Mientras i > 0 Hacer
        total <- total + i
        i <- i - 3
    FinMientras
    Escribir "total ", total, " media ", total / 4
    Repetir
        i <- i + 1
    Hasta Que i = 3 O i > 100
    Segun i Hacer
        1, 2: Escribir "uno o dos"
        3:
            Escribir "tres"
        De Otro Modo:
            Escribir "otro"
    FinSegun
    Escribir Factorial(5), " ", 2 ^ 3, " ", RC(16), " ", TRUNC(3.7), " ", 7 % 4
    Escribir i = 3 Y NO (i < 2)
    Escribir Mayusculas(nombre), " ", Longitud(nombre), " ", Subcadena(nombre, 0, 1)
    Si nombre = "Ana" Entonces Escribir "es Ana" FinSi
FinAlgoritmo
