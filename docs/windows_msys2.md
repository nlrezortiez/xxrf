# Сборка под Windows через MSYS2 (`ucrt64`)

Этот проект проверен в среде `MSYS2 UCRT64`. Ниже приведён минимальный пошаговый сценарий, при котором собираются библиотека, примеры, тесты и приложения, включая `xxrf_viewer`.

## 1. Установить MSYS2

Установить MSYS2 с официального сайта:

- https://www.msys2.org/

После установки открыть терминал `MSYS2 UCRT64`.

## 2. Обновить систему

В `UCRT64`-терминале выполнить:

```bash
pacman -Syu
```

Если MSYS2 попросит закрыть окно и запустить обновление повторно, сделать это и снова выполнить:

```bash
pacman -Syu
```

## 3. Установить зависимости

Установить базовый toolchain и библиотеки, которые требуются проекту:

```bash
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-glfw \
  mingw-w64-ucrt-x86_64-glew \
  mingw-w64-ucrt-x86_64-libusb
```

Этого достаточно для:

- сборки bundled `libhackrf`
- сборки `xxrf_viewer` на `GLFW + OpenGL + GLEW + ImGui`
- запуска тестов и примеров

## 4. Подготовить исходники

Склонировать репозиторий и перейти в него:

```bash
git clone <repo-url> xxrf
cd xxrf
```

Если `third_party/imgui` у тебя не в основном дереве, а был добавлен отдельным `git clone`, убери вложенный `.git`, чтобы директория воспринималась как vendored код:

```bash
rm -rf third_party/imgui/.git
```

## 5. Убедиться, что шрифты лежат в `assets`

Viewer ищет шрифты в директории `assets` репозитория. Должны присутствовать файлы:

- `assets/JetBrainsMonoNerdFontPropo-Regular.ttf`
- `assets/JetBrainsMonoNerdFontPropo-SemiBold.ttf`

Если их нет, viewer откатится на `ImGui::AddFontDefault()`, но внешний вид будет отличаться.

## 6. Сконфигурировать проект

Рекомендуемый генератор: `Ninja`.

```bash
cmake -S . -B build/ucrt64 -G Ninja \
  -DXXRF_BUILD_EXAMPLES=ON \
  -DXXRF_BUILD_APPS=ON \
  -DXXRF_BUILD_TESTS=ON
```

## 7. Собрать проект

Полная сборка:

```bash
cmake --build build/ucrt64 -j
```

Или только viewer:

```bash
cmake --build build/ucrt64 -j --target xxrf_viewer
```

## 8. Прогнать тесты

```bash
ctest --test-dir build/ucrt64 --output-on-failure
```

## 9. Запустить приложения

Viewer:

```bash
./build/ucrt64/apps/xxrf_viewer/xxrf_viewer.exe
```

AoA probe:

```bash
./build/ucrt64/apps/xxrf_aoa_probe/xxrf_aoa_probe.exe --help
```

Примеры:

```bash
./build/ucrt64/examples/xxrf_example_list_devices.exe
```

## 10. Собрать portable bundle для передачи другому пользователю


Для этого в проекте есть два специальных таргета.

### Собрать portable-папку

```bash
cmake -S . -B build/ucrt64-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ucrt64-release -j --target xxrf_bundle_viewer
```

После этого появится директория:

```text
build/ucrt64-release/dist/xxrf_viewer-win64
```

Внутри неё будут:

- `xxrf_viewer.exe`
- runtime DLL, которые требуются именно этой сборке
- `assets/`
- `README`

### Собрать zip-архив

```bash
cmake --build build/ucrt64-release -j --target xxrf_bundle_viewer_zip
```

После этого появится файл:

```text
build/ucrt64-release/dist/xxrf_viewer-win64.zip
```

## 11. Что уже учтено в проекте для MSYS2

В текущем коде уже зафиксированы две важные вещи для `ucrt64`:

- `stdc++exp` линкуется централизованно через общий CMake-хук для `MinGW + GNU`, поэтому `std::print` больше не нужно прописывать вручную по таргетам.
- Viewer больше не использует Linux-only путь вида `/usr/share/fonts/...`; шрифты берутся из локальной папки `assets`.

## 12. Типичные проблемы

### `find_package(glfw3)` или `find_package(GLEW)` не находит библиотеки

Обычно это означает, что проект собирается не из `UCRT64`-терминала, а из другой MSYS2-среды.

Проверь:

```bash
echo $MSYSTEM
```

Ожидаемое значение:

```text
UCRT64
```

### Ошибки линковки, связанные с `std::print`

Если проект свежий, этого уже быть не должно: `stdc++exp` подцепляется автоматически через `xxrf_apply_common()`.

Если ошибка всё же появляется, проверь, что конфигурация действительно пересоздана после обновления CMake-файлов:

```bash
cmake -S . -B build/ucrt64 -G Ninja
```

### Viewer стартует без кастомных шрифтов

Проверь наличие файлов:

- `assets/JetBrainsMonoNerdFontPropo-Regular.ttf`
- `assets/JetBrainsMonoNerdFontPropo-SemiBold.ttf`

## 13. Работа с HackRF под Windows

Сборка проекта и работа с устройством — это разные слои.

Для реального доступа к HackRF под Windows дополнительно нужно:

- чтобы устройство определялось системой корректно
- чтобы был установлен совместимый USB-драйвер для `libusb`-стека

Если viewer собирается, но не видит устройства, проблема обычно уже не в CMake и не в коде, а в драйвере/USB-стеке Windows.

## 14. Рекомендуемый сценарий

Для чистого повторяемого билда:

```bash
cmake -S . -B build/ucrt64 -G Ninja \
  -DXXRF_BUILD_EXAMPLES=ON \
  -DXXRF_BUILD_APPS=ON \
  -DXXRF_BUILD_TESTS=ON
cmake --build build/ucrt64 -j
ctest --test-dir build/ucrt64 --output-on-failure
./build/ucrt64/apps/xxrf_viewer/xxrf_viewer.exe
```
