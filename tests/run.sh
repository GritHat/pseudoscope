#!/bin/sh
# End-to-end tests.
#   programs/  translated, compiled, run; output compared with NAME.out
#              (NAME.in: input, NAME.opts: options, NAME.status: exit status)
#   accept/    syntax that must be accepted, and give C that compiles
#   errors/    programs to refuse; messages compared with NAME.err
# UPDATE=1 rewrites the expected files.
here=$(cd "$(dirname "$0")" && pwd)
plike=$(cd "$(dirname "${PLIKE:-bin/plike}")" && pwd)/$(basename "${PLIKE:-bin/plike}")
tmp=$(mktemp -d) || exit 1
trap 'rm -rf "$tmp"' EXIT
passed=0 failed=0

pass() { passed=$((passed + 1)); }
fail() { failed=$((failed + 1)); echo "FAIL $1"; [ -s "${2:-}" ] && sed 's/^/    /' "$2" | head -20; }
compare() { # name expected actual
    [ -n "${UPDATE:-}" ] && cp "$3" "$2"
    if diff -u "$2" "$3" > "$tmp/diff" 2>&1; then pass; else fail "$1" "$tmp/diff"; fi
}

cd "$here/programs" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    input=/dev/null; [ -f "$name.in" ] && input=$name.in
    want=0; [ -f "$name.status" ] && want=$(cat "$name.status")
    # shellcheck disable=SC2086
    "$plike" run --cc="$here/ccheck" $opts "$file" < "$input" > "$tmp/out" 2> "$tmp/err"
    got=$?
    if [ "$got" != "$want" ]; then
        echo "exit status $got, expected $want" >> "$tmp/err"
        fail "programs/$name" "$tmp/err"
    else
        compare "programs/$name" "$name.out" "$tmp/out"
    fi
done

cd "$here/accept" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    for extra in "" --bounds-check; do
        # shellcheck disable=SC2086
        if "$plike" -q $opts $extra "$file" -o "$tmp/a.c" 2> "$tmp/err" &&
           "$here/ccheck" -std=c11 -Wno-uninitialized -Wno-maybe-uninitialized -c "$tmp/a.c" -o "$tmp/a.o" 2> "$tmp/err"
        then pass; else fail "accept/$name $extra" "$tmp/err"; fi
    done
done

cd "$here/errors" || exit 1
for file in *.plike; do
    name=${file%.plike}
    opts= ; [ -f "$name.opts" ] && opts=$(cat "$name.opts")
    # shellcheck disable=SC2086
    "$plike" check $opts "$file" > /dev/null 2> "$tmp/err"
    got=$?
    if [ "$got" != 1 ]; then
        echo "exit status $got, expected 1" >> "$tmp/err"
        fail "errors/$name" "$tmp/err"
    else
        compare "errors/$name" "$name.err" "$tmp/err"
    fi
done

. "$here/cli.sh"

echo "end to end: $passed passed, $failed failed"
[ "$failed" = 0 ]
