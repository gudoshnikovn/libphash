#!/usr/bin/env bash
# Tracked text describes the library as it is, not how it got there. A reader of this
# repository has no issue tracker, no planning notes and no intermediate builds: a
# comment that cites one is a reference to nothing, and "since 2.0.0" in a header is
# out of date the day it ships. Comparisons with 1.x belong in CHANGELOG.md and
# MIGRATION.md only.
#
# Two passes over every tracked text file outside vendor/ and the binary fixtures:
#
#   strict  -- fails the run. References that are never legitimate in tracked text:
#              tracker ids (R12, T-123, "task 7"), paths into tasks/, the internal
#              REVIEW_v2*/CLAUDE.md files, and release-cycle wording ("since 2.0.0",
#              "development build", "earlier draft"). .gitignore is exempt: it names
#              the local files it keeps out of the repository.
#   advisory -- printed, never fails. Wording that usually tells a story ("used to",
#              "no longer", "previously", "again") but is also ordinary English --
#              "the previously loaded image", "tries again". Read each hit in context.
#
# git grep -P, not -E: git's ERE on macOS silently ignores \b and matches nothing.
#
# Usage: scripts/check_final_state_voice.sh    (from anywhere inside the repository)
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

EXCLUDE=(':!vendor' ':!tests/data' ':!tests/fuzz/corpus')
HISTORY_FILES=(':!CHANGELOG.md' ':!MIGRATION.md')

STRICT='\b(R[0-9]{2}|T-[0-9]{3})\b|\btasks/|\b[Tt]ask [0-9]+\b|REVIEW_v2|\bCLAUDE\.md\b|development build|earlier draft|(since|before|until|in) 2\.0\.0\b|earlier 2\.0\b'
ADVISORY='used to|no longer|previously|before this fix|was changed|\bagain\b|once more'

status=0

strict_hits=$(git grep -nIP "$STRICT" -- . "${EXCLUDE[@]}" "${HISTORY_FILES[@]}" \
    ':!.gitignore' ':!scripts/check_final_state_voice.sh' || true)
if [ -n "$strict_hits" ]; then
    echo "check_final_state_voice: tracked text refers to the development process:" >&2
    echo "$strict_hits" >&2
    echo >&2
    echo "Rewrite each line to state the fact and its reason in the present tense; keep" >&2
    echo "the history in the commit message." >&2
    status=1
fi

advisory_hits=$(git grep -nIPi "$ADVISORY" -- . "${EXCLUDE[@]}" "${HISTORY_FILES[@]}" \
    ':!scripts/check_final_state_voice.sh' || true)
if [ -n "$advisory_hits" ]; then
    count=$(printf '%s\n' "$advisory_hits" | wc -l | tr -d ' ')
    echo "check_final_state_voice: $count advisory hit(s) -- read each in context:"
    echo "$advisory_hits"
fi

if [ "$status" -eq 0 ]; then
    echo "check_final_state_voice: no process references in tracked text."
fi
exit "$status"
