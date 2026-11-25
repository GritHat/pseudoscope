"""The words of the video in Italian: what story.py says in English, and how it is said here.
make.py --lang=it uses it; a text that is missing stays in English."""

WORDS = {
    'Pseudocode and designs, brought to life':
        'Pseudocodice e progetti che prendono vita',
    'The first algorithm':
        'Il primo algoritmo',
    '1 · first year':
        '1 · primo anno',
    'Sorting an array, and understanding why it works':
        'Ordinare un array, e capire perché funziona',
    'Recursion and data structures':
        'Ricorsione e strutture',
    '2 · first and second year':
        '2 · primo e secondo anno',
    'Who waits for whom, and where pointers lead':
        'Chi aspetta chi, e dove portano i puntatori',
    'Your own pseudocode':
        'Il tuo pseudocodice',
    '3 · each school has its own':
        '3 · ogni scuola ha il suo',
    'PSeInt, VisuAlg, Portugol, Cambridge, with their libraries':
        'PSeInt, VisuAlg, Portugol, Cambridge, con le loro librerie',
    'Classes and inheritance':
        'Classi ed ereditarietà',
    '4 · object-oriented programming':
        '4 · programmazione a oggetti',
    'Who extends whom, and which method runs':
        'Chi estende chi, e quale metodo parte',
    'Designing':
        'Progettare',
    '5 · software engineering':
        '5 · ingegneria del software',
    'A design can be walked through before it is written':
        'Un progetto si può percorrere prima di scriverlo',
    'Lecture notes and papers':
        'Appunti e articoli',
    '6 · Markdown and LaTeX':
        '6 · Markdown e LaTeX',
    'What is already written opens as it is':
        "Ciò che è già scritto si apre così com'è",
    'A whole system':
        'Un sistema intero',
    "From the user's click to the database, and back":
        "Dal clic dell'utente al database, e ritorno",
    'When things go wrong':
        'Quando le cose vanno storte',
    '8 · networks':
        '8 · reti',
    'A reliable protocol over a channel that loses packets':
        'Un protocollo affidabile su un canale che perde pacchetti',
    'Advanced algorithms':
        'Algoritmi avanzati',
    '9 · later years':
        '9 · anni successivi',
    'Checking a property, not only a result':
        'Verificare una proprietà, non solo un risultato',
    'Distributed systems':
        'Sistemi distribuiti',
    "10 · master's":
        '10 · magistrale',
    'Electing a leader, as Raft does':
        'Eleggere un leader, come fa Raft',
    'What if it were wrong?':
        'E se fosse sbagliato?',
    'an exercise':
        'un esercizio',
    'The same election, with a rule forgotten':
        'La stessa elezione, con una regola dimenticata',
    'Seven languages':
        'Sette linguaggi',
    '11 · from the design to the code':
        '11 · dal progetto al codice',
    'The same tests, translated and compiled':
        'Le stesse prove, tradotte e compilate',
    'From the first loop to distributed systems, in any terminal':
        'Dal primo ciclo ai sistemi distribuiti, in qualunque terminale',
    'Getting started':
        'Per cominciare',
    'open the folder and start':
        'apri la cartella e comincia',
    'run the tests':
        'lancia le prove',
    'translated, in seven languages':
        'tradotto, in sette linguaggi',
    'all the keys':
        'tutti i tasti',
    'It starts from the files, with a preview of the chosen one.':
        "Si parte dai file: a destra c'è l'anteprima di quello scelto.",
    'The map: each subprogram is a box, with what goes in and what comes out.':
        'La mappa del programma: ogni sottoprogramma è una scatola, con ciò che entra e ciò che esce.',
    'Enter goes into detail: first the code…':
        'Invio scende nel dettaglio: prima il codice…',
    '…then its translation to C, line by line.':
        '…poi la sua traduzione in C, riga per riga.',
    'T shows the same in C++, Java, Rust, TypeScript, JavaScript and Python.':
        'Con T la stessa cosa in C++, Java, Rust, TypeScript, JavaScript e Python.',
    'r tries this subprogram by itself, with values typed or generated.':
        'Con r si prova questo sottoprogramma da solo: i valori si scrivono, o si generano.',
    'Each → is a statement: the code on the left, the data changing on the right.':
        "Ogni → è un'istruzione. A sinistra il codice, a destra i dati mentre cambiano.",
    'At the bottom, in words, what has just happened.':
        'In basso, a parole, cosa è appena successo.',
    'Not clear? ← goes back, as far as you want.':
        'Non è chiaro? Con ← si torna indietro, quanto si vuole.',
    'v turns the array into bars: the large values rise to the end.':
        "Con v l'array diventa un grafico: si vedono i valori grandi salire in fondo.",
    'And f turns the code into its activity diagram, showing where we are.':
        'E con f il codice diventa il suo diagramma di attività, con il punto in cui siamo.',
    'Five levels: each decides what you see, and how long a step is.':
        "Cinque livelli, dall'esterno al dettaglio: il livello decide cosa si vede e quanto è lungo un passo.",
    'Statement: an expression becomes its value, one piece at a time.':
        "Istruzione: un'espressione diventa il suo valore, un pezzo alla volta.",
    'Memory: the C code, addresses, bytes. For those who want to go all the way.':
        'Memoria: il C, gli indirizzi, i byte. Per chi vuole arrivare fino in fondo.',
    'Esc comes back up: choose the abstraction at any time.':
        "Esc risale: l'astrazione si sceglie in ogni momento.",
    'Program level: a step is a call or a return.':
        'Livello Programma: un passo è una chiamata o un ritorno.',
    'On the right, the stack: each call waits for the one above it.':
        'A destra la pila: ogni chiamata aspetta che finisca quella sopra di lei.',
    'On the left, the tree: fib(2) is computed twice. That is why it is slow.':
        "A sinistra l'albero: fib(2) viene calcolato due volte. Ecco perché è lenta.",
    'A binary search tree built by the program: pointers become a drawing.':
        'Un albero binario di ricerca costruito dal programma: i puntatori diventano un disegno.',
    'A program written for PSeInt, in Spanish. Nothing to convert.':
        'Un programma scritto per PSeInt, in spagnolo. Non va convertito.',
    'It runs, is explored and is translated like any other.':
        'Si esegue, si esplora e si traduce come tutti gli altri.',
    'Each has its libraries. Cambridge pseudocode writes a file, then reads it back.':
        'Ognuno ha le sue librerie. Lo pseudocodice di Cambridge scrive un file, poi lo rilegge riga per riga.',
    'Portugol Studio has a graphics library: d shows the drawing grow, statement by statement.':
        "Portugol Studio ha una libreria grafica: con d si guarda il disegno crescere, un'istruzione alla volta.",
    'An abstract class, an interface, and the classes that extend them.':
        "Una classe astratta, un'interfaccia, e le classi che le estendono.",
    'C has no abstract classes. Rather than translate wrongly, it says so.':
        'Il C non ha classi astratte. Invece di tradurre male, lo dice.',
    'Each language in its own way: in C++ a class extends as many as it likes…':
        'Ogni linguaggio a modo suo: in C++ una classe ne estende quante vuole…',
    '…in Java only one, plus interfaces…':
        '…in Java una sola, più le interfacce…',
    '…and in Rust none: there are only traits, and here they are not enough.':
        '…e in Rust nessuna: ci sono solo i trait, e qui non bastano.',
    'Then TypeScript, JavaScript and Python, with its multiple classes.':
        'Poi TypeScript, JavaScript e Python, con le sue classi multiple.',
    "At run time the object chooses: twice() is Shape's, but area() is Rectangle's.":
        "Quando gira, il metodo lo sceglie l'oggetto: twice() è di Shape, ma area() è quella di Rectangle.",
    'A PlantUML sequence diagram, walked through one message at a time.':
        'Un diagramma di sequenza in PlantUML, percorso un messaggio alla volta.',
    'Tab takes the other branch: what if the password is wrong?':
        "Tab prende l'altro ramo: e se la password fosse sbagliata?",
    'A state diagram: choose an arrow, and the state changes live.':
        'Un diagramma di stato: si sceglie la freccia, e lo stato cambia dal vivo.',
    'The way so far stays on the right. Classes, activities and flowcharts work too.':
        'A destra resta il cammino fatto. Valgono anche classi, attività e diagrammi di flusso.',
    'Or pseudo-classes still empty: where there is “...” there is no code yet.':
        "Oppure pseudoclassi ancora vuote: dove c'è «...» il codice non esiste.",
    'The design runs all the same, and shows who calls whom.':
        'Il progetto gira lo stesso, e mostra chi chiama chi.',
    'A plan written in PlantUML is a Gantt chart to walk through time: what is done, what is under way.':
        'Un piano scritto in PlantUML è un Gantt da percorrere nel tempo: cosa è finito, cosa è in corso.',
    'Entities and relationships, in Mermaid: each link reads as a sentence.':
        'Entità e relazioni, in Mermaid: ogni legame si legge come una frase.',
    'And mind maps, WBS, timelines, Graphviz graphs: one branch at a time.':
        'E mappe mentali, WBS, linee del tempo, grafi di Graphviz: un ramo alla volta.',
    'Markdown notes open like a folder: inside, their programs and their diagrams.':
        'Gli appunti in Markdown si aprono come una cartella: dentro, i loro programmi e i loro diagrammi.',
    'The pseudocode of the lesson runs where it is, with no copying elsewhere.':
        "Lo pseudocodice della lezione si esegue lì dov'è, senza copiarlo altrove.",
    'A LaTeX paper: the algorithmic environment becomes a program, pgfgantt a plan.':
        "Un articolo in LaTeX: l'ambiente algorithmic diventa un programma, pgfgantt un piano.",
    "Euclid's algorithm, as it is written in the paper, executed: Gcd(48, 18) is 6.":
        "L'algoritmo di Euclide, così com'è nell'articolo, eseguito: Gcd(48, 18) fa 6.",
    'Three components: each lives on its own and reacts to messages.':
        'Tre componenti: ognuno vive per conto suo e reagisce ai messaggi.',
    't runs all the tests. Enter shows one while it runs.':
        'Con t si lanciano tutte le prove. Invio ne mostra una mentre gira.',
    'System level: the sequence diagram draws itself, one message per step.':
        'Livello Sistema: il diagramma di sequenza si disegna da solo, un messaggio per passo.',
    'Enter goes into what the server is doing right now, down to a statement…':
        'Invio entra in ciò che il server sta facendo adesso, fino alla singola istruzione…',
    '…and Esc goes back outside, without losing the thread.':
        "…e Esc riporta all'esterno, senza perdere il filo.",
    'The same things from other points of view: Tab changes diagram.':
        'Le stesse cose, da altri punti di vista: Tab cambia diagramma.',
    'And the checks: what had to happen, end to end, and what did.':
        "E le verifiche: cosa doveva succedere, da un capo all'altro, e cosa è successo.",
    'Stop-and-wait: the channel loses one message in four.':
        'Stop-and-wait: il canale perde un messaggio su quattro.',
    'There: the acknowledgement got lost.':
        'Ecco: la conferma si è persa.',
    'The timer expires, and the sender sends the same packet again.':
        'Scade il timer, e il mittente manda di nuovo lo stesso pacchetto.',
    'Components: the data of each, and the messages as they travel.':
        'Componenti: i dati di ciascuno, e i messaggi mentre viaggiano.',
    'States: where the sender is, how it got there, where it can go.':
        "Stati: dov'è il mittente, come ci è arrivato, dove può andare.",
    'Dijkstra. One test checks distances, the other a property that must always hold.':
        "Dijkstra. Una prova verifica le distanze, l'altra una proprietà che deve valere sempre.",
    'If something is wrong, watch the test run and go down to the cause.':
        'Se qualcosa non torna, si guarda la prova girare e si scende fino alla causa.',
    'Five nodes. Whoever hears no leader for a while asks for votes.':
        "Cinque nodi. Chi non sente un leader per un po' si candida e chiede i voti.",
    'Two candidates at the same moment!':
        'Due candidati nello stesso istante!',
    'But each node votes once per term: only one gets a majority.':
        'Ma ogni nodo vota una volta sola per mandato: solo uno arriva alla maggioranza.',
    'The state chart of each node, live.':
        'Il diagramma degli stati di ogni nodo, dal vivo.',
    'The timing diagram: the leader crashes, and the others elect a new one.':
        'Il diagramma dei tempi: il leader cade, e gli altri ne eleggono uno nuovo.',
    'The tests fail: there are two leaders in the same term.':
        'Le prove falliscono: in un mandato ci sono due leader.',
    'Let us see where it happens, on the timing diagram…':
        'Si guarda dove succede, sul diagramma dei tempi…',
    '…and then in the sequence: the same nodes vote for both.':
        '…e poi nella sequenza: gli stessi nodi votano per tutti e due.',
    'The check on past votes is missing: the rule that makes it safe.':
        "Manca il controllo su chi si è già votato: è la regola che rende sicuro l'algoritmo.",
    'The tests of the election, run here: no other tool is needed.':
        "Le prove dell'elezione, eseguite qui: non serve nessun altro strumento.",
    'The same, translated to Rust and compiled. The simulation is translated too: same result.':
        "Le stesse, tradotte in Rust e compilate. Anche la simulazione è tradotta: l'esito è identico.",
    'The mistakes are the same too: in Java the exercise fails at the same point.':
        "Anche gli errori sono gli stessi: in Java l'esercizio fallisce nello stesso punto.",
    'And the code is yours to take away: C, C++, Java, Rust, TypeScript, JavaScript, Python.':
        'E il codice si porta via: C, C++, Java, Rust, TypeScript, JavaScript, Python.',
    'Pseudocode that runs: each → is a statement, and ← goes back.':
        "Pseudocodice che gira: ogni → è un'istruzione, e con ← si torna indietro.",
    'The data while they change: here the array, seen as bars.':
        "I dati mentre cambiano: qui l'array, visto come grafico.",
    'The translation line by line: C, C++, Java, Rust, TypeScript, JavaScript, Python.':
        'La traduzione riga per riga: C, C++, Java, Rust, TypeScript, JavaScript, Python.',
    'A distributed system: the sequence diagram draws itself.':
        'Un sistema distribuito: il diagramma di sequenza si disegna da solo.',
    'Diagrams to walk through: UML, Gantt, entities and relationships, mind maps.':
        'Diagrammi da percorrere: UML, Gantt, entità e relazioni, mappe mentali.',
    'The pseudocodes of schools, with their libraries: here Portugol draws.':
        'Gli pseudocodici delle scuole, con le loro librerie: qui Portugol disegna.',
    'And the tests, translated and compiled: the same result in every language.':
        'E le prove, tradotte e compilate: stesso esito in ogni linguaggio.',
    '1 · The first algorithm':
        '1 · Il primo algoritmo',
    '2 · Recursion':
        '2 · Ricorsione',
    '2 · Data structures':
        '2 · Strutture dati',
    '3 · Other pseudocodes':
        '3 · Altri pseudocodici',
    '3 · Their libraries':
        '3 · Le loro librerie',
    '4 · Inheritance':
        '4 · Ereditarietà',
    '5 · Pseudo-classes':
        '5 · Pseudoclassi',
    '5 · Plans, data, ideas':
        '5 · Piani, dati, idee',
    '8 · Protocols':
        '8 · Protocolli',
    '9 · Graphs':
        '9 · Grafi',
    '10 · Consensus':
        '10 · Consenso',
    '10 · Finding the mistake':
        "10 · Trovare l'errore",
    '11 · Seven languages':
        '11 · Sette linguaggi',
    'Step by step':
        'Passo per passo',
    'Systems':
        'Sistemi',
    'Diagrams':
        'Diagrammi',
    'Other pseudocodes':
        'Altri pseudocodici',
    'Translated tests':
        'Prove tradotte',
}
