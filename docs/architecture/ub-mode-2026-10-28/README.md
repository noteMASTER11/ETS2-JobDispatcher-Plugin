# Исходная точка аудита

Измерения сделаны на снимке текущего рабочего дерева. Его состав и хеши — в
`source-manifest.json`; версии инструментов — в `tool-versions.txt`.

## Файлы

- `crapper-python.txt`, `crap.edn`: настоящий Crapper с LCOV существующих тестов.
- `crapper-cpp.txt`: пустая область анализа C++; не успешный gate.
- `dryer-python.txt`, `dry.edn`: настоящий Dryer, threshold 0.8.
- `mutator-python.txt`, `tools.offline-bootstrap.edn`: выполненные мутации.
- `mutator-scan.txt`: все 68 обнаруженных мест мутаций bootstrap.
- `coverage.json`, `coverage-run.txt`: Python coverage; не покрытие DLL.
- `cpp-complexity.json`: Lizard по собственным C++-источникам и заголовкам.
- `homedir-probe.json`: воспроизведение ошибки пути, 0 вместо 18 модов.
- `summary.json`: компактная сводка, с явной областью каждого измерения.
- `ctest-inventory.json`: команды и набор имеющихся CTest-тестов.

## Воспроизведение

Создать новую папку снимка под `D:\ChatGPT\Temp`, скопировать в неё `src`,
`tools`, `tests` из проверяемого состояния, а также скрипты из `reproduce`.
Все команды ниже выполнять из этой папки. Python и инструменты установлены
в `D:\ChatGPT\Tools\ub-mode\.venv`; кэш грамматик задаётся run_quality.py.

```powershell
$env:TEMP='D:\ChatGPT\Temp'
$env:TMP=$env:TEMP
$env:PYTHONDONTWRITEBYTECODE='1'
$python='D:\ChatGPT\Tools\ub-mode\.venv\Scripts\python.exe'
& $python run_coverage.py
& $python run_quality.py crapper --root . --use-existing-coverage tools
& $python run_quality.py dryer --root . --threshold 0.8 tools
& $python run_quality.py mutator --root . --scan tools/offline-bootstrap.py
& $python run_quality.py mutator --root . --use-existing-coverage --test-command 'D:\ChatGPT\Tools\ub-mode\.venv\Scripts\python.exe mutation_runner.py' --max-workers 4 tools/offline-bootstrap.py
```

Существующий тест определяет корень через `__file__.resolve()`. Mutator
связывает tests/ с оригиналом, поэтому mutation_runner.py предоставляет
виртуальное расположение теста в рабочем каталоге. Код assertions остаётся
исходным; дополнительно проверяется, что импортирован именно worker-исходник.

`probe_homedir.py` использует путь локального тестового профиля этой машины;
для повторения на другой машине заменить этот путь. Он читает журнал и архивы,
но не запускает игру и не изменяет сохранения.

Для C++ использован Python API Lizard 1.24.3: `analyze_file` для каждого
`src/*.cpp` и `src/*.hpp`, поля `function_list`, `cyclomatic_complexity`,
`start_line`, `end_line`, `nloc`. Тесты и vendor исключены. Это самостоятельное
измерение сложности; Crapper не умеет разбирать эти файлы.
