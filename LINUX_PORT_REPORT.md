# Отчёт: портирование UniServerApp на Linux

## Статус
- ✅ **Компиляция** — все исходные файлы компилируются успешно
- ❌ **Линковка** — заблокирована отсутствием библиотек Terabit

## Статистика изменений
- **161 файл изменён**, +290 / -58 строк
- **Ветка:** `linux_server`
- **Коммит:** `57f57c147`

---

## Исправленные проблемы компиляции

### Совместимость Windows/Linux

| Проблема | Решение |
|----------|---------|
| `sprintf_s` (Windows-only) | Inline-функции в `systemStdAfx.h`: `vsprintf` без размера, `vsnprintf` с размером |
| `InterlockedIncrement/Decrement` | `__sync_fetch_and_add/sub` в `systemStdAfx.h` |
| `Sleep()` | `threading::Sleep()` или прямой вызов |
| `__cdecl` | Удалён (не нужен на Linux) |
| `GetModuleFileName` | `readlink("/proc/self/exe")` в `FilePath.cpp` |
| `SetCurrentDirectory` | `chdir()` в `FilePath.cpp` |
| `GetCurDiskDirectory` | `getcwd()` |
| `_wtoi` | `wcstol()` в `CumulativePerfCounter.cpp` |
| `W3Client` (ZZima HTTP) | `#ifdef _WIN32` вокруг `myPost()` |
| `Compatibility.cpp` (Wine detection) | Пустые реализации на Linux |
| `CrashRpt` | Отключён на Linux через `settings.enableCrashRpt` |

### Заголовочные файлы

| Проблема | Решение |
|----------|---------|
| `nlist.h` vs системный `<list>` | Обёртка в `15.2.0/list`: `#ifndef NLIST_H__ #include <list> #endif` |
| Boost TR1 + GCC 15 | Каталог `15.2.0/` со symlink на системные заголовники |
| Boost TR1 в include paths | Убран из `Vendor/boost/all.component` |
| `<stdexcept>` не подключён | Добавлен в `systemStdAfx.h` |
| `<cstdlib>`, `<cerrno>` не подключены | Добавлены в `systemStdAfx.h` |
| Чувствительность регистра путей | `chatsvc/` → `ChatSvc/`, `base.h` → `Base.h`, `SystemStdAfx.h` → `systemStdAfx.h` |

### Thrift

| Проблема | Решение |
|----------|---------|
| `cxxfunctional.h` использует TR1 | Патч: `#if __GNUC__ >= 5` → системный `<functional>` |
| `VERSION` не определён | `#define VERSION "0.9.1"` в `Thrift.h` |
| `htons`/`htonl` не объявлены | `<arpa/inet.h>` в `Thrift.h` |
| `gai_strerror`/`poll`/`addrinfo` | `<netdb.h>`, `<poll.h>`, `<sys/socket.h>` в `PlatformSocket.h` |
| `std::shared_ptr` vs `boost::shared_ptr` | Удалены компоненты MonitoringSvc и ClusterAdminSvc из сборки |

### Генератор сборки

| Проблема | Решение |
|----------|---------|
| CxxTest FATAL_ERROR на Linux | Изменён на WARNING в `main.py` |
| `enableCrashRpt = True` на Linux | Условие `if platform == 'win32'` в `.application` |

---

## Удалённые компоненты

| Компонент | Причина |
|-----------|---------|
| `Monitoring/MonitoringSvc` | Зависит от Thrift, не критичен для сервера |
| `ClusterAdmin/ClusterAdminSvc` | Зависит от Thrift, не критичен для сервера |

---

## Текущий блокер: линковка

Не найдены библиотеки:
- **`libIOTerabit.so`** — кастомный I/O фреймворк Terabit
- **`libTProactor.so`** — async I/O проактор Terabit  
- **`libzdll.so`** — нестандартное имя для zlib

### Что нужно сделать

1. **Скомпилировать Terabit для Linux** — нужны исходники и система сборки
2. **Или предоставить готовые .so файлы** — если есть бинарники
3. **Или заменить Terabit** — на Boost.Asio или libev (большие изменения)

### Где искать Terabit
- Исходники: `Vendor/Terabit/` (есть в проекте)
- Конфигурация: `Vendor/Terabit/all.component` ожидает `.so` файлы в `Vendor/Terabit/lib/`
- Зависит от ACE (`Vendor/ACE_wrappers`)

---

## Команды для повторения сборки

```bash
# Генерация CMake
cd prime-world-classic/pw/branches/r1117/Src/Game/PF/UniServer
source /home/rekon/miniconda3/etc/profile.d/conda.sh && conda run -n py27 python \
  /home/rekon/PW/prime-world-classic/pw/branches/r1117/Tools/TestFramework/run.py \
  --compiler cmake --platform linux --generateOnly \
  UniServerApp.application

# Сборка (параллельная, ~2 минуты вместо ~10)
cd build_linux_test
rm -rf CMakeCache*  # если менялся CMakeLists.txt
cmake ../UniServerApp.auto/UniServerApp -DCMAKE_POLICY_VERSION_MINIMUM=3.5
make -j$(nproc)
```
