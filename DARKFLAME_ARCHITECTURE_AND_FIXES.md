# Архитектура DarkFlame: Анализ работы модулей и исправления сигнатур

Документ описывает техническое устройство проекта DarkFlame (клиентской части под MTA:SA / Province), механизмы перехвата модулей, сетевой защиты, моста взаимодействия с Lua-виртуальными машинами ресурсов, а также подробный разбор того, почему проект перестал работать и как именно были исправлены сигнатуры в замененных файлах.

---

## 1. Общая архитектура решения

Проект построен как многоуровневая система внедрения и мониторинга:

```
[MTA Launcher]
      │
      ▼ (LdrLoadDll / CreateProcess hook)
[DarkFlameAgent.dll] (перехват дочернего процесса)
      │
      ▼ (Manual Map в gta_sa.exe)
[DarkFlameClient.dll]
   ├── LdrLoadDll Hook (перехват загрузки client.dll и netc.dll)
   ├── Netc Hooks (обход античита, глушение сигналов кика и проверок целостности)
   ├── Privacy / Forensic Hooks (спуфинг железа, адаптеров, защита от обнаружения)
   ├── DirectX 9 / ImGui GUI (меню по Shift+R, телеметрия, логирование)
   └── Lua Bridge & Embedded Runtime
         ├── Signature Scanner (динамический поиск функций ядра MTA в памяти)
         ├── Private Lua 5.1 VM (изолированный рантайм для компиляции и вызовов)
         ├── MinHook (перехват CallHook, IsNameAllowed, triggerServerEvent)
         └── Injection Manager (поиск целевых ресурсов и внедрение управляющих потоков)
```

### Ключевые компоненты:
1. **`DarkFlameClient.dll` / `dllmain.cpp`**:
   - Перехватывает нативный `ntdll!LdrLoadDll` и `ntdll!LdrUnloadDll` до инициализации игровых модулей.
   - Как только игра загружает `netc.dll`, мгновенно устанавливает сетевые хуки (`InstallNetcHooks`), блокируя античит до возврата из загрузчика библиотек.
   - Как только загружается `client.dll`, запускает фоновый сканирующий поток `StartLuaArgsHook` (`lua_args_hook.cpp`), который циклически пытается обнаружить сигнатуры ядра и инициализировать Lua-мост (`InstallLuaBridge`).
2. **`gui.cpp`**:
   - Хукает Direct3D9 (`IDirect3D9::CreateDevice`, `EndScene` / `Present`).
   - Каждый кадр в функции `RenderFrame()` вызывает пульс моста — `PulseLuaBridge()`.

---

## 2. Сетевая подсистема и обход античита (`netc_hooks.cpp`, `netc_signatures.h`)

Античит MTA Province (`netc.dll`) использует ряд функций для мониторинга аномалий, проверки целостности памяти и отправки пакетов кика на сервер:

* **Функции кика**:
  * `ExecuteSecurityViolationKick` — принудительный разрыв соединения при детекте нарушений.
  * `SendClientKick`, `SetClientKick`, `SetClientKickNew` — отправка сформированного пакета кика.
* **Функции проверок целостности и сканеры**:
  * `VfB00Z00Scanner` и `NetworkPacketMonitoringSystem` — сканирование памяти процесса и пакетов на сигнатуры читов/ботов.
  * `ScanModuleIntegrity` (`HandleSelfFileIntegrityResultPacket`) — сверка контрольных сумм модулей.
  * `SendLogger` / `SendLoggerToServerMtaDev` — телеметрия подозрительных событий разработчикам.

### Механизм защиты:
В `netc_hooks.cpp` адреса этих процедур определяются по сигнатурам из `netc_signatures.h`, после чего на них ставятся JMP/RET-заглушки или detours через MinHook. Попытки отправить лог или сформировать пакет нарушения глушатся, не позволяя серверному античиту зафиксировать вмешательство.

---

## 3. Устройство и принцип работы Lua Bridge (`lua_bridge.cpp`)

Lua Bridge — ключевое звено, позволяющее внедрять управляющие скрипты ботов прямо в контекст игровых Lua-ресурсов GTA San Andreas (MTA).

### 3.1. Обнаружение структур MTA в памяти
MTA компилирует свой сетевой клиент с открытыми исходниками, но с модификациями и обфускацией. Для работы моста требуются:
1. **`lua_newthread`**: Стандартная C-функция Lua для создания дочерней сопрограммы (`lua_State`).
2. **`CLuaManager::GetVirtualMachine`**: Метод менеджера ресурсов MTA, получающий указатель на `CLuaMain` по переданному `lua_State`.
3. **`CLuaDefs::m_pLuaManager`**: Глобальный указатель на менеджер Lua-ресурсов.
4. **Таблица зарегистрированных C-функций (`CLuaCFunctions`)**:
   - MTA регистрирует свои API (`triggerServerEvent`, `addEvent`, `triggerEvent`, `addEventHandler`, `removeEventHandler`) во внутреннем красно-черном дереве (`std::map`).
   - Функция `FindRegisteredLuaFunction()` находит корень дерева по опкоду `mov ecx, <address>` (`0xB9`) в функции `CLuaCFunctions::GetFunction` и обходит дерево узлов (`LuaFunctionNode`), извлекая реальные адреса функций по строковым именам.

### 3.2. Внедрение скриптов (`InjectIntoResource`)
Для запуска бота в нужном контексте (например, `province_tram`):
1. `FindResourceState("province_tram", target)`: сканирует связанный список всех активных VM (`LuaMains`), находя узел, чье имя ресурса совпадает с целевым.
2. `lua_newthread(target.state)`: создает новый `lua_State` внутри оригинальной VM ресурса. Это гарантирует, что созданный поток имеет доступ ко всем глобальным переменным и элементам оригинального скрипта (светофоры, чекпоинты, координаты маршрута).
3. `DarkFlameLuaInstallPrivateGlobals(thread)`: изолирует окружение созданного потока от случайных детектов.
4. `ManagedChunk`: скрипт оборачивается в защитную метатаблицу с собственным табличным окружением `_G`, перехватывающим создание таймеров (`setTimer`), обработчиков событий и биндов, чтобы при выгрузке (`CleanupChunk`) всё корректно очищалось без утечек памяти.
5. `DarkFlameLoadLuaSource`: парсит и исполняет байткод встроенным независимым парсером DarkFlame.

### 3.3. Сокрытие вызовов (Stealth & Anti-Detection)
MTA позволяет серверным скриптам устанавливать хуки отладки (`addDebugHook`), которые ловят вызовы функций и генерацию событий:
* Мост ставит перехваты на `CDebugHookManager::CallHook` и `CDebugHookManager::IsNameAllowed`.
* Когда бот вызывает `triggerServerEvent` или внутренние методы, взводятся флаги `g_hideCalls` или счетчик `g_scopeDepth` (`TrapScope`).
* Перехваченный `HookCallHook` проверяет `HiddenActive()`: если вызов инициирован ботом, оригинальный `CallHook` не вызывается (возвращает `1`), и сторонние защитные скрипты сервера/клиента даже не получают уведомления о произошедшем событии.

---

## 4. Разбор проблемы: Почему проект перестал работать

В репозитории проект перестал запускаться из-за грубых ошибок в сигнатурах и логике инициализации моста в `lua_bridge_signatures.h` и `lua_bridge.cpp`.

### 4.1. Хрупкая и неверная сигнатура `GetVirtualMachinePattern`
* **Что было на диске**:
  ```cpp
  // GetVirtualMachine завиртуализирована; сигнатура указывает на её 5-байтовый jmp-переходник (ILT).
  inline constexpr std::string_view GetVirtualMachinePattern = "E9 20 E8 55 00";
  ```
* **Почему это сломалось**:
  Паттерн `"E9 20 E8 55 00"` — это жестко закодированная инструкция `jmp` с фиксированным относительным смещением (`0x0055E820`). Любой патч MTA, смена базового адреса DLL, ASLR или повторная сборка `client.dll` полностью меняют это смещение. Сигнатурный сканер не мог найти этот паттерн, и `getVirtualMachine` возвращал `nullptr`.

### 4.2. Жесткая зависимость от виртуализированных функций VMProtect
* **Что было на диске**:
  ```cpp
  inline constexpr std::string_view AddDebugHookPattern = "9C E8 ? ? ? ? B8 AC B4 15 68";
  inline constexpr std::string_view RemoveDebugHookPattern = "E8 ? ? ? ? 01 94 14 42 55 52 9F";
  ```
  И в `InstallLuaBridge`:
  ```cpp
  if(!onPreFunction || !callHook || !newThread
      || !getLuaFunction || !getVirtualMachine || !luaManagerLoad
      || !addDebugHook || !removeDebugHook || !luaMToRef
      || !luaFunctionRefDtor || !clientGameAccess)
  {
      Log::Write(L"[lua-bridge] signature resolution failed");
      return false; // Полный отказ в запуске!
  }
  ```
* **Почему это сломалось**:
  Паттерны `9C E8...` (pushfd, call...) принадлежали виртуализированным стабам протектора. При компиляции новых версий игры VMProtect генерирует новые мутировавшие последовательности команд. Сигнатуры возвращали `not_found`, а условие в `InstallLuaBridge` трактовало отсутствие `addDebugHook` / `removeDebugHook` / `onPreFunction` как **фатальную ошибку**, из-за чего мост вообще никогда не выходил в состояние `g_ready = true`.

### 4.3. Ошибочный оффсет `OnPreFunctionCallOffset`
В версии на диске пытались резолвить функцию `OnPreFunction` через фиксированный оффсет 16 от сайта доступа к `g_pClientGame`:
```cpp
const std::int32_t relative = *reinterpret_cast<const std::int32_t*>(
    clientGameAccess + OnPreFunctionCallOffset);
onPreFunction = clientGameAccess + OnPreFunctionCallOffset + 4 + relative;
```
Любой сдвиг инструкций приводил к чтению мусора вместо адреса функции.

---

## 5. Как это починили в замененных файлах

В прикрепленных файлах архитектура сигнатур и проверок была полностью переработана в сторону максимальной надежности и отказоустойчивости.

### 5.1. Динамический расчет адреса `GetVirtualMachine` через якорный паттерн
Вместо поиска нестабильного jmp, в `lua_bridge_signatures.h` и `lua_bridge.cpp` применили якорный поиск устойчивой функции-вызывателя:
```cpp
inline constexpr std::string_view GetVirtualMachinePattern =
    "55 8B EC 56 57 8B 7D 0C 85 FF 74 5A 80 BF C9 00 00 00 00 74 0F 5F 5E "
    "C7 45 0C 00 00 00 00 5D E9 ? ? ? ? 8B 77 60 83 FE FF 74 3A A1 ? ? ? ? "
    "53 FF 75 08 33 DB 8B 88 10 01 00 00 E8 ? ? ? ?";
```
А затем в коде динамически вычисляется адрес вызова по смещению опкода `call` (`+0x3D`):
```cpp
const std::uintptr_t callSite = getVirtualMachineAnchor + CallSiteOffset;
std::int32_t relative{};
if(ReadValue(reinterpret_cast<const void*>(callSite + 1), 0, relative) && relative)
{
    getVirtualMachine = callSite + 5 + relative;
}
```
Этот расчет корректно работает независимо от релокаций и обновлений билда.

### 5.2. Смягчение условий проверки и опциональность хуков
В исправленном `InstallLuaBridge` функции, которые подвержены обфускации или могут отсутствовать, больше **НЕ** являются блокерами:
```cpp
if(!callHook || !newThread
    || !getLuaFunction || !getVirtualMachine || !luaManagerLoad
    || !luaMToRef
    || !luaFunctionRefDtor
    || !clientGameAccess)
{
    Log::Write(L"[lua-bridge] signature resolution failed");
    return false;
}
if(!isNameAllowed)
    Log::Write(L"[lua-bridge] IsNameAllowed not found; hide filter uses the CallHook bypass");
if(!addDebugHook || !removeDebugHook)
    Log::Write(L"[lua-bridge] native debug-hook API not found; dfAddDebugHook is disabled");
```
* Если `IsNameAllowed` не найден, система автоматически переключается на сокрытие через `CallHook bypass`.
* Если нативные `addDebugHook` / `removeDebugHook` не найдены, мост просто отключает алиас `dfAddDebugHook`, но **все остальные функции, инъекции и боты продолжают стабильно работать**.

### 5.3. Возврат надежного паттерна доступа к `g_pClientGame`
Вместо жестко закодированного смещения `0xD8`:
```cpp
inline constexpr std::string_view ClientGameDebugHookAccessPattern =
    "8B 0D ? ? ? ? 83 C4 04 FF 75 ? 8B 89 ? ? ? ? FF 75 ? E8 ? ? ? ?";
```
Смещение поля менеджера хуков извлекается динамически из инструкции `mov ecx, [ecx + offset]`:
```cpp
g_clientGameSlot = *reinterpret_cast<void***>(clientGameAccess + 2);
g_debugHookManagerOffset = *reinterpret_cast<const std::uint32_t*>(clientGameAccess + 14);
```

### 5.4. Восстановление сигнатур в `netc_signatures.h`
Восстановлены строгие маски для процедур сканирования:
* `VfB00Z00Scanner` и `NetworkPacketMonitoringSystem` очищены от избыточных `?` wildcard'ов, которые приводили к ложным срабатываниям на похожих функциях пролога в новых версиях компилятора MSVC.

Благодаря этим правкам `DarkFlameClient.dll` успешно находит все критически важные функции, устанавливает хуки и активирует мост.
