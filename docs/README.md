# Documentación del proyecto ESP32 C6 YAML

## Introducción

Este repositorio contiene un ejemplo de firmware Matter para dispositivos de iluminación basado en el ESP32-C6. El proyecto parte del ejemplo **Color Temperature Light** del SDK de Espressif y está orientado a crear un dispositivo conectado capaz de exponerse dentro de un ecosistema Matter, controlando la temperatura de color y el brillo del actuador.

El código está organizado como un proyecto estándar de ESP-IDF con componentes adicionales de ESP-Matter. Toda la personalización específica del dispositivo se realiza mediante archivos de configuración YAML y módulos C++ incluidos en `main/`.

## Requisitos previos

- **Hardware**: placa de desarrollo compatible con ESP32-C6 con capacidad de conexión USB.
- **Software**:
  - ESP-IDF instalado y configurado (recomendado v5.x o posterior).
  - SDK de ESP-Matter y toolchain de Xtensa/RISC-V según la guía oficial.
  - Python 3.8 o superior con `pip` para instalar utilidades complementarias.
- **Herramientas opcionales**:
  - Docker si se desea reproducir el entorno de compilación proporcionado.
  - `esp_matter_mfg_tool` para generar credenciales de fábrica.

## Estructura del proyecto

```
.
├── CMakeLists.txt          # Punto de entrada del sistema de build
├── config.yaml             # Configuración principal del dispositivo Matter
├── config.yaml.switch      # Ejemplo alternativo de configuración
├── main/                   # Código fuente de la aplicación
│   ├── app_main.cpp        # Lógica principal del dispositivo
│   ├── device_modules/     # Módulos específicos de tipos de dispositivos
│   └── ...
├── partitions.csv          # Tabla de particiones flash personalizada
├── sdkconfig.defaults      # Configuraciones base de ESP-IDF
├── tools/                  # Scripts auxiliares para generación de credenciales y flasheo
└── docs/                   # Documentación del proyecto
```

## Configuración inicial

1. **Exportar variables de entorno** del ESP-IDF y ESP-Matter:
   ```bash
   source $IDF_PATH/export.sh
   source $ESP_MATTER_PATH/export.sh
   ```
2. **Seleccionar la configuración YAML** adecuada. Por defecto el build usa `config.yaml`. Para utilizar otra variante, copia o renombra el archivo deseado a `config.yaml` antes de compilar.
3. **Ajustar parámetros en `sdkconfig.defaults`** si necesitas modificar opciones del framework antes de ejecutar `idf.py menuconfig`.
4. **Revisar la tabla de particiones** en `partitions.csv` si el dispositivo requiere memoria adicional o diferentes offsets.

## Compilación y flasheo

1. Conecta la placa ESP32-C6 por USB y verifica el puerto asignado (`/dev/ttyACM0`, `/dev/ttyUSB0`, etc.).
2. Desde la raíz del proyecto ejecuta:
   ```bash
   idf.py set-target esp32c6
   idf.py build
   idf.py -p /dev/ttyACM0 flash monitor
   ```
   Ajusta el puerto serie según corresponda.
3. Para limpiar artefactos previos:
   ```bash
   idf.py fullclean
   ```

### Generación de credenciales Matter

El proyecto incluye comandos de apoyo (ver `README.md` original) para generar credenciales dinámicas con `esp_matter_mfg_tool`. Ejemplo:
```bash
pip install esp_matter_mfg_tool
python tools/generate_creds_auto_mac.py --port /dev/ttyACM0 --get mac
python tools/generate_creds_auto_mac.py --port /dev/ttyACM0 --get passcode
python tools/generate_creds_auto_mac.py --port /dev/ttyACM0 --get discriminator
```
Estas credenciales se cargan en el dispositivo durante el flasheo para permitir la puesta en servicio Matter.

## Personalización del dispositivo

- **Configuración YAML**: define endpoints, clusters y atributos expuestos por el dispositivo. Modificar este archivo permite cambiar el tipo de luminaria o añadir sensores.
- **Módulos C++** (`main/device_modules/`): contienen la lógica específica de cada tipo de dispositivo. Puedes extender o crear nuevos módulos para admitir funcionalidades personalizadas.
- **Macros comunes** (`main/common_macros.h`): agrupan constantes reutilizables, ayudando a mantener coherencia entre módulos.

## Solución de problemas

- Si el build falla por dependencias faltantes, verifica que `IDF_PATH` y `ESP_MATTER_PATH` estén correctamente exportados.
- Para errores de conexión serie, comprueba permisos sobre el puerto y que no haya otro programa usándolo.
- Al cambiar la tabla de particiones, ejecuta `idf.py fullclean` para regenerar el build con la nueva distribución de memoria.

## Recursos adicionales

- [Documentación oficial de ESP-Matter](https://docs.espressif.com/projects/esp-matter/en/latest/)
- [Guía de ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/)
- [Especificación Matter](https://csa-iot.org/all-solutions/matter/)

## Licencia

El uso del código se rige por la licencia incluida en el repositorio original de Espressif. Consulta los encabezados de los archivos fuente para más información.
