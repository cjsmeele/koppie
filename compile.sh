#!/bin/bash

# Copyright (c) 2017, Chris Smeele.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Usage: compile.sh [-o OUTFILE] [INFILE]
# If INFILE is not provided, stdin is used.
# If OUTFILE is not provided, a.out is used.

set -e

KOPPIE=${KOPPIE:-"koppie"}
LLC=${LLC:-"llc"}
CC=${CC:-"gcc"}

while getopts "o:" optname; do
    case "$optname" in
        "o")
        OUTFILE="$OPTARG";;
    esac
done

shift $((OPTIND-1))

if [[ $# -eq 0 ]]; then
    OUTFILE=${OUTFILE:-"a.out"}
    BASE=$(basename -s .out "$OUTFILE")
    "$KOPPIE" "$BASE" >"${BASE}.ir"
else
    OUTFILE=${OUTFILE:-$(basename -s .b "$1")}
    BASE=$(basename -s .out "$OUTFILE")
    "$KOPPIE" "$1" < "$1" >"${BASE}.ir"
fi

"$LLC" -relocation-model=dynamic-no-pic -data-sections -function-sections "${BASE}.ir" -o "${BASE}.S"
"$CC" -s -static -g0 -fno-exceptions -fno-rtti -Wl,--build-id=none "${BASE}".S -o "${OUTFILE}"
#"$CC" "${BASE}".S -o "${OUTFILE}"
