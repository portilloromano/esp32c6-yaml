import os

def guardar_contenido_directorio(directorio, archivo_salida):
    with open(archivo_salida, 'w', encoding='utf-8') as salida:
        for root, _, files in os.walk(directorio):
            for file in files:
                ruta_completa = os.path.join(root, file)
                salida.write(f"File: {ruta_completa}\n")
                # salida.write(f"Nombre: {file}\n")
                # salida.write("\nContenido:\n")
                try:
                    with open(ruta_completa, 'r', encoding='utf-8') as f:
                        salida.write(f.read())
                except Exception as e:
                    salida.write(f"[Error al leer archivo: {e}]\n")
                salida.write("\n" + "-" * 50 + "\n\n")

# Ejemplo de uso
# directorio = "./main"
directorio = "./main"
archivo_salida = "code.txt"
guardar_contenido_directorio(directorio, archivo_salida)
