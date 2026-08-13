#!/bin/bash
# Анализирует diff исходных кодов (.cpp/.h) в ветке vs базовой ветки.
# Показывает только реальные логические изменения, исключая case-fix в #include.
#
# Usage:
#   ./analyze_source_diffs.sh                    # текущая ветка vs main
#   ./analyze_source_diffs.sh linux_server       # конкретная ветка
#   ./analyze_source_diffs.sh linux_server dev   # ветка vs конкретная база
#
# Запускать из любой директории внутри git-репозитория.

set -euo pipefail

BRANCH="${1:-$(git rev-parse --abbrev-ref HEAD)}"
BASE="${2:-main}"

# Находим корень репозитория
REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

BRANCH_POINT=$(git merge-base "$BRANCH" "$BASE")

echo "=== Ветка: $BRANCH → $BASE (merge-base: $(git log -1 --oneline "$BRANCH_POINT")) ==="
echo "    РЕЖИМ: только логические изменения (case-fix в #include исключены)"
echo ""

###############################################################################
# 1. Общая статистика
###############################################################################
echo "═══════════════════════════════════════════════════════════"
echo "  ОБЩАЯ СТАТИСТИКА"
echo "═══════════════════════════════════════════════════════════"

MODIFIED=$(git diff --name-only --diff-filter=M "$BRANCH_POINT" HEAD | wc -l)
ADDED=$(git diff --name-only --diff-filter=A "$BRANCH_POINT" HEAD | wc -l)
DELETED=$(git diff --name-only --diff-filter=D "$BRANCH_POINT" HEAD | wc -l)
TOTAL=$((MODIFIED + ADDED + DELETED))

echo "Изменённых всего:   $TOTAL"
echo "  Модифицировано:   $MODIFIED"
echo "  Добавлено:        $ADDED"
echo "  Удалено:          $DELETED"
echo ""

# Статистика по исходникам
MOD_SRC=$(git diff --name-only --diff-filter=M "$BRANCH_POINT" HEAD | grep -cE '\.(cpp|h|hpp)$' || true)
ADD_SRC=$(git diff --name-only --diff-filter=A "$BRANCH_POINT" HEAD | grep -cE '\.(cpp|h|hpp)$' || true)
DEL_SRC=$(git diff --name-only --diff-filter=D "$BRANCH_POINT" HEAD | grep -cE '\.(cpp|h|hpp)$' || true)

echo "Исходный код (.cpp/.h/.hpp):"
echo "  Модифицировано:   $MOD_SRC"
echo "  Добавлено:        $ADD_SRC"
echo "  Удалено:          $DEL_SRC"
echo ""

###############################################################################
# 2. Классификация модифицированных исходников
###############################################################################
echo "═══════════════════════════════════════════════════════════"
echo "  КЛАССИФИКАЦИЯ МОДИФИЦИРОВАННЫХ ФАЙЛОВ"
echo "═══════════════════════════════════════════════════════════"

TMPDIR=$(mktemp -d)
trap "rm -r '$TMPDIR'" EXIT

# Список модифицированных исходников
git diff --name-only --diff-filter=M "$BRANCH_POINT" HEAD | grep -E '\.(cpp|h|hpp)$' > "$TMPDIR/modified_src.txt" || true

COUNT=$(wc -l < "$TMPDIR/modified_src.txt")
if [ "$COUNT" -eq 0 ]; then
    echo "Нет модифицированных исходников."
    exit 0
fi

echo "Модифицировано исходников: $COUNT"
echo ""

# Классификация: 4 категории
> "$TMPDIR/case_only.txt"       # только case-fix в #include
> "$TMPDIR/logical_only.txt"    # только логические изменения
> "$TMPDIR/both.txt"             # и case-fix, и логические
> "$TMPDIR/empty_diff.txt"       # пустой diff

echo "Сортировка по типам изменений..."

while IFS= read -r f; do
    DIFF=$(git diff --text "$BRANCH_POINT" HEAD -- "$f" 2>/dev/null | tr -d '\000' || true)
    if [ -z "$DIFF" ]; then
        echo "$f" >> "$TMPDIR/empty_diff.txt"
        continue
    fi

    HAS_CASE_FIX=0
    HAS_LOGICAL=0

    # Проверяем case-fix в #include
    INCLUDE_LINES=$(printf '%s' "$DIFF" | LC_ALL=C grep -aE '^[+-]#include' 2>/dev/null || true)
    if [ -n "$INCLUDE_LINES" ]; then
        HAS_CASE_FIX=1
    fi

    # Проверяем реальные изменения кода:
    # Исключаем diff-header, include, пустые строки
    REAL_CHANGES=$(printf '%s' "$DIFF" | LC_ALL=C grep -a "^[+-]" \
        | LC_ALL=C grep -v "^[+-][+-][+-]" \
        | LC_ALL=C grep -v '^[+-]#include' \
        | LC_ALL=C grep -v "^[+-]$" \
        | LC_ALL=C grep -v "^[+-] *$" \
        | head -10 || true)

    if [ -n "$REAL_CHANGES" ]; then
        HAS_LOGICAL=1
    fi

    if [ "$HAS_CASE_FIX" -eq 1 ] && [ "$HAS_LOGICAL" -eq 1 ]; then
        echo "$f" >> "$TMPDIR/both.txt"
    elif [ "$HAS_CASE_FIX" -eq 1 ]; then
        echo "$f" >> "$TMPDIR/case_only.txt"
    elif [ "$HAS_LOGICAL" -eq 1 ]; then
        echo "$f" >> "$TMPDIR/logical_only.txt"
    else
        echo "$f" >> "$TMPDIR/empty_diff.txt"
    fi
done < "$TMPDIR/modified_src.txt"

N_CASE_ONLY=$(wc -l < "$TMPDIR/case_only.txt")
N_LOGICAL_ONLY=$(wc -l < "$TMPDIR/logical_only.txt")
N_BOTH=$(wc -l < "$TMPDIR/both.txt")
N_EMPTY=$(wc -l < "$TMPDIR/empty_diff.txt")

echo "  Только case-fix:             $N_CASE_ONLY  (исключены из diff)"
echo "  Только логические изменения:   $N_LOGICAL_ONLY"
echo "  Оба (case-fix + логика):      $N_BOTH  (показан diff без case-fix)"
echo "  Пустой diff (metadata/mode):   $N_EMPTY"
echo ""

# Объединённый список файлов с логикой
cat "$TMPDIR/logical_only.txt" "$TMPDIR/both.txt" > "$TMPDIR/all_logical.txt"
N_ALL_LOGICAL=$(wc -l < "$TMPDIR/all_logical.txt")

###############################################################################
# 3. Распределение логических изменений по каталогам
###############################################################################
if [ "$N_ALL_LOGICAL" -gt 0 ]; then
    echo "═══════════════════════════════════════════════════════════"
    echo "  ФАЙЛЫ С ЛОГИЧЕСКИМИ ИЗМЕНЕНИЯМИ ПО КАТАЛОГАМ ($N_ALL_LOGICAL)"
    echo "═══════════════════════════════════════════════════════════"
    cat "$TMPDIR/all_logical.txt" | sed 's|^.*Src/|Src/|; s|^.*Vendor/|Vendor/|' | cut -d'/' -f1-3 | sort | uniq -c | sort -rn
    echo ""
fi

###############################################################################
# 4. Распределение удалённых файлов по каталогам
###############################################################################
if [ "$DELETED" -gt 0 ]; then
    echo "═══════════════════════════════════════════════════════════"
    echo "  УДАЛЁННЫЕ ФАЙЛЫ ПО КАТАЛОГАМ (топ-15)"
    echo "═══════════════════════════════════════════════════════════"
    git diff --name-only --diff-filter=D "$BRANCH_POINT" HEAD | cut -d'/' -f1-3 | sort | uniq -c | sort -rn | head -15
    echo ""
fi

###############################################################################
# 5. Распределение добавленных файлов по типам
###############################################################################
if [ "$ADDED" -gt 0 ]; then
    echo "═══════════════════════════════════════════════════════════"
    echo "  ДОБАВЛЕННЫЕ ФАЙЛЫ ПО ТИПАМ"
    echo "═══════════════════════════════════════════════════════════"
    git diff --name-only --diff-filter=A "$BRANCH_POINT" HEAD | sed 's/.*\.//' | sort | uniq -c | sort -rn | head -15
    echo ""
fi

###############################################################################
# 6. Диффы логических изменений (case-fix скрыты)
###############################################################################
if [ "$N_ALL_LOGICAL" -gt 0 ]; then
    echo "═══════════════════════════════════════════════════════════"
    echo "  ДИФФЫ ЛОГИЧЕСКИХ ИЗМЕНЕНИЙ (case-fix скрыты)"
    echo "═══════════════════════════════════════════════════════════"

    while IFS= read -r f; do
        # git diff с фильтрацией: убираем строки с #include
        DIFF=$(git diff --text "$BRANCH_POINT" HEAD -- "$f" 2>/dev/null | tr -d '\000' \
            | LC_ALL=C grep -v '^[+-]#include' 2>/dev/null || true)
        # Показываем только если после фильтрации осталось что-то кроме ---/+++/@@
        MEANINGFUL=$(echo "$DIFF" | grep -v "^[---@@]" | grep -v "^diff " | grep -v "^index " | grep -v "^$" | head -5 || true)
        if [ -n "$MEANINGFUL" ]; then
            echo ""
            echo "--- $f ---"
            echo "$DIFF" | head -80
        fi
    done < "$TMPDIR/all_logical.txt"
    echo ""
fi

###############################################################################
# 7. Итог
###############################################################################
echo "═══════════════════════════════════════════════════════════"
echo "  ИТОГ"
echo "═══════════════════════════════════════════════════════════"
echo ""
echo "Всего файлов в ветке:     $TOTAL"
echo "  Из них удалено:           $DELETED"
echo "  Из них добавлено:         $ADDED"
echo "  Из них модифицировано:    $MODIFIED"
echo ""
echo "Модифицированные исходники: $MOD_SRC"
echo "  Только case-fix (скрыты):     $N_CASE_ONLY"
echo "  Только логические:            $N_LOGICAL_ONLY"
echo "  Оба (логика + case-fix):      $N_BOTH"
echo "  Пустой diff:                  $N_EMPTY"
echo "  → Итого с логикой:            $N_ALL_LOGICAL"
echo ""
echo "Добавленные исходники:      $ADD_SRC"
echo "Удалённые исходники:        $DEL_SRC"
