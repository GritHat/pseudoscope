# Command line tests, sourced by run.sh.
cd "$tmp" || exit 1
printf 'procedure main()\nbegin\nprint("hi")\nend main\n' > hi.plike
printf 'procedure main()\nbegin\nx := 1\nend main\n' > bad.plike
printf 'procedure main()\nbegin\nfoo()\nend main\n' > warn.plike
printf 'procedure p()\nbegin\nend p\n' > nomain.plike

cli() { # description, wanted status, command...
    what=$1 want=$2; shift 2
    "$@" > "$tmp/out" 2> "$tmp/err" < /dev/null
    got=$?
    if [ "$got" = "$want" ]; then pass; else
        echo "exit status $got, expected $want" >> "$tmp/err"; fail "cli: $what" "$tmp/err"; fi
}
has() { # description, file, text
    if grep -qF -- "$3" "$2"; then pass; else fail "cli: $1" "$2"; fi
}

cli "help" 0 "$pseudoscope" --help;                 has "help text" out "Usage: pseudoscope"
cli "version" 0 "$pseudoscope" --version;           has "version text" out "pseudoscope 5."
cli "no input" 2 "$pseudoscope";                    has "no input text" err "no input file"
cli "unknown option" 2 "$pseudoscope" --nope hi.plike
cli "bad value" 2 "$pseudoscope" --indexing=two hi.plike; has "bad value text" err "use zero or one"
cli "too many files" 2 "$pseudoscope" a b c
cli "missing file" 3 "$pseudoscope" missing.plike;  has "missing file text" err "cannot open"
cli "a directory" 3 "$pseudoscope" .
cli "to stdout" 0 "$pseudoscope" hi.plike;          has "stdout text" out "int main(void)"
cli "old style output" 0 "$pseudoscope" hi.plike old.c; has "old style file" old.c "int main(void)"
cli "output option" 0 "$pseudoscope" translate hi.plike -o new.c; has "output file" new.c "int main(void)"
cli "unwritable output" 3 "$pseudoscope" hi.plike -o no/such/dir.c
cli "never overwrite the input" 2 "$pseudoscope" hi.plike -o ./hi.plike; has "input kept" hi.plike "procedure main"
cli "check" 0 "$pseudoscope" check hi.plike
cli "check errors" 1 "$pseudoscope" check bad.plike; has "error text" err "bad.plike:3:1: error: 'x' is not declared"
cli "errors leave no output" 1 "$pseudoscope" bad.plike -o none.c
[ ! -e none.c ] && pass || fail "cli: output written despite errors"
cli "warning" 0 "$pseudoscope" check warn.plike;    has "warning text" err "warning:"
cli "quiet" 0 "$pseudoscope" check -q warn.plike;   [ ! -s err ] && pass || fail "cli: quiet" err
cli "werror" 1 "$pseudoscope" check -W warn.plike
cli "color" 1 "$pseudoscope" check --color=always bad.plike; has "color text" err "$(printf '\033[1;31m')"
cli "build" 0 "$pseudoscope" build hi.plike;        [ "$(./hi)" = hi ] && pass || fail "cli: built program"
cli "build to" 0 "$pseudoscope" build hi.plike -o there; [ "$(./there)" = hi ] && pass || fail "cli: built program 2"
cli "build without main" 1 "$pseudoscope" build nomain.plike; has "no main text" err "procedure 'main'"
cli "run" 0 "$pseudoscope" run hi.plike;            has "run text" out "hi"
cli "missing compiler" 3 "$pseudoscope" run --cc=/no/such/cc hi.plike
cli "tokens" 0 "$pseudoscope" tokens hi.plike;      has "tokens text" out "string	hi"
cli "ast" 0 "$pseudoscope" ast hi.plike;            has "ast text" out "procedure main"
cli "italian" 1 "$pseudoscope" check --lang=it bad.plike; has "italian text" err "errore: 'x' non è stato dichiarato"
cli "spanish" 1 "$pseudoscope" check --lang=es bad.plike; has "spanish text" err "error: 'x' no está declarado"
cli "language of the system" 1 env PSEUDOSCOPE_LANG= LC_ALL=es_ES.UTF-8 "$pseudoscope" check bad.plike; has "spanish from the system" err "no está declarado"
cli "language asked for" 1 env PSEUDOSCOPE_LANG=it LC_ALL=es_ES.UTF-8 "$pseudoscope" check bad.plike; has "italian from the variable" err "non è stato dichiarato"
cli "a language nobody translated" 1 env PSEUDOSCOPE_LANG= LC_ALL=fr_FR.UTF-8 "$pseudoscope" check bad.plike; has "english otherwise" err "error: 'x' is not declared"
printf '\\begin{algorithmic}\n\\State $x \\gets 1$\n\\While{$x < 3$}\n\\State $x \\gets x + $\n\\EndWhile\n\\end{algorithmic}\n' > bad.tex
cli "an algorithm in LaTeX" 1 "$pseudoscope" check bad.tex; has "its line as it was written" err "EndWhile"
printf 'BFS(G, s)\n    for each vertex v in G.Adj[s]\n        print v\n' > graph.clrs
cli "what is not read of Cormen" 1 "$pseudoscope" check graph.clrs; has "said, with the line as written" err "for each vertex v in G.Adj"
cli "Cormen typeset with codebox" 0 "$pseudoscope" check "$here/dialects/codebox.tex"; has "both procedures" err "2 programs and diagrams checked"
cli "portuguese" 1 "$pseudoscope" check --lang=pt_BR.UTF-8 bad.plike; has "portuguese text" err "erro: 'x' não está declarado"
cli "unknown language" 2 "$pseudoscope" check --lang=tlh bad.plike; has "the languages there are" err "(I speak en es it pt)"
cli "help in spanish" 0 "$pseudoscope" --lang=es --help; has "spanish help" out "Uso: pseudoscope"
cli "exec" 0 "$pseudoscope" exec hi.plike;          has "exec text" out "hi"
cli "exec steps" 1 "$pseudoscope" exec --steps=2 hi.plike; has "steps text" err "too many steps"
cli "bad steps" 2 "$pseudoscope" exec --steps=x hi.plike
cli "ui needs a terminal" 2 "$pseudoscope" ui hi.plike
cli "ui missing file" 3 "$pseudoscope" ui missing.plike
cli "ui files" 0 "$pseudoscope" ui . --keys= ;      has "ui files text" out "hi.plike"
cli "ui open file" 0 "$pseudoscope" ui . --keys="<down><down><down><up><home><enter>"
cli "dot" 0 "$pseudoscope" ast --dot hi.plike;      has "dot text" out "digraph"
"$pseudoscope" run - < hi.plike > out 2> err && [ "$(cat out)" = hi ] && pass || fail "cli: standard input" err
[ -z "$(find "${TMPDIR:-/tmp}" -maxdepth 1 -name 'pseudoscope-*' -newer hi.plike 2>/dev/null)" ] && pass || fail "cli: temporary files left"
