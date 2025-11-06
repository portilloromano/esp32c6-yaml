# yml2esp

Generador declarativo de código para ESP-IDF/ESP-Matter a partir de YAML.

## Requisitos
- Python 3.10+
- ESP-IDF y ESP-Matter exportados en el entorno (`IDF_PATH`, `ESP_MATTER_PATH`)
- `pip install -e .`

## Uso
```
pip install -e .
yml2esp validate examples/light.yaml
yml2esp render examples/light.yaml --out ./out
yml2esp build --project ./out
yml2esp flash --project ./out --port /dev/ttyACM0
```

## Pipeline
validate → render → build → flash
