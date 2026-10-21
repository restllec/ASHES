<div align="center">

# 🔥 ASHES

**Высокопроизводительный внутренний клиент и фреймворк автоматизации для MTA:SA Province**

[![C++20](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg?style=for-the-badge&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x86-0078D6.svg?style=for-the-badge&logo=windows)](https://microsoft.com/windows)
[![DirectX](https://img.shields.io/badge/Graphics-DirectX%209-orange.svg?style=for-the-badge)](https://en.wikipedia.org/wiki/Direct3D#Direct3D_9)
[![License](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](LICENSE)
[![Telegram](https://img.shields.io/badge/Telegram-ASHES%20Bot-2CA5E0.svg?style=for-the-badge&logo=telegram)](https://t.me/ashes_rst_bot)

</div>

---

## 📌 О проекте

**ASHES** — модульный C++ клиент с открытым исходным кодом, предоставляющий внутриигровой оверлей DirectX 9 (ImGui), динамический мост выполнения Lua-скриптов и комплексную систему сетевой синхронизации и автоматизации для MTA:SA Province.

Проект спроектирован по модульной 3-звенной архитектуре без сторонних динамических зависимостей (все библиотеки линкуются статически).

---

## 🏗 Архитектура системы

Архитектура состоит из трех ключевых модулей:

```
[ Ashes.exe (Standalone Loader) ]
               │
               ▼ (Manual-Map)
     [ AshesAgent.dll ]  ──► Перехват создания дочерних процессов MTA
               │
               ▼ (Manual-Map в gta_sa.exe)
     [ AshesClient.dll ]
         ├── DirectX 9 Overlay (ImGui + Present/Reset Hooks)
         ├── Lua Bridge (Dynamic VM Hook, Resource Injection)
         ├── Network & State Hooks (netc.dll)
         └── PDB Crash Handler (SEH + Minidump)
```

1. **`Ashes.exe` (Loader / Launcher)**:
   * Автономный (portable) исполняемый файл с собственным графическим интерфейсом на DirectX 9 / ImGui.
   * Содержит встроенные payload-библиотеки и ресурсы, автоматически распаковывая их при запуске без создания мусорных файлов.
   * Управляет инжектом, лицензированием и конфигурацией серийных номеров.

2. **`AshesAgent.dll` (Bootstrap Agent)**:
   * Внедряется в приостановленный процесс лаунчера MTA через ручной маппинг памяти (`ManualMap`).
   * Устанавливает хук на создание дочернего процесса (`CreateProcessW`) для перехвата `gta_sa.exe`.

3. **`AshesClient.dll` (Core Runtime Engine)**:
   * Внедряется непосредственно в `gta_sa.exe`.
   * **D3D9 Present/Reset Hooks**: отрисовка плавного меню по нажатию `Shift + R` с поддержкой анимаций и адаптивного масштабирования.
   * **Lua Bridge**: интеграция в виртуальные машины игровых ресурсов (`province_tram`, `province_snowPlow`) с обходом хуков отладки MTA.
   * **Network Protocol**: сетевые хуки в `netc.dll`, спуфинг серийного номера диска, фильтрация служебных пакетов синхронизации.
   * **PDB Crash Handler**: автоматический перехват необработанных исключений с сохранением минидампов и трассировкой стека вызовов до строк кода.

---

## 🚀 Возможности и модули

* **🎮 Внутриигровой оверлей (`Shift + R`)**:
  * Современный темный интерфейс на базе ImGui с кастомной палитрой, плавными карточками и вкладками.
  * Интерактивная телеметрия в реальном времени (координаты, скорость, угол поворота, состояние сети).
  * Управление всеми ботами и модулями в один клик.

* **🤖 Встроенный комплекс автоматизации**:
  * **TramBot** — автономное управление городским трамваем по маршрутам с контролем скорости и стрелок.
  * **JBKBot** — автоматизация рабочего процесса на заводе ЖБК.
  * **PlowBot** — система уборки снега и полива дорожного покрытия.
  * **PilotTelemetry** — полетная телеметрия и навигационные маркеры.
  * **Полив & Табачка** — автоматизация сельскохозяйственных работ.

* **🛡 Безопасность и стабильность**:
  * Асинхронная инициализация модулей (полное отсутствие дедлоков `Loader Lock` и сбоев `Error 1114`).
  * Структурная обработка исключений (`__try / __except`) при перехвате графического конвейера D3D9.
  * Защита от детектов сканеров целостности модулей памяти.

---

## 🛠 Сборка из исходников

### Требования
* **ОС**: Windows 10 / 11 (x64)
* **IDE**: Visual Studio 2022 (v143) с компонентами «Разработка классических приложений на C++»
* **Набор средств сборки**: MSVC v143 (x86), Windows 10/11 SDK
* Все зависимости (`MinHook`, `ImGui`, `ByteRevenant`) уже включены в репозиторий.

### Сборка через Visual Studio
1. Откройте файл решения `DarkFlame.sln`.
2. Выберите конфигурацию **`Release`** и платформу **`x86`**.
3. Нажмите `Сборка` -> `Собрать решение` (`Ctrl + Shift + B`).

### Сборка через командную строку (MSBuild)
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" DarkFlame.sln /p:Configuration=Release /p:Platform=x86 /m /v:minimal
```

Скомпилированные бинарники формируются в каталоге:
```
bin/Release/x86/
├── Ashes.exe
├── AshesAgent.dll
└── AshesClient.dll
```

---

## 📂 Структура проекта

```text
├── Agent/                  # Исходный код AshesAgent.dll (перехватчик процессов)
├── Client/                 # Исходный код AshesClient.dll (ядро, хуки, GUI, Lua Bridge)
│   ├── assets/             # Графические ресурсы, иконки, встроенные Lua-скрипты
│   ├── third_party/        # Сторонние библиотеки (MinHook, ImGui, Lua 5.1 runtime)
│   ├── gui.cpp             # Отрисовка ImGui оверлея и D3D9 хуки
│   ├── lua_bridge.cpp      # Ядро Lua-моста и загрузчик скриптов
│   ├── netc_hooks.cpp      # Сетевые фильтры и спуфер
│   └── dllmain.cpp         # Точка входа DLL и диспетчеризация
├── Loader/                 # Исходный код Ashes.exe (автономный лаунчер)
├── Shared/                 # Общие заголовки протоколов IPC и Crash Handler
├── DarkFlame.sln           # Файл решения Visual Studio
└── README.md               # Документация проекта
```

---

## 💬 Сообщество и поддержка

* Официальный Telegram-бот проекта: **[@ashes_rst_bot](https://t.me/ashes_rst_bot)**
* Получение PRO-доступа, генерация лицензионных ключей и обновления.

---

## ⚖️ Дисклеймер

Проект разработан исключительно в образовательных и исследовательских целях для изучения низкоуровневого программирования на C++, архитектуры DirectX 9 и взаимодействия с виртуальными машинами Lua. Авторы не несут ответственности за любое использование материалов проекта, нарушающее правила сторонних сервисов.

---

<div align="center">
<sub>Designed & Maintained by restllec • 2026</sub>
</div>

