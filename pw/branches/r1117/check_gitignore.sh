#!/bin/bash
# Проверяет, какие файлы в ветке не покрыты .gitignore
#
# Usage:
#   ./check_gitignore.sh                  # текущая ветка vs main
#   ./check_gitignore.sh linux_server     # конкретная ветка
#   ./check_gitignore.sh linux_server main  # ветка vs конкретная база

set -euo pipefail

BRANCH="${1:-$(git rev-parse --abbrev-ref HEAD)}"
BASE="${2:-main}"

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

BRANCH_POINT=$(git merge-base "$BRANCH" "$BASE")

echo "=== Ветка: $BRANCH → $BASE (merge-base: $(git log -1 --oneline "$BRANCH_POINT")) ==="
echo ""

TMPDIR=$(mktemp -d)
trap "rm -r '$TMPDIR'" EXIT

# Собираем список всех изменённых файлов
git diff --name-only "$BRANCH_POINT..$BRANCH" | LC_ALL=C sort > "$TMPDIR/all.txt"
TOTAL=$(wc -l < "$TMPDIR/all.txt")

# Проверяем какие попадают под .gitignore
git diff --name-only "$BRANCH_POINT..$BRANCH" | \
    git check-ignore --stdin 2>/dev/null | \
    cut -d: -f2- | LC_ALL=C sort -u > "$TMPDIR/ignored.txt"
IGNORED=$(wc -l < "$TMPDIR/ignored.txt")

# Вычитаем — осталось
comm -23 "$TMPDIR/all.txt" "$TMPDIR/ignored.txt" > "$TMPDIR/remaining.txt"
REMAINING=$(wc -l < "$TMPDIR/remaining.txt")

echo "Всего изменённых:  $TOTAL"
echo "Отсеяно .gitignore: $IGNORED"
echo "Осталось:           $REMAINING"
echo ""

if [ "$REMAINING" -gt 0 ]; then
    echo "=== Оставшиеся файлы (не покрыты .gitignore) ==="
    cat "$TMPDIR/remaining.txt"

    echo ""
    echo "=== Распределение по типам ==="
    echo -n "  .cpp:    " && grep -c '\.cpp$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .h:      " && grep -c '\.h$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .hpp:    " && grep -c '\.hpp$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .py:     " && grep -c '\.py$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .md:     " && grep -c '\.md$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .sh:     " && grep -c '\.sh$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .cfg:    " && grep -c '\.cfg$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  .cmake:  " && grep -c '\.cmake$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
    echo -n "  прочее:  " && grep -cv -E '\.(cpp|h|hpp|py|md|sh|cfg|cmake)$' "$TMPDIR/remaining.txt" 2>/dev/null || echo 0
else
    echo "Все файлы покрыты .gitignore ✓"
fi
