# Editor de Texto CLI en Unix + Shell Educativo de Syscalls

**Universidad EAFIT — Sistemas Operativos (C2666-SI2004-4328)**
Taller Práctico 01 · Equipo individual

Editor de texto interactivo operado por línea de comandos, escrito en C y
apoyado exclusivamente en llamadas al sistema POSIX, integrado dentro del Shell
Educativo de Syscalls estudiado en clase.

---

## Restricción de entrada/salida

Ninguna operación sobre el archivo de texto usa `stdio.h`. No hay `fopen`,
`fread`, `fwrite` ni `fclose` sobre el archivo editado. Todo el acceso a disco
se hace con:

`open(2)` · `read(2)` · `write(2)` · `lseek(2)` · `ftruncate(2)` · `close(2)`

Las funciones `printf` y `fgets` se usan únicamente para el diálogo por consola,
que es lo que el enunciado permite de forma expresa.

---

## Compilación y ejecución

```bash
make          # compila bin/eafitOS y bin/editor
make run      # compila y ejecuta el shell
make test     # ejecuta las pruebas automatizadas
make valgrind # verifica que no haya fugas de memoria
make clean    # elimina binarios, objetos y temporales
```

Requiere Linux con GCC y Make. `valgrind` es opcional.

---

## Uso

### Dentro del shell (forma integrada)

```
$ ./bin/eafitOS
eafitOS> help edicion
eafitOS> edit documento.txt
edit:documento.txt> a Primera linea
edit:documento.txt> p
edit:documento.txt> q
eafitOS>
```

### Como binario independiente

```bash
./bin/editor documento.txt      # con traza de syscalls
./bin/editor -q documento.txt   # modo silencioso, sin traza
```

También puede lanzarse como proceso externo desde el propio shell, usando
`fork(2)` y `execvp(3)`:

```
eafitOS> p_exec ./bin/editor documento.txt
```

---

## Comandos del editor

| Comando | Descripción | Llamadas al sistema |
|---------|-------------|---------------------|
| `o <archivo>` | Abre el archivo; si no existe, lo crea con permisos 0644. | `open`, `lseek`, `read`, `close` |
| `p [n]` | Imprime la línea n. Sin argumento, todo el archivo. | `lseek`, `read`, `write` (fd 1) |
| `a <texto>` | Añade el texto como línea nueva al final. | `lseek`, `read`, `write` |
| `d <n>` | Borra la línea n desplazando la cola y truncando. | `lseek`, `read`, `write`, `ftruncate` |
| `q` | Cierra el descriptor, libera la memoria y sale. | `close` |
| `h` | Muestra la ayuda. | — |
| `i` | Muestra el estado interno (fd, tamaño, líneas, heap). | — |
| `t` | Activa o desactiva la traza de llamadas al sistema. | — |

`Ctrl+D` equivale a `q`.

---

## Decisiones de diseño

Las tres decisiones centrales, desarrolladas en detalle en el documento de
sustentación:

**1. Categoría nueva `edicion` en el shell.** Las cuatro categorías existentes
están organizadas por subsistema del kernel, y todos sus comandos comparten una
invariante: son atómicos y sin estado. El editor rompe ambas propiedades, porque
abre un REPL anidado que se apropia de STDIN y mantiene un descriptor vivo entre
comandos. La diferencia es de naturaleza, no de tema, y por eso se creó una
categoría propia en lugar de forzarlo dentro de `datos`.

**2. El disco como única fuente de verdad.** En lugar de cargar el archivo en
memoria como hace `vi`, se conserva en RAM solo un índice de líneas con los
desplazamientos de cada una. Toda escritura va directa al disco. Se gana
independencia del tamaño del archivo e imposibilidad de desincronización; se
pierde la función de deshacer.

**3. Motor independiente del shell.** `src/editor/editor.c` no incluye
`shell.h`. El mismo objeto se enlaza en los dos binarios, lo que permite
demostrar también la estrategia alternativa de integración (`p_exec`, con `fork`
y `execvp`) sin código adicional.

---

## Estructura del proyecto

```
.
├── Makefile                    targets: all, run, test, valgrind, clean
├── README.md                   este archivo
├── include/
│   ├── editor.h                tipos y API pública del motor
│   └── shell.h                 cabecera del shell (modificada)
├── src/
│   ├── editor/
│   │   ├── editor.c            MOTOR: toda la lógica y las syscalls
│   │   └── editor_main.c       main() del binario autónomo
│   └── shell/
│       ├── main.c              tabla de comandos y REPL (modificado)
│       ├── cat_edicion.c       ADAPTADOR: cmd_edit() (nuevo)
│       ├── cat_datos.c         categoría datos (sin cambios)
│       ├── cat_memoria.c       categoría memoria (sin cambios)
│       ├── cat_monitoreo.c     categoría monitoreo (sin cambios)
│       └── cat_util.c          categoría utilidades (sin cambios)
├── tests/
│   └── test_editor.sh          12 escenarios, 31 verificaciones
└── extras/
    ├── cloner.c                material de apoyo: hard links con link(2)
    └── file_manager.c          material de apoyo: CRUD con syscalls
```

Al compilar se generan dos carpetas adicionales, `bin/` con los ejecutables y
`build/` con los objetos intermedios. Ambas se eliminan con `make clean`.

---

## Estado de la validación

- Compila con `-Wall -Wextra` sin advertencias.
- 31 de 31 pruebas automatizadas superadas.
- Valgrind: 0 bytes en uso al salir, 0 errores.

### Nota para usuarios de WSL

Ejecuta el proyecto desde tu carpeta de Linux (`~`) y no desde `/mnt/c`. Las
rutas de Windows montadas en WSL no conservan los bits de permiso de POSIX: todo
archivo se reporta como `777`. El editor funciona igual —`open(2)` recibe el
modo `0644` exactamente como debe—, pero la comprobación de permisos no puede
verificarse ahí y el script la marca como omitida en vez de fallida. Lo mismo
aplica sobre particiones NTFS, FAT32 o exFAT.

```bash
cp -r "/mnt/c/ruta/al/proyecto" ~/ && cd ~/proyecto && make
```

El script distingue tres resultados: superadas, fallidas y omitidas. Lo que debe
ser cero siempre es **fallidas**; las omitidas solo indican que la comprobación
no aplica en ese entorno.

---

## Cobertura total de la restricción de E/S

En el shell original, el comando `m_info` de `cat_memoria.c` leía
`/proc/self/status` con `fopen`/`fgets`/`fclose`. Aunque la restricción del
taller apunta al archivo de texto que manipula el editor, ese comando se
convirtió a `open(2)`, `read(2)` y `close(2)` para que **ningún punto del
entregable** use la biblioteca estándar de C con fines de acceso a archivos.

Comprobación:

```bash
grep -rn "fopen\|fread\|fwrite\|fclose" src/ include/
```

No devuelve ninguna coincidencia en código; las únicas apariciones de `fgets`
son sobre `stdin`, que el enunciado permite de forma expresa por tratarse de
diálogo por consola.

La conversión obligó a reimplementar a mano lo que `fgets` hacía por nosotros:

- **Bucle de lectura hasta EOF.** Los archivos de `/proc` los genera el kernel al
  vuelo, de modo que `stat(2)` informa un tamaño de 0 bytes y no se puede
  reservar el bloque exacto de antemano. Hay que leer hasta que `read(2)`
  devuelva 0.
- **Manejo de lecturas parciales y de `EINTR`**, con los mismos criterios que el
  editor.
- **Troceado en líneas con `memchr(3)`**, que opera sobre memoria ya leída y no
  sobre el archivo.

La traza que produce el comando deja ver el mecanismo con claridad:

```
[syscall] open("/proc/self/status", O_RDONLY) ... = 3
[syscall] read(3, buffer+0, 1024) ... = 1024
[syscall] read(3, buffer+1024, 1024) ... = 383     <- lectura parcial
[syscall] read(3, buffer+1407, 1024) ... = 0       <- fin de archivo
[syscall] close(3) ... = 0
```

Esa lectura de 383 bytes es justamente el caso que un `read` único sin bucle
habría truncado en silencio.
