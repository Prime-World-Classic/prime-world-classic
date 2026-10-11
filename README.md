# Prime World Classic #1 fork

## Prime World
Исходный код боевой части игры Prime World.  
Внимательно ознакомьтесь с условиями лицензионного соглашения.

## Содержимое
### LFS
Было решено избавиться от LFS, перенеся их в Release. Отсутствующие аудио файлы [скачайте из релиза Audio](https://github.com/Prime-World-Classic/Prime-World/releases/tag/Audio) и положите в папку `pw\branches\r1117\Data\Audio`
### Папки
- pw — основной код боевой части
- pw_publish — собранный клиент боевой части с читами и редактор для клиента
- Client-Synchrinizer-Server - python скрипт синхронизатора, необходимого для связи клиентов с сервером (бэкэнд + боевая сессия) во время старта боевых сессий
- Data_Patch - производная директория для скрипта `copy_data_patch.bat`
- PW_NanoUpdater - проект, компилируемый в три таргета:
* PW_NanoUpdater и PW_NanoUpdaterAdm - обновление игры на клиентах посредством C++ библиотеки libgit2
* PW_HashesTest - проверка целостности хэш-сумм файлов Packs на клиентах
* rating - общие алгоритмы начисления рейтингов (калибровочная .py и основная .html)

### Файлы
- make-links.bat - создаёт junction ссылки на директории **Data**, **Localization**, **Profiles**, **Tools** из `pw\branches\r1117` в `pw_publish\branch\Client\PvP`
- copy_data_patch.bat - копирует файлы, перечисленные в коммите из `pw\branches\r1117\Data` в `Data_Patch\Data`. Использование `copy_data_patch.bat ea03238`
- `setup_update_repos_https.bat` — инициализирует репозитории обновлений (`content`, `PWCGitUpdates`, а также тестовые версии `content-test` и `pwc-gitupdates-test`) через HTTPS, автоматически добавляя GitLab и GitHub зеркала в список remote-репозиториев.
- `setup_update_repos_ssh.bat` — инициализирует репозитории обновлений (`content`, `PWCGitUpdates`, а также тестовые версии `content-test` и `pwc-gitupdates-test`) через SSH, автоматически добавляя GitLab и GitHub зеркала в список remote-репозиториев.
- `setup_update_repos_https.sh` — Linux/macOS-версия скрипта инициализации репозиториев обновлений через HTTPS с настройкой дополнительных remote-зеркал GitLab и GitHub.
- `setup_update_repos_ssh.sh` — Linux/macOS-версия скрипта инициализации репозиториев обновлений через SSH с настройкой дополнительных remote-зеркал GitLab и GitHub.

- update.sh - скрипт обновления для Linux - Lutris версии

## Компиляция клиента и сервера
Основной солюшен: `pw\branches\r1117\Src\PF.sln`. Открывается в Visual Studio 2008 Professional SP1. Актуальные и проверенные конфигурации: ShippingSingleExe - публичная версия, ReleaseSingleExe - тестовая версия с читами. На данный момент компилируются:
* PW_Game.exe
* UniServerApp

Не компилируется:
* PF_Editor

Публичная конфигурация - ShippingSingleExe. Тестовая конфигурация (с читами) - ReleaseSingleExe. Возможны проблемы с компиляцией некоторых конфигураций/проектов на разных этапах жизни репозитория.

### Пример файла server_ip.h
В репозитории не версионируется файл server_ip.h. Его необходимо будет доложить в папку `pw\branches\r1117\Srv\PW_Game`. Ниже пример его содержимого:
```
#pragma once

#define SESSION_TOKEN "Tester00Tester00Tester00Tester00"
#define API_KEY "APIKEY00APIKEY00APIKEY00APIKEY00APIKEY00"

// Main server IP
#define SERVER_IP_W L"123.123.123.123"
#define SERVER_IP "123.123.123.123"

// Proxy server IP
#define SERVER_PROXY_IP_W L"123.123.123.123"
#define SERVER_PROXY_IP "123.123.123.123"

// Radmin server IP
#define MIRROR_SERVER_IP_W L"123.123.123.123"
#define MIRROR_SERVER_IP "123.123.123.123"

#define SERVER_PORT "27300"
#define LOGIN_PORT "27301"

#define SERVER_CLUSTER_PORT_FRONT 27310
#define SERVER_CLUSTER_PORT_BACK 27340

#define SYNCHRONIZER_PORT 27302


extern int usedServer;
static const char* SERVER_IP_ARRAY[] = {SERVER_IP, MIRROR_SERVER_IP, SERVER_PROXY_IP};
static const wchar_t* SERVER_IP_W_ARRAY[] = {SERVER_IP_W, MIRROR_SERVER_IP_W, SERVER_PROXY_IP_W};
```




## Запуск клиента с читами в режиме локальной игры
Необходимо клонировать ветку main и объединить папку Bin с основными данными игры.

1. Запустите make-links.bat
2. В `pw_publish\branch\Client\PvP\Profiles\game.cfg` должно стоять значение `local_game 3` или `local_game 1` для запуска игры без активного сервера.
3. Запуск клиента с читами `pw_publish\branch\Client\PvP\Bin\PW_Game.exe`.

### Общие сведения при запуске игры
1. Вы должны увидеть лобби, где можете выбрать карту, героя и уйти в бой.
2. Первый запуск будет долгим из-за загрузки файлов в оперативную память.
3. Для героев используются билды по умолчанию.
4. В бою нажмите тильду — откроется консоль для ввода читов. Список команд на **help** крашит игру, но в логах отобразится список.

В случае возникновения ошибок смотрите логи в `pw_publish\branch\Client\PvP\Bin\logs`.

## Данные игры
Данные редактируются через редактор.  
Расположены в:  
`pw\branches\r1117\Data`

Через данные можно:
1. Менять описания талантов и способностей героев.
2. Менять таланты и способности героев.
3. Менять логику крипов и башен.
4. Добавлять героев и способности.
5. Добавлять таланты.
6. Менять и добавлять эффекты.
7. Менять и добавлять модели и анимации.

При изменении данных новый клиент собирать из кода не нужно. Нажмите File -> Save, и все изменения сразу подтянутся в клиент PW_Game. Для примера, можете попробовать поменять описание какого-нибудь таланта или способности героя.

## Редактор
Находится в:  
`pw_publish\branch\Client\PvP\Bin\PF_Editor.exe`

При первом открытии редактора нужно настроить путь к `Data`:
1. Tools -> File System Configuration.
2. Add -> WinFileSystem.
3. В качестве system root установите папку Data: `pw_publish\branch\Client\PvP\Data`.
4. Закройте окна.
5. В редакторе: Views -> Object Browser и Views -> Properties Editor. Это две основные панели для редактирования данных.

Вкладки редактора можно перемещать и закреплять.

## Как запустить PvP
1. В `Profiles -> game.cfg` выставить значение `local_game 0`.
2. В `login_adress` указать <адрес сервера>.
3. Запустите игру с параметром -dev_login MyNickname или PW_MiniLauncher.exe. При передаче аргументов учитывайте наличие пробелов.

## Как запустить игру с ботами и крипами
1. В `Profiles -> найдите файл в `private.cfg`.
2. Откройте файл через блокнот.
3. Найдите `AT BEGINNING GAME`.
4. Вставьте новую строку: `add_ai bots` — это для каждого героя в игре поставит ИИ бота.
5. Для спавна крипов фракций `spawn_creeps 6` (битовая маска 2 | 4)
6. Для спавна нейтральных крипов (лагеря в лесу, мини-боссы) `spawn_neutral_creeps 1`
7. Данные команды также можно прописать в консоли в игре

# Список серверов Prime World по состоянию на 05.05.2024
## Prime World: Classic
* Активный сервер Prime World, сообщество ВК насчитывает 5000+ подписчиков
* Сервер организации Prime World Classic, владеющей данным репозиторием на github. Разработка ведётся в пределах репозиториев данной организации. Будем рады помощи в разработке.
### Ссылки
* Страница ВК: https://vk.com/primeworldclassic
* Сообщество в Telegram: https://t.me/primeworldclassic
* Сообщество Discord: https://discord.gg/S3yrbFGT86
* Страница Steam: https://store.steampowered.com/app/3684820/Prime_World_Classic/
* Сайт с веб-версией замка: https://playpw.fun/
* Сайт со служебной информацией: https://pw.26rus-game.ru/
  
## Prime World: Nova
* Активный сервер Prime World, сообщество ВК насчитывает 10000+ подписчиков
### Ссылки
* Страница ВК: https://vk.com/pw_nova
* Сообщество в Telegram: https://t.me/PW_Nova
* Сообщество Discord: https://discord.gg/F5UCRsD7QJ
* Сайт: https://playnova.ru

# Другие известные серверы
## Prime World: Reborn
* На данный момент неизвестно, функционирует ли сервер.
### Ссылки
* Страница ВК: https://vk.com/pw.reborn

## Prime World: Legends
* Находится в стадии открытого бета-тестирования.
### Ссылки
* Страница Steam: https://store.steampowered.com/app/3602240/Prime_World_Legends/
* Сообщество Telegram: https://t.me/prime_world_legends_game
* Сайт: https://primeworld.top
