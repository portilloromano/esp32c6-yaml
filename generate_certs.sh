#!/bin/bash

# --- Configuración ---
BASE_DIR=$(pwd)
CERT_DIR="${BASE_DIR}/certs"
SCRIPT_DIR="${BASE_DIR}"
OUTPUT_BASE_DIR="${BASE_DIR}/output_fabrica"
DEVICE_PORT="/dev/ttyACM0"
PAI_CERT_PEM="${CERT_DIR}/PAI.crt"
PAI_KEY_PEM="${CERT_DIR}/PAI.key" 
CD_FILE="${CERT_DIR}/cd.der"
PYTHON_INTERPRETER="/opt/espressif/tools/python_env/idf5.4_py3.12_env/bin/python"
ESPTOOL_CMD="esptool.py"
CHIP_CERT_CMD="chip-cert"
MFG_TOOL_CMD="esp-matter-mfg-tool"
PYTHON_SCRIPT_NAME="generate_creds_by_mac.py"

FACTORY_PARTITION_ADDR="0xFFA000"
CHIP_TARGET="esp32c6"

VENDOR_ID="0xFFF1"
VENDOR_NAME="Home"
PRODUCT_ID="0x8000"
PRODUCT_NAME="Color Temperature Light"
DEVICE_TYPE_ID="0x010D"

# --- Funciones ---
check_file() {
  if [ ! -f "$1" ]; then echo "Error: Falta $1"; return 1; fi
  return 0
}
check_command() {
  if ! type -p "$1" > /dev/null 2>&1; then echo "Error: Comando '$1' no encontrado."; return 1; fi
  return 0
}

# --- Verificaciones Iniciales ---
echo "--- Verificando herramientas y archivos base ---"
check_command "$ESPTOOL_CMD" || exit 1
check_command "$CHIP_CERT_CMD" || exit 1
check_command "$MFG_TOOL_CMD" || exit 1
check_file "${SCRIPT_DIR}/${PYTHON_SCRIPT_NAME}" || exit 1
check_file "$PAI_CERT_PEM" || exit 1
check_file "$PAI_KEY_PEM" || exit 1 
if [ ! -x "$PYTHON_INTERPRETER" ]; then echo "Error: Intérprete Python no encontrado: $PYTHON_INTERPRETER"; exit 1; fi
echo "Verificaciones iniciales OK."


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

# --- Obtener credenciales únicas del dispositivo ---
echo "--- Obteniendo credenciales únicas del dispositivo en $DEVICE_PORT ---"
DEVICE_MAC=$($PYTHON_INTERPRETER "${SCRIPT_DIR}/${PYTHON_SCRIPT_NAME}" -p $DEVICE_PORT --get mac)
if [ $? -ne 0 ] || [ -z "$DEVICE_MAC" ]; then echo "Error obteniendo MAC."; exit 1; fi
DEVICE_PASSCODE=$($PYTHON_INTERPRETER "${SCRIPT_DIR}/${PYTHON_SCRIPT_NAME}" -p $DEVICE_PORT --get passcode)
if [ $? -ne 0 ] || [ -z "$DEVICE_PASSCODE" ]; then echo "Error obteniendo Passcode."; exit 1; fi
DEVICE_DISCRIMINATOR=$($PYTHON_INTERPRETER "${SCRIPT_DIR}/${PYTHON_SCRIPT_NAME}" -p $DEVICE_PORT --get discriminator)
if [ $? -ne 0 ] || [ -z "$DEVICE_DISCRIMINATOR" ]; then echo "Error obteniendo Discriminator."; exit 1; fi

SERIAL_SUFFIX=$(echo "$DEVICE_MAC" | tr -d ':')
DEVICE_SERIAL="$SERIAL_SUFFIX"
OUTPUT_DIR="${OUTPUT_BASE_DIR}/${DEVICE_SERIAL}"
echo "-------------------------------------------"
echo "Dispositivo MAC: $DEVICE_MAC"
echo "Serial #:        $DEVICE_SERIAL"
echo "Passcode:        $DEVICE_PASSCODE"
echo "Discriminator:   $DEVICE_DISCRIMINATOR"
echo "Directorio Salida: $OUTPUT_DIR"
echo "-------------------------------------------"
mkdir -p "$OUTPUT_DIR"

# --- Ejecutar esp-matter-mfg-tool (Modo Generación DAC Interna) ---
echo "--- Ejecutando esp-matter-mfg-tool (Generando DAC internamente) ---"
# Definir la ruta esperada del binario de salida para flasheo posterior
MFG_BIN_SEARCH_PATH="${OUTPUT_DIR}/fff1_8000"

$MFG_TOOL_CMD \
    --passcode "$DEVICE_PASSCODE" \
    --discriminator "$DEVICE_DISCRIMINATOR" \
    --serial-num "$DEVICE_SERIAL" \
    --vendor-id "$VENDOR_ID" \
    --product-id "$PRODUCT_ID" \
    --vendor-name "$VENDOR_NAME" \
    --product-name "$PRODUCT_NAME" \
    --hw-ver 1 \
    -cd "$CD_FILE" \
    --pai \
    -c "$PAI_CERT_PEM" \
    -k "$PAI_KEY_PEM" \
    --cn-prefix "Matter Test DAC" \
    --lifetime 7305 \
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