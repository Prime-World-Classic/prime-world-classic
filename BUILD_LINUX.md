# Сборка Linux-версии UniServerApp

## Введение

Инструкция описывает сборку серверной части Prime World Classic (UniServerApp) на Linux (Ubuntu 24.04+). Проект изначально разрабатывался под Windows (Visual Studio 2008), поэтому сборка на Linux требует обхода ряда проблем.

## Системные требования

- **OS**: Ubuntu 24.04+ (или любой дистрибутив с GCC 15+)
- **Python 2.7** (через conda) — для генерации CMake из системы компонентов
- **CMake 4.x** — для сборки
- **Thrift compiler** (`thrift-compiler` пакет)
- **Системные библиотеки**:
  - `libace-dev` (ACE 8.0.5 — используется только для заголовков, линкуется вендорный ACE 5.7)
  - `libssl-dev`, `libcrypto-dev`
  - `libcurl4-openssl-dev`
  - `libz-dev`
  - `libthrift-dev`
  - `libtinyxml2-dev` или `libtinyxml-dev`
  - `build-essential`

## Предварительные приготовления

### 1. Установка Python 2.7 через conda

Генератор CMake требует Python 2.7. Установите conda и создайте окружение:

```bash
# Если conda уже установлен, пропустите
conda create -n py27 python=2.7 -y
```

### 2. Проверка зависимостей

```bash
sudo apt install -y build-essential cmake thrift-compiler \
  libssl-dev libcurl4-openssl-dev libz-dev \
  libthrift-dev libtinyxml-dev libace-dev
```

### 3. Что уже исправлено в репозитории

Следующие изменения внесены в репозиторий и **не требуют ручной правки**:

- **`Src/Game/PF/UniServer/UniServerApp.application`** — добавлена зависимость `libcrypto.so` для Linux
- **`Tools/TestFramework/main.py`** — генератор автоматически добавляет `SPIPE_Addr.cpp` как OBJECT-таргет с `-fno-rtti` для Linux-сборок
- **`Src/Game/PF/UniServer/main.ace.component`** — чистый компонент без модификаций

Эти изменения решают проблему отсутствующего символа `_ZTV14ACE_SPIPE_Addr` в вендорной библиотеке `libACE-5.7.so`.

## Сборка

### Шаг 1. Генерация CMakeLists.txt

Перейдите в каталог UniServerApp и запустите генератор:

```bash
cd prime-world-classic/pw/branches/r1117/Src/Game/PF/UniServer

# Активируйте Python 2.7
source /путь/к/miniconda3/etc/profile.d/conda.sh
conda activate py27

# Запустите генератор
TestFrameworkPath=/путь/к/prime-world-classic/pw/branches/r1117/Tools/TestFramework \
  python /путь/к/prime-world-classic/pw/branches/r1117/Tools/TestFramework/run.py \
  --compiler cmake --platform linux --generateOnly \
  UniServerApp.application
```

Результат: `UniServerApp.auto/UniServerApp/CMakeLists.txt`

### Шаг 2. CMake-конфигурация

```bash
rm -rf build_linux_test
mkdir build_linux_test
cd build_linux_test

cmake ../UniServerApp.auto/UniServerApp \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_BINARY_DIR="$(pwd)"
```

> **Примечание**: флаг `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` необходим, т.к. генератор создаёт CMakeLists.txt с `cmake_minimum_required(VERSION 2.8)`, что несовместимо с CMake 4.x.
>
> **Важно**: флаг `-DCMAKE_BINARY_DIR="$(pwd)"` обязателен — сгенерированный CMakeLists.txt использует абсолютные пути для всех источников, из-за чего CMake 4.x может записать build-файлы в исходный каталог вместо `build_linux_test`. Явное указание `CMAKE_BINARY_DIR` предотвращает эту проблему.

### Шаг 3. Компиляция

```bash
make -j$(nproc)
```

Полная компиляция занимает ~1-2 минуты на современном процессоре (с параллельной сборкой).

Результат: `build_linux_test/UniServerApp` (~36-40 МБ ELF 64-bit executable)

## Установка

Скопируйте бинарник в каталог сервера:

```bash
cp build_linux_test/UniServerApp \
   /путь/к/prime-world-classic/pw_publish/branch/Server/PvX/Bin/
```

## Запуск

### Запуск координатора

```bash
cd pw_publish/branch/Server/PvX/Bin
./UniServerApp coordinator
```

### Запуск через скрипт

Используйте `start_all.sh`:

```bash
cd pw_publish/branch/Server/PvX/Bin
bash start_all.sh
```

Логи пишутся в `logs/coordinator.log`.

### Остановка

```bash
bash stop_all.sh
# или
pkill -f UniServerApp
```

## Структура запускаемых модулей

UniServerApp — монолитный бинарник, который запускается с разными аргументами:

| Аргумент | Модуль | Описание |
|----------|--------|----------|
| `coordinator` | Координатор | Центральный узел кластера |
| `login` | Сервер авторизации | Обработка логинов |
| `relay` | Ретранслятор | Маршрутизация сообщений |
| `chat` | Чат-сервис | Сервер чата |
| `lobby` | Лобби | Очередь и лобби игроков |
| `matchmaking` | Мэтчмейкинг | Подбор матчей |
| `hybridserver` | Гибридный сервер | Управление игровыми серверами |
| `clusteradmin` | Админ кластера | Администрирование кластера |

## Конфигурация

Файлы конфигурации находятся в `pw_publish/branch/Server/PvX/Profiles/`:

- `server.cfg` — основные настройки сервера
- `cluster.cfg` — настройки кластера
- `srv_private.cfg` — приватные настройки (отсутствует по умолчанию, нужно создать)

### server.cfg

```
setvar coordinator_address = "localhost:35000"
setvar log_rotation_period = 24
setvar cluster_name = "Local"
```

### cluster.cfg

```
setvar coordinator_address = "localhost:35000"
setvar cluster_name = "Local"
setvar first_server_port = 35010
```

## Диагностика

### Ошибка `symbol lookup error: ... libACE-5.7.so: undefined symbol: _ZTV14ACE_SPIPE_Addr`

Это означает, что бинарник собран **без** патчей из генератора. Пересоберите, следуя инструкции выше. Убедитесь, что `Tools/TestFramework/main.py` содержит изменения для `SPIPE_Addr.cpp`.

### Ошибка `symbol lookup error: ... _ZTI8ACE_Addr`

Возникает при компиляции `SPIPE_Addr.cpp` **с RTTI**. Генератор автоматически добавляет `-fno-rtti`. Если проблема сохраняется, проверьте, что OBJECT-таргет `spip_e_addr_obj` присутствует в сгенерированном CMakeLists.txt.

### Ошибка `Cannot read configuration file srv_private.cfg`

Создайте пустой файл:

```bash
touch pw_publish/branch/Server/PvX/Profiles/srv_private.cfg
```

### Ошибка `Cannot read configuration file Profiles/server.cfg`

Запускайте сервер из каталога `pw_publish/branch/Server/PvX/Bin/` или укажите правильный путь к профилям.

### Проверка символов

Убедитесь, что бинарник содержит нужный символ:

```bash
nm -D UniServerApp | grep "_ZTV14ACE_SPIPE_Addr"
# Должно показать: 000000000076b7b8 V _ZTV14ACE_SPIPE_Addr
```

## Пересборка

При изменении кода достаточно:

```bash
cd build_linux_test
make -j$(nproc)
```

При изменении компонентов (`.application`, `.component` файлов) необходимо сначала пересгенерировать CMake (Шаг 1), затем пересобрать.

## Архитектура сборки

```
UniServerApp.application          # Описание приложения (компоненты, настройки)
        ↓
Tools/TestFramework/run.py        # Генератор (Python 2.7)
        ↓
UniServerApp.auto/.../CMakeLists.txt  # Сгенерированный CMake
        ↓
cmake + make                      # Сборка
        ↓
UniServerApp                      # Исполняемый файл
```

### Как работает патч SPIPE_Addr

1. Вендорная `libACE-5.7.so` не содержит символов `ACE_SPIPE_Addr` (SPIPE был исключён при оригинальной компиляции)
2. Код UniServerApp зависит от `ACE_SPIPE_Addr` для межпроцессного общения через Unix domain sockets
3. Генератор добавляет `SPIPE_Addr.cpp` как OBJECT-таргет, компилируемый с `-fno-rtti`
4. Объектный файл линкуется напрямую в исполняемый файл
5. Флаг `--export-dynamic` экспортирует символы в dynamic symbol table
6. При загрузке `libACE-5.7.so` динамический линкер находит недостающие символы в основном бинарнике

## Ссылки

- Репозиторий: https://github.com/Prime-World-Classic/prime-world-classic
- Ветка Linux: `linux_server`
- Сообщество VK: https://vk.com/primeworldclassic
- Discord: https://discord.gg/S3yrbFGT86
