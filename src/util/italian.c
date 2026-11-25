#include "plike/diag.h"

#include <string.h>

/* English and Italian forms of every message. The arguments come in the same
 * order in both; "%.0s" drops one that Italian does not need. */
const char *const ITALIAN_MESSAGES[][2] = {
    /* words put inside other messages */
    {"end of file", "la fine del file"},
    {"identifier", "un nome"},
    {"integer number", "un numero intero"},
    {"real number", "un numero reale"},
    {"string", "un testo"},
    {"character", "un carattere"},
    {"array dimension", "la dimensione di un array"},
    {"a declaration (function, procedure, var or type)", "una dichiarazione (function, procedure, var o type)"},
    {"a field name", "il nome di un campo"},
    {"')' after the parameters", "')' dopo i parametri"},
    {"a loop variable", "la variabile del ciclo"},
    {"an assignment", "un'assegnazione"},
    {"':' and a type", "':' e un tipo"},
    {"an expression", "un'espressione"},
    {"a parameter name", "il nome di un parametro"},
    {"a procedure name", "il nome della procedura"},
    {"a function name", "il nome della funzione"},
    {"a size: only the first dimension can be left out", "una dimensione: solo la prima si può omettere"},
    {"a statement", "un'istruzione"},
    {"a type", "un tipo"},
    {"a type name", "il nome di un tipo"},
    {"a variable name", "il nome di una variabile"},
    {"'end' after the record fields", "'end' dopo i campi del record"},
    {"'%s' or 'end %s'", "'%s' oppure 'end %s'"},

    {"the name of a state", "il nome di uno stato"},
    {"the name of a message", "il nome di un messaggio"},
    {"a text between quotes", "un testo tra virgolette"},
    {"'end %s'", "'end %s'"},
    {"'delay' or 'loss'", "'delay' o 'loss'"},
    {"a variable, a procedure or a function of the class", "una variabile, una procedura o una funzione della classe"},

    /* systems */
    {"'%s' is not a component", "'%s' non è un componente"},
    {"messages are sent to components, not to %s", "i messaggi si mandano ai componenti, non a %s"},
    {"a time must be an integer, not %s", "un tempo deve essere un integer, non %s"},
    {"'%s' does not handle the message '%s'", "'%s' non gestisce il messaggio '%s'"},
    {"no component handles the message '%s'", "nessun componente gestisce il messaggio '%s'"},
    {"%s is not a component or an object", "%s non è un componente né un oggetto"},
    {"'%s' is a component: it must be a global variable", "'%s' è un componente: deve essere una variabile globale"},
    {"'%s' has no state '%s'", "'%s' non ha lo stato '%s'"},
    {"a message carries simple values: '%s' cannot be one", "un messaggio porta valori semplici: '%s' non può esserlo"},
    {"the message '%s' has other parameters at line %d", "il messaggio '%s' ha altri parametri alla riga %d"},
    {"the message 'start' has no parameters", "il messaggio 'start' non ha parametri"},
    {"a link joins components, not %s", "un collegamento unisce componenti, non %s"},

    /* writing */
    {"unknown escape sequence", "dopo '\\' c'è un carattere non previsto"},
    {"unexpected byte 0x%02x", "carattere non previsto (0x%02x)"},
    {"unexpected character '%c'", "carattere non previsto: '%c'"},
    {"a character literal holds exactly one character", "tra apici singoli ci va un solo carattere"},
    {"invalid number '%.*s'", "'%.*s' non è un numero valido"},
    {"'%.*s' starts with 0, so it is octal, but has the digit %c",
     "'%.*s' comincia con 0, quindi è in base 8, ma contiene la cifra %c"},
    {"unterminated %s", "manca la chiusura del testo%.0s"},
    {"comment is never closed", "il commento non viene mai chiuso"},
    {"code is nested too deeply", "troppi livelli uno dentro l'altro"},
    {"'%s' compares; to assign use ':='%s", "'%s' confronta due valori; per assegnare usa ':='%.0s"},
    {"expression is too long", "l'espressione è troppo lunga"},
    {"missing array index", "manca l'indice dell'array"},
    {"expected %s, found '%s'", "qui serve %s, invece c'è '%s'"},
    {"expected %s, found %s", "qui serve %s, invece c'è %s"},
    {"read needs a variable", "a read serve una variabile"},

    /* arrays */
    {"a %dd array was given %d dimensions", "un array %dd ha ricevuto %d dimensioni"},
    {"an array has between 1 and 16 dimensions", "un array ha da 1 a 16 dimensioni"},
    {"'%s' has %d dimension%s here but its type has %d", "'%s' qui ha dimensioni: %d%.0s, ma il suo tipo ne ha %d"},
    {"the size of '%s' is missing", "manca la dimensione di '%s'"},
    {"the size of an array must be positive", "la dimensione di un array deve essere positiva"},
    {"the size of %s must be constant", "la dimensione di questo array deve essere costante%.0s"},
    {"an array index must be an integer, not %s", "l'indice di un array deve essere un integer, non %s"},
    {"an array bound must be an integer, not %s", "il limite di un array deve essere un integer, non %s"},
    {"a pointer takes one index, not %d", "un puntatore vuole un solo indice, non %d"},
    {"arrays are accessed with [] (see --mixed-arrays)", "agli array si accede con [] (vedi --mixed-arrays)"},
    {"%s is not an array", "%s non è un array"},
    {"the array has %d dimension%s but %d indices were given", "dimensioni dell'array: %d%.0s, indici scritti: %d"},
    {"only the first dimension of '%s' can be left out", "di '%s' si può omettere solo la prima dimensione"},
    {"the bounds of '%s' depend on itself", "i limiti di '%s' dipendono da se stesso"},

    /* names */
    {"'%s' is not declared", "'%s' non è stato dichiarato"},
    {"'%s' is not declared, using '%s'", "'%s' non è stato dichiarato: uso '%s'"},
    {"'%s' is already declared at line %d", "'%s' è già dichiarato alla riga %d"},
    {"type '%s' is already declared at line %d", "il tipo '%s' è già dichiarato alla riga %d"},
    {"type '%s' is not declared, using '%s'", "il tipo '%s' non è stato dichiarato: uso '%s'"},
    {"unknown type '%s'", "il tipo '%s' non esiste"},
    {"field '%s' is already declared", "il campo '%s' è già dichiarato"},
    {"there is no field '%s' in %s", "non c'è un campo '%s' in %s"},
    {"record '%s' contains itself; use a pointer", "il record '%s' contiene se stesso; usa un puntatore"},
    {"'%s' is not defined in this file", "'%s' non è definita in questo file"},

    /* subprograms */
    {"parameter '%s' of '%s' is %s, not %s", "il parametro '%s' di '%s' è %s, non %s"},
    {"parameter '%s' has no type", "il parametro '%s' non ha un tipo"},
    {"parameter '%s' already has a type, ignoring this one", "il parametro '%s' ha già un tipo: ignoro questo"},
    {"taking '%s' as the parameter '%s'", "considero '%s' come il parametro '%s'"},
    {"the type of '%s' belongs in the body (see --params)", "il tipo di '%s' va nel corpo (vedi --params)"},
    {"the type of '%s' belongs in the parameter list (see --params)",
     "il tipo di '%s' va nella lista dei parametri (vedi --params)"},
    {"'%s' is an %s parameter: it needs a variable", "'%s' è un parametro %s: serve una variabile"},
    {"'%s' takes numbers, not %s", "'%s' vuole numeri, non %s"},
    {"'%s' takes %d argument%s, not %d", "valori da passare a '%s': %d%.0s, non %d"},
    {"a procedure has no value", "una procedura non ha un valore"},
    {"a procedure cannot return a value", "una procedura non può restituire un valore"},
    {"a function cannot return an array; use an out parameter",
     "una funzione non può restituire un array; usa un parametro out"},
    {"function '%s' has no result type", "manca il tipo del risultato della funzione '%s'"},
    {"'%s' returns %s, not %s", "'%s' restituisce %s, non %s"},
    {"this is not a function", "questa non è una funzione"},

    /* values */
    {"cannot print %s", "non posso stampare %s"},
    {"read needs a variable of a basic type", "a read serve una variabile di un tipo semplice"},
    {"a loop bound must be a number, not %s", "i limiti di un ciclo devono essere numeri, non %s"},
    {"a loop variable must be a number", "la variabile di un ciclo deve essere un numero"},
    {"the step cannot be 0", "il passo non può essere 0"},
    {"only a variable has an address", "solo una variabile ha un indirizzo"},
    {"%s cannot be applied to %s", "%s non si può usare con %s"},
    {"%s cannot be applied to %s and %s", "%s non si può usare tra %s e %s"},
    {"%s cannot be %s", "qui serve una condizione%.0s, non %s"},
    {"%s is not a pointer", "%s non è un puntatore"},
    {"%s is not a record", "%s non è un record"},
    {"the number does not fit an integer", "il numero è troppo grande per un integer"},
    {"%s cannot be assigned to %s", "non posso mettere %s in %s"},
    {"this value is not used", "questo valore non viene usato"},
    {"'%s' is a function: it cannot be assigned", "'%s' è una funzione: non le si può assegnare un valore"},
    {"this cannot be assigned", "a questo non si può assegnare un valore"},
};

const int ITALIAN_MESSAGE_COUNT = sizeof ITALIAN_MESSAGES / sizeof ITALIAN_MESSAGES[0];

const char *diag_text(const Diag *diag, const char *english) {
    if (!diag->italian) return english;
    for (int i = 0; i < ITALIAN_MESSAGE_COUNT; i++)
        if (strcmp(ITALIAN_MESSAGES[i][0], english) == 0) return ITALIAN_MESSAGES[i][1];
    return english;
}
