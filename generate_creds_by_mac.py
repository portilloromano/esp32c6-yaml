#!/usr/bin/env python3

import argparse
import sys
import binascii
import subprocess
import re
import os

# Constantes (igual que antes)
INVALID_PASSCODES = {
    00000000, 11111111, 22222222, 33333333, 44444444,
    55555555, 66666666, 77777777, 88888888, 99999999,
    12345678, 87654321
}
FALLBACK_PASSCODE = 20202021
FALLBACK_DISCRIMINATOR = 0xF00 # 3840

# --- Funciones de generación (sin cambios) ---

def mac_str_to_bytes(mac_str: str) -> bytes | None:
    """Convierte una cadena MAC AA:BB:CC:DD:EE:FF a bytes."""
    try:
        mac_bytes = binascii.unhexlify(mac_str.replace(':', ''))
        if len(mac_bytes) != 6:
            raise ValueError("La MAC debe tener 6 bytes")
        return mac_bytes
    except (ValueError, binascii.Error) as e:
        print(f"Error: Formato de MAC inválido '{mac_str}'. Debe ser como AA:BB:CC:DD:EE:FF. Detalles: {e}", file=sys.stderr)
        return None

def generate_discriminator_from_mac(mac_bytes: bytes) -> int:
    """Genera el discriminador de 12 bits desde los bytes de la MAC."""
    if len(mac_bytes) < 6:
        print("Error: Se necesitan al menos 6 bytes de MAC para el discriminador.", file=sys.stderr)
        return FALLBACK_DISCRIMINATOR
    discriminator_val = (mac_bytes[4] << 8) | mac_bytes[5]
    discriminator_val &= 0x0FFF
    if discriminator_val in [0x000, 0xFFF, 0xFFE]:
        print(f"Warning: Discriminador generado inválido/reservado (0x{discriminator_val:03X}). Usando fallback 0x{FALLBACK_DISCRIMINATOR:03X}", file=sys.stderr)
        return FALLBACK_DISCRIMINATOR
    return discriminator_val

def generate_passcode_from_mac(mac_bytes: bytes) -> int:
    """Genera el passcode de 8 dígitos desde los bytes de la MAC."""
    if len(mac_bytes) < 4:
        print("Error: Se necesitan al menos 4 bytes de MAC para el passcode.", file=sys.stderr)
        return FALLBACK_PASSCODE
    passcode_base = (mac_bytes[0] << 24) | (mac_bytes[1] << 16) | (mac_bytes[2] << 8) | mac_bytes[3]
    passcode = (passcode_base % 99999998) + 1
    if passcode in INVALID_PASSCODES:
        print(f"Warning: Passcode generado inválido ({passcode:08}). Usando fallback {FALLBACK_PASSCODE}", file=sys.stderr)
        return FALLBACK_PASSCODE
    return passcode

# --- Función para obtener MAC con esptool (sin cambios respecto a la última versión) ---

def get_ieee802154_mac(port: str | None) -> str | None:
    """Obtiene la MAC IEEE 802.15.4 usando esptool.py"""
    try:
        esptool_cmd = "esptool.py" # Asumimos que está en el PATH

        cmd = [esptool_cmd]
        if port:
            cmd.extend(["--port", port])
        cmd.append("read_mac")

        print(f"Info: Ejecutando esptool para leer MAC: {' '.join(cmd)}", file=sys.stderr)
        result = subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=15)
        output = result.stdout

        print(f"Info: Salida de esptool:\n{output}", file=sys.stderr)

        match = re.search(r"MAC:\s*([0-9A-Fa-f:]{17}).*\(IEEE 802\.15\.4\)", output, re.IGNORECASE)

        if not match:
             print("Warning: No se encontró '(IEEE 802.15.4)'. Buscando 'BASE MAC:' como alternativa...", file=sys.stderr)
             match = re.search(r"BASE MAC:\s*([0-9A-Fa-f:]{17})", output, re.IGNORECASE)

        if match:
            mac_address = match.group(1).upper()
            if len(mac_address.replace(':','')) == 12:
                 print(f"Info: MAC (IEEE 802.15.4 o BASE) encontrada: {mac_address}", file=sys.stderr)
                 return mac_address
            else:
                 print(f"Error: MAC encontrada '{mac_address}' no tiene el formato esperado (6 bytes).", file=sys.stderr)
                 return None
        else:
            print("Error: No se pudo encontrar la MAC IEEE 802.15.4 ni la BASE MAC en la salida de esptool.", file=sys.stderr)
            return None

    except FileNotFoundError:
         print(f"Error: El comando '{esptool_cmd}' no se encontró. Asegúrate de que esptool.py esté en tu PATH (activa el entorno ESP-IDF).", file=sys.stderr)
         return None
    except subprocess.TimeoutExpired:
        print(f"Error: Timeout esperando la respuesta de esptool.py. ¿Está el dispositivo conectado y respondiendo en el puerto {port}?", file=sys.stderr)
        return None
    except subprocess.CalledProcessError as e:
        print(f"Error al ejecutar esptool.py (código de salida {e.returncode}): {e}", file=sys.stderr)
        print(f"Salida de error de esptool:\n{e.stderr}", file=sys.stderr)
        print(f"Salida estándar de esptool:\n{e.stdout}", file=sys.stderr)
        print("Consejo: Asegúrate de que el puerto serie sea correcto y que ningún otro proceso lo esté usando.", file=sys.stderr)
        print("Consejo: Intenta poner el dispositivo en modo bootloader (BOOT+RESET) si la lectura normal falla.", file=sys.stderr)
        return None
    except Exception as e:
        print(f"Error inesperado al obtener la MAC: {e}", file=sys.stderr)
        return None


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Genera Passcode, Discriminador o MAC Matter desde un dispositivo.')
    parser.add_argument('--port', '-p', help='Puerto serie al que está conectado el dispositivo ESP32 (ej: /dev/ttyUSB0, COM3). Si se omite, esptool intentará autodetectar.')
    # ----> MODIFICACIÓN AQUÍ: Añadir 'mac' a las opciones <----
    parser.add_argument('--get', required=True, choices=['passcode', 'discriminator', 'mac'], help='Qué valor obtener/generar')
    # ----------------------------------------------------------

    args = parser.parse_args()

    mac_address_str = get_ieee802154_mac(args.port)

    if mac_address_str is None:
        print("Error: No se pudo obtener la dirección MAC del dispositivo.", file=sys.stderr)
        sys.exit(1)

    # ----> MODIFICACIÓN AQUÍ: Procesar la nueva opción 'mac' <----
    if args.get == 'mac':
        print(mac_address_str) # Simplemente imprime la MAC encontrada
        sys.exit(0)
    # -----------------------------------------------------------

    # Si no era 'mac', convertir a bytes para generar los otros valores
    mac_bytes = mac_str_to_bytes(mac_address_str)
    if mac_bytes is None:
        sys.exit(1)

    if args.get == 'passcode':
        result = generate_passcode_from_mac(mac_bytes)
        print(f"{result:08}")
    elif args.get == 'discriminator':
        result = generate_discriminator_from_mac(mac_bytes)
        print(result)
    # El 'else' ya no es necesario porque argparse valida las choices

    sys.exit(0)