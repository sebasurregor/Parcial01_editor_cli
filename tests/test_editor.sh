#!/bin/bash
# =====================================================================================
# Script de pruebas del Editor de Texto CLI
# Universidad EAFIT - Sistemas Operativos (SO2026B)
# =====================================================================================
# Ejecuta el editor alimentando su STDIN con secuencias de comandos y verifica el
# resultado real en disco. Se usa la bandera -q (modo silencioso) para que la traza
# de syscalls no ensucie la salida; para verla, quita el -q.
#
#   Uso:  ./test_editor.sh
# =====================================================================================

EDITOR=./bin/editor
DIR=pruebas_editor
PASS=0
FALLA=0
OMIT=0

V="\033[1;32m"   # verde
R="\033[1;31m"   # rojo
T="\033[1;97m"   # blanco
I="\033[0;90m"   # gris
N="\033[0m"      # reset

if [ ! -x "$EDITOR" ]; then
    echo -e "${R}No se encuentra el binario $EDITOR. Ejecuta 'make' primero.${N}"
    exit 1
fi

rm -rf "$DIR" && mkdir -p "$DIR"

titulo() { echo -e "\n${T}=== $1 ===${N}"; }

# omitir <motivo>: la prueba no aplica en este entorno; no cuenta como fallo
omitir() {
    echo -e "  ${I}[OMITIDO]${N} $1"
    OMIT=$((OMIT + 1))
}

# ---------------------------------------------------------------------------------
# Deteccion de capacidades del sistema de archivos
# ---------------------------------------------------------------------------------
# En WSL, cuando el proyecto vive bajo /mnt/c (una unidad de Windows montada como
# DrvFs), el sistema de archivos NO almacena los bits de permiso de POSIX: todo
# archivo se reporta como 777 sin importar el modo con el que se creo. Lo mismo
# ocurre en particiones NTFS, FAT32 o exFAT montadas en Linux y en algunos
# recursos de red.
#
# Esto NO indica ningun fallo del editor: open(2) sigue recibiendo el modo 0644
# exactamente igual, pero el sistema de archivos lo descarta al guardarlo. Por eso
# la prueba se omite en lugar de darse por fallida.
soporta_permisos_posix() {
    local f="$DIR/.chk_perm"
    : > "$f" 2>/dev/null || return 1
    chmod 600 "$f" 2>/dev/null || { rm -f "$f"; return 1; }
    local m
    m=$(stat -c %a "$f" 2>/dev/null)
    rm -f "$f"
    [ "$m" = "600" ]
}

# open(..., 0644) crea el archivo con el modo 0644 filtrado por la umask del
# proceso. Con la umask habitual (022) el resultado es 644, pero se calcula para
# no depender de la configuracion de quien ejecute las pruebas.
permisos_esperados() {
    printf '%o\n' "$(( 0644 & ~8#$(umask) ))"
}

# verificar <descripcion> <esperado> <obtenido>
verificar() {
    if [ "$2" == "$3" ]; then
        echo -e "  ${V}[OK]${N}    $1"
        PASS=$((PASS + 1))
    else
        echo -e "  ${R}[FALLA]${N} $1"
        echo -e "         ${I}esperado: [$2]${N}"
        echo -e "         ${I}obtenido: [$3]${N}"
        FALLA=$((FALLA + 1))
    fi
}

# ---------------------------------------------------------------------------------
titulo "1. Crear un archivo inexistente y añadir líneas"
# ---------------------------------------------------------------------------------
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/uno.txt
a primera linea
a segunda linea
a tercera linea
q
EOF
verificar "el archivo se creó en disco" "si" \
          "$([ -f $DIR/uno.txt ] && echo si || echo no)"
verificar "contiene 3 líneas" "3" "$(wc -l < $DIR/uno.txt)"
verificar "el contenido es el esperado" "primera linea|segunda linea|tercera linea" \
          "$(tr '\n' '|' < $DIR/uno.txt | sed 's/|$//')"
if soporta_permisos_posix; then
    ESPERADO=$(permisos_esperados)
    verificar "los permisos son $ESPERADO (modo 0644 filtrado por la umask)" \
              "$ESPERADO" "$(stat -c %a $DIR/uno.txt)"
else
    omitir "permisos POSIX: este sistema de archivos no los conserva."
    echo -e "         ${I}Ocurre al trabajar bajo /mnt/c en WSL o sobre NTFS/FAT.${N}"
    echo -e "         ${I}Copia el proyecto a tu carpeta ~ de Linux para comprobarlo.${N}"
fi

# ---------------------------------------------------------------------------------
titulo "2. Abrir un archivo existente NO destruye su contenido (sin O_TRUNC)"
# ---------------------------------------------------------------------------------
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/uno.txt
q
EOF
verificar "el contenido sobrevive al open" "3" "$(wc -l < $DIR/uno.txt)"

# ---------------------------------------------------------------------------------
titulo "3. Imprimir una línea concreta y el archivo completo"
# ---------------------------------------------------------------------------------
SALIDA=$($EDITOR -q 2>/dev/null << 'EOF'
o pruebas_editor/uno.txt
p 2
q
EOF
)
verificar "p 2 imprime la segunda línea" "si" \
          "$(echo "$SALIDA" | grep -q 'segunda linea' && echo si || echo no)"
verificar "p 2 NO imprime las otras líneas" "no" \
          "$(echo "$SALIDA" | grep -q 'tercera linea' && echo si || echo no)"

SALIDA=$($EDITOR -q 2>/dev/null << 'EOF'
o pruebas_editor/uno.txt
p
q
EOF
)
verificar "p sin argumento imprime las 3 líneas" "3" \
          "$(echo "$SALIDA" | grep -c 'linea$')"

# ---------------------------------------------------------------------------------
titulo "4. Borrado: primera, intermedia y última línea"
# ---------------------------------------------------------------------------------
printf 'A\nB\nC\nD\n' > $DIR/borrar.txt
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/borrar.txt
d 1
q
EOF
verificar "borrar la PRIMERA línea desplaza la cola" "B|C|D" \
          "$(tr '\n' '|' < $DIR/borrar.txt | sed 's/|$//')"

printf 'A\nB\nC\nD\n' > $DIR/borrar.txt
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/borrar.txt
d 2
q
EOF
verificar "borrar una línea INTERMEDIA" "A|C|D" \
          "$(tr '\n' '|' < $DIR/borrar.txt | sed 's/|$//')"

printf 'A\nB\nC\nD\n' > $DIR/borrar.txt
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/borrar.txt
d 4
q
EOF
verificar "borrar la ÚLTIMA línea (solo ftruncate)" "A|B|C" \
          "$(tr '\n' '|' < $DIR/borrar.txt | sed 's/|$//')"
verificar "el tamaño en disco se recortó a 6 bytes" "6" "$(stat -c %s $DIR/borrar.txt)"

# ---------------------------------------------------------------------------------
titulo "5. Vaciar el archivo por completo y borrar de más"
# ---------------------------------------------------------------------------------
printf 'A\nB\n' > $DIR/vaciar.txt
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/vaciar.txt
d 1
d 1
q
EOF
verificar "el archivo queda en 0 bytes" "0" "$(stat -c %s $DIR/vaciar.txt)"

SALIDA=$($EDITOR -q 2>&1 << 'EOF'
o pruebas_editor/vaciar.txt
d 1
q
EOF
)
verificar "borrar en archivo vacío reporta error controlado" "si" \
          "$(echo "$SALIDA" | grep -qi 'vacio' && echo si || echo no)"

# ---------------------------------------------------------------------------------
titulo "6. Caso borde: archivo cuyo último byte NO es '\\n'"
# ---------------------------------------------------------------------------------
printf 'sin salto final' > $DIR/sinsalto.txt
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/sinsalto.txt
a nueva
q
EOF
verificar "el editor inserta el '\\n' faltante" "sin salto final|nueva" \
          "$(tr '\n' '|' < $DIR/sinsalto.txt | sed 's/|$//')"
verificar "quedan 2 líneas bien formadas" "2" "$(wc -l < $DIR/sinsalto.txt)"

# ---------------------------------------------------------------------------------
titulo "7. Texto con espacios, acentos y línea vacía"
# ---------------------------------------------------------------------------------
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/texto.txt
a  esto tiene    espacios internos y acentuación: ñáéíóú
a
q
EOF
verificar "se preservan espacios y acentos" "si" \
          "$(grep -q 'espacios internos y acentuación: ñáéíóú' $DIR/texto.txt && echo si || echo no)"
verificar "se preserva la indentación (espacio extra)" "si" \
          "$(head -1 $DIR/texto.txt | grep -q '^ esto' && echo si || echo no)"
verificar "'a' sin texto añade una línea vacía" "2" "$(wc -l < $DIR/texto.txt)"

# ---------------------------------------------------------------------------------
titulo "8. Validación de argumentos fuera de rango e inválidos"
# ---------------------------------------------------------------------------------
SALIDA=$($EDITOR -q 2>&1 << 'EOF'
o pruebas_editor/uno.txt
p 99
p 0
d 99
p abc
z
q
EOF
)
verificar "p 99 (fuera de rango) da error" "si" \
          "$(echo "$SALIDA" | grep -q 'linea 99 no existe' && echo si || echo no)"
verificar "p abc (no numérico) da error de uso" "si" \
          "$(echo "$SALIDA" | grep -q 'Uso: p' && echo si || echo no)"
verificar "comando desconocido 'z' es rechazado" "si" \
          "$(echo "$SALIDA" | grep -q "no reconocido" && echo si || echo no)"
verificar "el archivo no se corrompió tras los errores" "3" "$(wc -l < $DIR/uno.txt)"

SALIDA=$($EDITOR -q 2>&1 << 'EOF'
a texto sin haber abierto nada
p
q
EOF
)
verificar "operar sin archivo abierto da error controlado" "si" \
          "$(echo "$SALIDA" | grep -q 'no hay ningun archivo abierto' && echo si || echo no)"

# ---------------------------------------------------------------------------------
titulo "9. Errores del sistema reportados con perror"
# ---------------------------------------------------------------------------------
SALIDA=$($EDITOR -q 2>&1 << 'EOF'
o /directorio/que/no/existe/x.txt
q
EOF
)
verificar "ruta inválida reporta ENOENT vía perror" "si" \
          "$(echo "$SALIDA" | grep -qi 'No such file' && echo si || echo no)"

touch $DIR/solo_lectura.txt && chmod 444 $DIR/solo_lectura.txt 2>/dev/null
if [ "$(id -u)" = "0" ]; then
    omitir "EACCES: root ignora los bits de acceso."
elif [ -w "$DIR/solo_lectura.txt" ]; then
    omitir "EACCES: este sistema de archivos no aplica el bit de solo lectura."
else
    SALIDA=$($EDITOR -q 2>&1 << 'EOF'
o pruebas_editor/solo_lectura.txt
q
EOF
)
    verificar "archivo sin permiso de escritura reporta EACCES" "si" \
              "$(echo "$SALIDA" | grep -qi 'Permission denied' && echo si || echo no)"
fi
chmod 644 $DIR/solo_lectura.txt 2>/dev/null

# ---------------------------------------------------------------------------------
titulo "10. Reapertura sin 'q': no debe filtrar descriptores"
# ---------------------------------------------------------------------------------
SALIDA=$($EDITOR -q 2>&1 << 'EOF'
o pruebas_editor/uno.txt
o pruebas_editor/borrar.txt
o pruebas_editor/texto.txt
i
q
EOF
)
verificar "el fd se reutiliza en cada apertura (no crece)" "si" \
          "$(echo "$SALIDA" | grep -q 'Descriptor (fd):    3' && echo si || echo no)"

# ---------------------------------------------------------------------------------
titulo "11. Archivo grande (fuerza varias iteraciones del búfer de 4 KB)"
# ---------------------------------------------------------------------------------
seq 1 2000 | sed 's/^/linea /' > $DIR/grande.txt
ORIG=$(wc -c < $DIR/grande.txt)
$EDITOR -q > /dev/null 2>&1 << 'EOF'
o pruebas_editor/grande.txt
d 1
q
EOF
verificar "quedan 1999 líneas tras borrar la primera" "1999" "$(wc -l < $DIR/grande.txt)"
verificar "la primera línea ahora es 'linea 2'" "linea 2" "$(head -1 $DIR/grande.txt)"
verificar "la última línea sigue intacta" "linea 2000" "$(tail -1 $DIR/grande.txt)"
verificar "el tamaño se redujo exactamente en 8 bytes" "$((ORIG - 8))" \
          "$(wc -c < $DIR/grande.txt)"

# ---------------------------------------------------------------------------------
titulo "12. Salida por Ctrl+D (EOF) en lugar de 'q'"
# ---------------------------------------------------------------------------------
printf 'o pruebas_editor/uno.txt\na desde EOF\n' | $EDITOR -q > /dev/null 2>&1
verificar "EOF cierra el editor guardando los cambios" "4" "$(wc -l < $DIR/uno.txt)"

# ---------------------------------------------------------------------------------
titulo "RESUMEN"
# ---------------------------------------------------------------------------------
echo -e "  Pruebas superadas: ${V}$PASS${N}"
if [ "$FALLA" -gt 0 ]; then
    echo -e "  Pruebas fallidas:  ${R}$FALLA${N}"
else
    echo -e "  Pruebas fallidas:  $FALLA"
fi
if [ "$OMIT" -gt 0 ]; then
    echo -e "  Pruebas omitidas:  ${I}$OMIT  (no aplican en este entorno)${N}"
fi
echo ""

# ---------------------------------------------------------------------------------
# Verificación de fugas de memoria (si valgrind está instalado)
# ---------------------------------------------------------------------------------
if command -v valgrind > /dev/null 2>&1; then
    titulo "VALGRIND: verificación de fugas de memoria"
    valgrind --leak-check=full --error-exitcode=99 $EDITOR -q 2> $DIR/valgrind.log << 'EOF' > /dev/null
o pruebas_editor/uno.txt
a linea de prueba
p
d 1
o pruebas_editor/grande.txt
p 5
q
EOF
    grep -E "in use at exit|total heap usage|All heap blocks|definitely lost|ERROR SUMMARY" \
         $DIR/valgrind.log | sed 's/^/  /'
else
    echo -e "${I}(valgrind no está instalado; instálalo con 'sudo apt install valgrind'"
    echo -e " y vuelve a ejecutar el script para verificar las fugas de memoria)${N}"
fi

[ "$FALLA" -eq 0 ] || exit 1
exit 0
