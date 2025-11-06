# Contributing to yml2esp

## Coding Style
- Firmware: C/C++17
- Python tooling: Python 3.10+
- No comments in generated firmware code
- English only

## Workflow
1. Edit YAML → `yml2esp render` → generates firmware in `out/main`
2. Build and flash with `idf.py`
3. All new device types must extend:
   - `schema.json`
   - templates in `yml2esp/templates/`

## Pull Requests
- Include minimal example YAML
- Include generated output
