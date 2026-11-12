# DreamWorks Shrek 2 Remastered — CrossOver / Wine `GameInput.dll` Fix

Фикс запуска **DreamWorks Shrek 2 Remastered** в **CrossOver / Wine / Proton** (macOS / Linux) с полной поддержкой **геймпадов и вибрации**.

---

## В чём причина краша

При запуске в CrossOver игра успешно инициализирует Direct3D 12 (через D3DMetal), но сразу после этого падает с окном **BugSplat** и логом в `logs/Shrek-*.log`:

```text
GAME CRASH
Code: 0xC0000005
Address: 00000001402FAF11
W:\BuildAgent\work\swamp2_p4\Code\LuxEngine2\Input\Win64\LuxWindowsPlatformInput.cpp(40): Lux::Input::LuxWindowsPlatformInput::LuxWindowsPlatformInput
```

**Причина:**
1. Игра использует новый API ввода Microsoft **GameInput v1** (`GameInputCreate` из `GameInput.dll`).
2. Встроенная в Wine / CrossOver библиотека `gameinput.dll` является заглушкой и возвращает `0x80004001` (`E_NOTIMPL`).
3. `Shrek.exe` пропускает получение интерфейса `IGameInput`, оставляя указатель равным `NULL`, но сразу после этого вызывает `IGameInput::SetFocusPolicy` без проверки на `NULL` → `0xC0000005` (Access Violation).

---

## Как работает этот фикс

Вместо модификации `Shrek.exe` в папку с игрой добавляется легковесная нативная прослойка **`GameInput.dll`** (~7 КБ, зависит только от `KERNEL32.dll`), которая:
- Реализует используемые игрой методы **GameInput v1** (`GameInputCreate`, `IGameInput`, `IGameInputDevice`, `IGameInputReading`).
- Транслирует состояние геймпадов (кнопки, D-Pad, оба стика, триггеры LT/RT), горячее подключение до 4 контроллеров и вибрацию (`SetRumbleState`) в стандартный **`XINPUT1_4.dll`**, который отлично работает в CrossOver / Wine / Proton.
- Сохраняет стандартную работу клавиатуры и мыши (они в игре работают через Win32 API).

> Примечание: геймпад определяется как контроллер Xbox (`vendorId = 0x045E`), поэтому подсказки кнопок в интерфейсе игры будут в стиле Xbox.

---

## Быстрая установка

### 1. Скопируйте `GameInput.dll` к игре
Возьмите готовый файл [`bin/GameInput.dll`](bin/GameInput.dll) (или скачайте его из раздела **Releases**) и положите его в папку с игрой **рядом с `Shrek.exe`**, например:
```text
drive_c/Games/DreamWorks Shrek 2 Remastered/GameInput.dll
```

### 2. Включите DLL override для `gameinput`
Без этого Wine загрузит свою встроенную заглушку из `system32` вместо файла в папке с игрой.

#### Вариант А: Через интерфейс CrossOver (`winecfg`)
1. Выберите вашу бутылку в CrossOver и нажмите **Run Command...** (Выполнить команду) или откройте **Wine Configuration** (`winecfg`).
2. Перейдите на вкладку **Libraries** (Библиотеки).
   *(Опционально: на вкладке **Applications** можно сначала добавить `Shrek.exe`, чтобы правило применялось только к игре.)*
3. В поле **New override for library** введите:
   ```text
   gameinput
   ```
4. Нажмите **Add** (Добавить) и убедитесь, что стоит режим **`(native, builtin)`** (Сначала сторонняя, потом встроенная).
5. Нажмите **Apply** → **OK**.

#### Вариант Б: Через терминал macOS (для CrossOver)
Подставьте имя вашей бутылки вместо `1243` (и путь к `CrossOver.app` вместо `CrossOver Preview.app`, если используете обычный CrossOver):
```bash
"/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine" --bottle "1243" reg add "HKCU\\Software\\Wine\\AppDefaults\\Shrek.exe\\DllOverrides" /v gameinput /t REG_SZ /d "native,builtin" /f
```

#### Вариант В: Steam / Proton (Linux / Steam Deck)
В свойствах игры в Steam → **Параметры запуска** укажите:
```text
WINEDLLOVERRIDES="gameinput=n,b" %command%
```

---

## Сборка из исходников

Для сборки понадобится `mingw-w64` (`x86_64-w64-mingw32-gcc`).

На macOS через Homebrew:
```bash
brew install mingw-w64
./build.sh
```

На Linux (Debian/Ubuntu):
```bash
sudo apt install gcc-mingw-w64-x86-64
./build.sh
```

После сборки готовый файл появится в `bin/GameInput.dll`.

---

## Структура репозитория

- `src/gameinput_shim.c` — реализация `GameInput v1` поверх `XInput`
- `src/gameinput.def` — таблица экспортов (`GameInputCreate @1`)
- `build.sh` — скрипт сборки через `x86_64-w64-mingw32-gcc`
- `bin/GameInput.dll` — скомпилированная 64-битная DLL

---

## Лицензия

MIT License — см. [LICENSE](LICENSE).
