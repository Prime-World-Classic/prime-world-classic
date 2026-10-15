# hlsl_sm3_check — побайтовая сверка SM3-пути шейдеров

Зачем. Правки исходников `.hlsl` ради Vulkan (макрос `PW_MODERN_ARRAYS`,
шаг 10 плана `docs/PLAN_NVRHI.md` §3.z п.2) не должны менять то, что
клиент получает на D3D9: поставляемые `.shd` перегенерировать «на всякий
случай» нельзя — любой сдвиг в DXBC требует визуального гейта и нового
эталона. Тул сравнивает DXBC каждой единицы корпуса (материал × стадия ×
профиль пинов) до правки и после.

Как устроен. Корпус и пины берутся из `Src/Render/Shaders/MaterialSpec.xml`
(тот же источник, что у `tools/shader_budget`). Компиляция —
`tools/hlsl_compile_test` (реальный `D3DCompile` из `d3dcompiler_47.dll`,
профили `vs_3_0`/`ps_3_0`, включаемые каталоги `--inc`). Для каждой единицы
печатаются `hr`, размер DXBC и md5; режим `--save` пишет JSON-снимок,
`--compare` сверяет с ним.

Запуск. Компилятор должен быть **настоящим fxc-пути**: под Wine
`d3dcompiler` встроен через vkd3d и на части материалов даёт
`E5017: Aborting due to not yet implemented feature: SM1 cast from bool to
integer` (проверено на `AOEMaterial`), поэтому нужен Windows-хост:

    # на tiny10: C:\tools\hlsl\hlsl_compile_test.exe (scp с хоста),
    # дерево исходников: C:\pwbuild\shaders\Src\Render\Shaders
    python3 tools/hlsl_sm3_check/hlsl_sm3_check.py --profile affected \
        --remote pw-tiny10 --save /tmp/sm3_base_p.json
    python3 tools/hlsl_sm3_check/hlsl_sm3_check.py --profile affected \
        --remote pw-tiny10 --compare /tmp/sm3_base_p.json

Профили (`--profile`): `all` — все комбинации пинов из спецификации;
`affected` — только те, где компилируются материалы, затронутые правкой
шага 10 (`AFFECTED_UNITS` + `AFFECTED_PINS` в скрипте; при изменении
материалов таблицу надо обновить); `fast` — smoke-подмножество.
Точечно: `--only <материал>`, `--stages vs|ps|both`, `--pins N`.

Итог. `одинаковых DXBC: N, изменившихся: 0, пропало единиц: 0, новых: 0` →
`ИТОГ: SM3-путь побайтово не изменился`. Если DXBC изменился, тул печатает
размеры до/после; дальше — `hlsl_compile_test --disasm`, чтобы отличить
смену кодогенерации от смены операций (см. `tools/hlsl_compile_test/README.md`).
