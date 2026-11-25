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

cli "help" 0 "$plike" --help;                 has "help text" out "Usage: plike"
cli "version" 0 "$plike" --version;           has "version text" out "plike 2."
cli "no input" 2 "$plike";                    has "no input text" err "no input file"
cli "unknown option" 2 "$plike" --nope hi.plike
cli "bad value" 2 "$plike" --indexing=two hi.plike; has "bad value text" err "use zero or one"
cli "too many files" 2 "$plike" a b c
cli "missing file" 3 "$plike" missing.plike;  has "missing file text" err "cannot open"
cli "a directory" 3 "$plike" .
cli "to stdout" 0 "$plike" hi.plike;          has "stdout text" out "int main(void)"
cli "old style output" 0 "$plike" hi.plike old.c; has "old style file" old.c "int main(void)"
cli "output option" 0 "$plike" translate hi.plike -o new.c; has "output file" new.c "int main(void)"
cli "unwritable output" 3 "$plike" hi.plike -o no/such/dir.c
cli "never overwrite the input" 2 "$plike" hi.plike -o ./hi.plike; has "input kept" hi.plike "procedure main"
cli "check" 0 "$plike" check hi.plike
cli "check errors" 1 "$plike" check bad.plike; has "error text" err "bad.plike:3:1: error: 'x' is not declared"
cli "errors leave no output" 1 "$plike" bad.plike -o none.c
[ ! -e none.c ] && pass || fail "cli: output written despite errors"
cli "warning" 0 "$plike" check warn.plike;    has "warning text" err "warning:"
cli "quiet" 0 "$plike" check -q warn.plike;   [ ! -s err ] && pass || fail "cli: quiet" err
cli "werror" 1 "$plike" check -W warn.plike
cli "color" 1 "$plike" check --color=always bad.plike; has "color text" err "$(printf '\033[1;31m')"
cli "build" 0 "$plike" build hi.plike;        [ "$(./hi)" = hi ] && pass || fail "cli: built program"
cli "build to" 0 "$plike" build hi.plike -o there; [ "$(./there)" = hi ] && pass || fail "cli: built program 2"
cli "build without main" 1 "$plike" build nomain.plike; has "no main text" err "procedure 'main'"
cli "run" 0 "$plike" run hi.plike;            has "run text" out "hi"
cli "missing compiler" 3 "$plike" run --cc=/no/such/cc hi.plike
cli "tokens" 0 "$plike" tokens hi.plike;      has "tokens text" out "string	hi"
cli "ast" 0 "$plike" ast hi.plike;            has "ast text" out "procedure main"
cli "dot" 0 "$plike" ast --dot hi.plike;      has "dot text" out "digraph"
"$plike" run - < hi.plike > out 2> err && [ "$(cat out)" = hi ] && pass || fail "cli: standard input" err
[ -z "$(find "${TMPDIR:-/tmp}" -maxdepth 1 -name 'plike-*' -newer hi.plike 2>/dev/null)" ] && pass || fail "cli: temporary files left"
