#!/bin/bash

# --- Configuración ---
SCRIPT_LOCATION_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
PROJECT_ROOT_DIR="$(dirname "$SCRIPT_LOCATION_DIR")"
CERT_DIR="${PROJECT_ROOT_DIR}/certs"
CONFIG_FILE="${PROJECT_ROOT_DIR}/config.yaml"
OUTPUT_BASE_DIR="${PROJECT_ROOT_DIR}/output_fabrica"
PAI_CERT_PEM="${CERT_DIR}/PAI.crt"
PAI_KEY_PEM="${CERT_DIR}/PAI.key" 
CD_FILE="${CERT_DIR}/cd.der"
PYTHON_INTERPRETER="/opt/espressif/tools/python_env/idf5.4_py3.12_env/bin/python"
ESPTOOL_CMD="esptool.py"
CHIP_CERT_CMD="chip-cert"
MFG_TOOL_CMD="esp-matter-mfg-tool"
PYTHON_SCRIPT_PATH="${SCRIPT_LOCATION_DIR}/generate_creds_by_mac.py"

FACTORY_PARTITION_ADDR="0xFFA000"
MANUFACTURING_DATE_RAW=$(date +%Y%m%d)
MANUFACTURING_DATE_DISPLAY="${MANUFACTURING_DATE_RAW:0:4}-${MANUFACTURING_DATE_RAW:4:2}-${MANUFACTURING_DATE_RAW:6:2}"

# --- Funciones ---
check_file() {
  if [ ! -f "$1" ]; then echo "Error: Falta $1"; return 1; fi
  return 0
}
check_command() {
  if ! type -p "$1" > /dev/null 2>&1; then echo "Error: Comando '$1' no encontrado."; return 1; fi
  return 0
}
# Función para leer valores del YAML usando Python
get_yaml_value() {
  local key=$1
  local config_file=$2
  "$PYTHON_INTERPRETER" -c "import yaml; f = open('$config_file', 'r'); config = yaml.safe_load(f); keys = '$key'.split('.'); val = config; [val := val.get(k) for k in keys]; print(val if val is not None else '')"
}

normalize_to_hex() {
  local value="${1,,}"
  value="${value// /}"
  if [[ "$value" == 0x* ]]; then
    echo "${value#0x}"
  elif [[ "$value" =~ ^[0-9]+$ ]]; then
    printf '%x' "$((10#$value))"
  else
    echo "$value"
  fi
}

# --- Verificaciones Iniciales ---
echo "--- Verificando herramientas y archivos base ---"
check_command "$ESPTOOL_CMD" || exit 1
check_command "$CHIP_CERT_CMD" || exit 1
check_command "$MFG_TOOL_CMD" || exit 1
check_file "$CONFIG_FILE" || exit 1
check_file "$PYTHON_SCRIPT_PATH" || exit 1
check_file "$PAI_CERT_PEM" || exit 1
check_file "$PAI_KEY_PEM" || exit 1 
if [ ! -x "$PYTHON_INTERPRETER" ]; then echo "Error: Intérprete Python no encontrado: $PYTHON_INTERPRETER"; exit 1; fi
echo "Verificaciones iniciales OK."

# --- Cargar configuración desde YAML ---
echo "--- Cargando configuración desde $CONFIG_FILE ---"
if ! "$PYTHON_INTERPRETER" -c "import yaml" > /dev/null 2>&1; then
  echo "Error: La librería 'PyYAML' no está instalada en el entorno de Python ($PYTHON_INTERPRETER)."
  echo "Por favor, instálala con: $PYTHON_INTERPRETER -m pip install pyyaml"
  exit 1
fi
DEVICE_PORT_DEFAULT=$(get_yaml_value "fabrication.port" "$CONFIG_FILE")
CHIP_TARGET=$(get_yaml_value "fabrication.chip_target" "$CONFIG_FILE")
VENDOR_ID=$(get_yaml_value "fabrication.vendor_id" "$CONFIG_FILE")
VENDOR_NAME=$(get_yaml_value "fabrication.vendor_name" "$CONFIG_FILE")
PRODUCT_ID=$(get_yaml_value "fabrication.product_id" "$CONFIG_FILE")
PRODUCT_NAME=$(get_yaml_value "fabrication.product_name" "$CONFIG_FILE")
DEVICE_TYPE_ID=$(get_yaml_value "fabrication.device_type_id" "$CONFIG_FILE")
HARDWARE_VERSION=$(get_yaml_value "fabrication.hardware_version" "$CONFIG_FILE")

VENDOR_ID_HEX=$(normalize_to_hex "$VENDOR_ID")
PRODUCT_ID_HEX=$(normalize_to_hex "$PRODUCT_ID")

# --- Generar CD si no existe ---
if [ ! -f "$CD_FILE" ]; then
  echo "--- Generando Declaración de Certificación (CD) de prueba ---"
  $CHIP_CERT_CMD gen-cd \
    --key /opt/espressif/esp-matter/connectedhomeip/connectedhomeip/credentials/test/certification-declaration/Chip-Test-CD-Signing-Key.pem \
    --cert /opt/espressif/esp-matter/connectedhomeip/connectedhomeip/credentials/test/certification-declaration/Chip-Test-CD-Signing-Cert.pem \
    --out "$CD_FILE" \
    --format-version 1 --vendor-id $VENDOR_ID --product-id $PRODUCT_ID \
    --device-type-id $DEVICE_TYPE_ID --certificate-id "ZIG20142ZB330003-24" \
    --security-level 0 --security-info 0 --version-number 1 --certification-type 1
  if [ $? -ne 0 ]; then echo "Error generando CD."; exit 1; fi
  echo "CD generado: $CD_FILE"
else
  echo "--- Usando CD existente: $CD_FILE ---"
fi
check_file "$CD_FILE" || exit 1

# --- Procesar argumentos de línea de comandos ---
# Usar el puerto del YAML por defecto, pero permitir sobreescribirlo con -p
DEVICE_PORT="$DEVICE_PORT_DEFAULT"
if [ "$1" == "-p" ] && [ -n "$2" ]; then DEVICE_PORT="$2"; fi

# --- Obtener credenciales únicas del dispositivo ---
echo "--- Obteniendo credenciales únicas del dispositivo en $DEVICE_PORT ---"
DEVICE_MAC=$($PYTHON_INTERPRETER "$PYTHON_SCRIPT_PATH" -p $DEVICE_PORT --get mac)
if [ $? -ne 0 ] || [ -z "$DEVICE_MAC" ]; then echo "Error obteniendo MAC."; exit 1; fi
DEVICE_PASSCODE=$($PYTHON_INTERPRETER "$PYTHON_SCRIPT_PATH" -p $DEVICE_PORT --get passcode)
if [ $? -ne 0 ] || [ -z "$DEVICE_PASSCODE" ]; then echo "Error obteniendo Passcode."; exit 1; fi
DEVICE_DISCRIMINATOR=$($PYTHON_INTERPRETER "$PYTHON_SCRIPT_PATH" -p $DEVICE_PORT --get discriminator)
if [ $? -ne 0 ] || [ -z "$DEVICE_DISCRIMINATOR" ]; then echo "Error obteniendo Discriminator."; exit 1; fi

SERIAL_SUFFIX=$(echo "$DEVICE_MAC" | tr -d ':')
DEVICE_SERIAL="$SERIAL_SUFFIX"
OUTPUT_DIR="${OUTPUT_BASE_DIR}/${DEVICE_SERIAL}"
echo "-------------------------------------------"
echo "Dispositivo MAC: $DEVICE_MAC"
echo "Serial #:        $DEVICE_SERIAL"
echo "Chip Target:     $CHIP_TARGET"
echo "Passcode:        $DEVICE_PASSCODE"
echo "Discriminator:   $DEVICE_DISCRIMINATOR"
echo "Fecha Fabr.:     $MANUFACTURING_DATE_DISPLAY (CLI: $MANUFACTURING_DATE_RAW)"
echo "Directorio Salida: $OUTPUT_DIR"
echo "-------------------------------------------"
mkdir -p "$OUTPUT_DIR"

# --- Ejecutar esp-matter-mfg-tool (Modo Generación DAC Interna) ---
echo "--- Ejecutando esp-matter-mfg-tool (Generando DAC internamente) ---"
# Definir la ruta esperada del binario de salida para flasheo posterior
MFG_BIN_SEARCH_PATH="${OUTPUT_DIR}/${VENDOR_ID_HEX}_${PRODUCT_ID_HEX}"

$MFG_TOOL_CMD \
    --passcode "$DEVICE_PASSCODE" \
    --discriminator "$DEVICE_DISCRIMINATOR" \
    --serial-num "$DEVICE_SERIAL" \
    --vendor-id "$VENDOR_ID" \
    --product-id "$PRODUCT_ID" \
    --vendor-name "$VENDOR_NAME" \
    --product-name "$PRODUCT_NAME" \
    --hw-ver "$HARDWARE_VERSION" \
    --mfg-date "$MANUFACTURING_DATE_RAW" \
    -cd "$CD_FILE" \
    --pai \
    -c "$PAI_CERT_PEM" \
    -k "$PAI_KEY_PEM" \
    --cn-prefix "Matter Test DAC" \
    --lifetime 3650 \
    --outdir "$OUTPUT_DIR" \
    --target "$CHIP_TARGET"

# Verificar resultado de mfg_tool
if [ $? -eq 0 ]; then
    echo "¡Éxito preliminar! esp-matter-mfg-tool terminó."

    # --- ENCONTRAR EL ARCHIVO .bin REAL ---
    echo "--- Buscando archivo de partición .bin generado ---"
    MFG_BIN_FILE=$(find "$MFG_BIN_SEARCH_PATH" -maxdepth 2 -name '*-partition.bin' -print -quit)

    if [ -z "$MFG_BIN_FILE" ]; then
      echo "Error: No se pudo encontrar el archivo *-partition.bin generado en $MFG_BIN_SEARCH_PATH"
      # Intentar buscar en el directorio de salida principal como fallback
      MFG_BIN_FILE=$(find "$OUTPUT_DIR" -maxdepth 2 -name '*-partition.bin' -print -quit)
      if [ -z "$MFG_BIN_FILE" ]; then
         echo "Error: No se pudo encontrar el archivo *-partition.bin generado en $OUTPUT_DIR tampoco."
         exit 1
      fi
    fi
    echo "Archivo .bin encontrado: $MFG_BIN_FILE"
    # ------------------------------------

    check_file "$MFG_BIN_FILE" || exit 1
    echo "Contenido generado:"
    echo "  - Passcode:        $DEVICE_PASSCODE"
    echo "  - Discriminator:   $DEVICE_DISCRIMINATOR"
    echo "  - Serial:          $DEVICE_SERIAL"
    echo "  - DAC(Generado)/Key(Privada)/CD/PAI: Basados en prueba"
    echo "IMPORTANTE: Usa el código QR o manual de $OUTPUT_DIR/onboarding_codes.csv para comisionar."

    # --- Flasheo Automático ---
    echo "--- Flasheando partición de fábrica encontrada ---"
    $ESPTOOL_CMD -p "$DEVICE_PORT" write_flash "$FACTORY_PARTITION_ADDR" "$MFG_BIN_FILE"
    if [ $? -eq 0 ]; then
      echo "¡Partición de fábrica flasheada exitosamente en $FACTORY_PARTITION_ADDR!"
    else
      echo "Error: falló el flasheo de la partición de fábrica."
      exit 1
    fi
    # ------------------------

else
    echo "Error: falló la ejecución de esp-matter-mfg-tool."
    exit 1
fi

exit 0
