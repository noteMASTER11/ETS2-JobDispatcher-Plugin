# Повторные измерения после исправлений

Инструменты и их зафиксированные версии совпадают с
[базовым измерением](../ub-mode-2026-10-28/README.md).

- `summary.json` — область runtime, функции CRAP, покрытие и мутации.
- `source-manifest.json` — SHA-256 проверяемого рабочего дерева.
- `coverage.json`, `coverage-run.txt` — реальные Python-проверки.
- `crapper-runtime.txt`, `crap.edn` — 23 функции поставляемых подготовщиков.
- `dryer-runtime.txt`, `dry.edn` — порог 0.8.
- `mutator-runtime.txt`, `tools.offline-bootstrap.edn` — результаты мутаций.
- `cpp-complexity.json` — отдельный Lizard CCN; не CRAP DLL.

`reproduce/` содержит runner-скрипты. Скопируйте их в корень отдельного снимка
`src/`, `tools/`, `tests/`, а в `runtime/` положите проверенный
`converter_pix.exe` из локальной среды сборки. В тесте заменён процесс
извлечения; конвертер нужен для проверки его подлинной контрольной суммы.
Игровые ресурсы не требуются и не включены в этот каталог.

```powershell
$env:TEMP='D:\ChatGPT\Temp'
$env:TMP=$env:TEMP
$env:PYTHONDONTWRITEBYTECODE='1'
$python='D:\ChatGPT\Tools\ub-mode\.venv\Scripts\python.exe'
& $python run_coverage.py
& $python run_quality.py crapper --root . --use-existing-coverage tools/offline-bootstrap.py tools/prepare-game-assets.py tools/prepare-locales.py
& $python run_quality.py dryer --root . --threshold 0.8 tools/offline-bootstrap.py tools/prepare-game-assets.py tools/prepare-locales.py
& $python run_quality.py mutator --root . --use-existing-coverage --test-command 'D:\ChatGPT\Tools\ub-mode\.venv\Scripts\python.exe mutation_runner.py' --max-workers 4 tools/offline-bootstrap.py
```

Runner запускает оба актуальных набора assertions с корнем mutation worker,
проверяя путь импортированного production-модуля. Связанный `tests/` не должен
переключать импорт на немутированный исходник.

Mutation exit 3 означает наличие трёх оставшихся эквивалентных для текущего
контракта мутаций, а не полностью пройденный порог 100%.
