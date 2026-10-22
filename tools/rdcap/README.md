# Капча RenderDoc (Wine-контур `nvrhi`)

Инструменты хоста для съёма `.rdc` кадра клиента, запущенного под Wine.
Процедура, env клиента и грабли — скил `pw-render`, `references/renderdoc.md`
(репо `/home/rekon/PW`, каталог `.pi/skills/`). Здесь — только код инструментов.

| файл | что это |
|---|---|
| `rdctl.py` | клиент протокола target-control (порты 38920–38927 захваченного процесса): `ports`, `trigger [кадров] [порт]`, `queue <кадр> [кадров] [порт]`, `watch [сек] [порт]`, `arm <дельта> [порт]` |
| `rdwatch.py` | наблюдатель маркера клиента (`PWRDCAP 1 seq= cur= frame= sec= api=`): шлёт `QueueCapture(frame)` и ждёт `NewCapture` ровно этого кадра |
| `xkey.c` | XTEST-клавиша на X-дисплей стенда (`cc -O2 -o xkey xkey.c -lX11 -ldl`, `./xkey F12`) — хоткей слоя; `xdotool` на машине нет |
| `implicit_layer.d/renderdoc_capture.json` | манифест слоя `VK_LAYER_RENDERDOC_Capture` с **абсолютным** `library_path` (в поставке RenderDoc там заглушка `/io/dist/lib/librenderdoc.so` — слой не грузится) |

Манифест годится только для конкретной установки RenderDoc: перед использованием
проверить, что `library_path` указывает на `lib/librenderdoc.so` локальной
распаковки (замер: 1.46 в `/home/rekon/PW/tools/renderdoc_1.46/`), и что
`VK_ADD_IMPLICIT_LAYER_PATH=<этот каталог> ENABLE_VULKAN_RENDERDOC_CAPTURE=1
vulkaninfo --summary` показывает слой.

На машине рабочие копии лежат в `~/PW/tools/rd/` (симлинки на эти файлы), чтобы
`wine-client/rdcap_match.sh` мог ссылаться на короткий путь.
