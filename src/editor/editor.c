/* strdup() y otras extensiones requieren esta macro. Se protege con #ifndef
 * porque el Makefile ya la define con -D_GNU_SOURCE; así el archivo compila
 * igual de bien si alguien lo compila a mano sin esa bandera. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "editor.h"

#include <stdio.h>      /* printf, fgets, snprintf: SOLO para dialogo por consola */
#include <stdlib.h>     /* malloc, realloc, free, strtol                          */
#include <string.h>     /* strlen, memcpy, strcmp                                 */
#include <errno.h>      /* errno                                                  */
#include <fcntl.h>      /* open, O_RDWR, O_CREAT                                  */
#include <unistd.h>     /* read, write, lseek, ftruncate, close, STDOUT_FILENO    */

/* ==========================================================================
 * Colores ANSI.
 * Se redefinen aqui (en vez de incluir shell.h) para preservar la
 * independencia del motor respecto del shell. Es una duplicacion consciente:
 * el precio de mantener el modulo reutilizable.
 * ========================================================================== */
#define C_RESET   "\033[0m"
#define C_PROMPT  "\033[1;36m"   /* cian    */
#define C_SYSCALL "\033[1;35m"   /* magenta */
#define C_OK      "\033[1;32m"   /* verde   */
#define C_ERR     "\033[1;31m"   /* rojo    */
#define C_INFO    "\033[0;90m"   /* gris    */
#define C_TITLE   "\033[1;97m"   /* blanco  */

/* Tamano del bufer de trabajo. 4096 bytes coincide con el tamano de pagina y
 * con el tamano de bloque tipico de los sistemas de archivos de Linux, asi que
 * es el punto donde el costo por syscall se amortiza mejor. */
#define BUFSZ    4096
#define MAX_LINE 4096

/* ==========================================================================
 * Macros de trazado (estilo strace simplificado).
 * TRACE imprime la llamada SIN salto de linea; TRACE_OK o TRACE_ERR la cierran.
 * Entre ambas no puede imprimirse nada mas, o la linea quedaria partida.
 * ========================================================================== */
#define TRACE(ed, name, fmt, ...)                                             \
    do { if ((ed)->trace)                                                     \
        printf(C_SYSCALL "[syscall] " name C_RESET "(" fmt ") ... ",          \
               ##__VA_ARGS__); } while (0)

#define TRACE_OK(ed, res)                                                     \
    do { if ((ed)->trace)                                                     \
        printf("= " C_OK "%lld" C_RESET "\n", (long long)(res)); } while (0)

#define TRACE_ERR(ed)                                                         \
    do { if ((ed)->trace)                                                     \
        printf("= " C_ERR "-1 (%s)" C_RESET "\n", strerror(errno)); } while (0)

/* ==========================================================================
 * SECCION 1: ENVOLTORIOS ROBUSTOS DE LAS SYSCALLS
 * --------------------------------------------------------------------------
 * read(2) y write(2) pueden transferir MENOS bytes de los solicitados sin que
 * eso constituya un error, y pueden ser interrumpidas por una senal retornando
 * -1 con errno == EINTR. Ignorar estos dos hechos es el error mas frecuente en
 * codigo que trabaja con descriptores de archivo. Estos envoltorios lo manejan
 * de forma centralizada para que el resto del modulo quede legible.
 *
 * Nota sobre errno: printf podria modificarlo, asi que se guarda y se restaura
 * antes de retornar, para que quien llame pueda usar perror() con seguridad.
 * ========================================================================== */

/* Lee hasta 'n' bytes. Retorna los bytes leidos (< n solo si llego a EOF), -1 error. */
static ssize_t ed_read(Editor *ed, void *buf, size_t n)
{
    size_t total = 0;

    while (total < n) {
        TRACE(ed, "read", "%d, buf+%zu, %zu", ed->fd, total, n - total);
        ssize_t r = read(ed->fd, (char *)buf + total, n - total);

        if (r == -1) {
            if (errno == EINTR) {                    /* interrumpida por senal */
                if (ed->trace) printf("= " C_ERR "EINTR" C_RESET " (reintentando)\n");
                continue;
            }
            int saved = errno;
            TRACE_ERR(ed);
            errno = saved;
            return -1;
        }
        TRACE_OK(ed, r);
        if (r == 0) break;                            /* fin de archivo (EOF) */
        total += (size_t)r;
    }
    return (ssize_t)total;
}

/* Escribe exactamente 'n' bytes en el archivo. Retorna 0 en exito, -1 en error. */
static int ed_write(Editor *ed, const void *buf, size_t n)
{
    size_t total = 0;

    while (total < n) {
        TRACE(ed, "write", "%d, buf+%zu, %zu", ed->fd, total, n - total);
        ssize_t w = write(ed->fd, (const char *)buf + total, n - total);

        if (w == -1) {
            if (errno == EINTR) {
                if (ed->trace) printf("= " C_ERR "EINTR" C_RESET " (reintentando)\n");
                continue;
            }
            int saved = errno;
            TRACE_ERR(ed);
            errno = saved;
            return -1;
        }
        TRACE_OK(ed, w);
        total += (size_t)w;                    /* puede haber escrito parcial */
    }
    return 0;
}

/* Reposiciona el puntero del archivo. Retorna el nuevo desplazamiento o -1. */
static off_t ed_lseek(Editor *ed, off_t offset, int whence, const char *wname)
{
    TRACE(ed, "lseek", "%d, %lld, %s", ed->fd, (long long)offset, wname);
    off_t pos = lseek(ed->fd, offset, whence);

    if (pos == (off_t)-1) {
        int saved = errno;
        TRACE_ERR(ed);
        errno = saved;
        return (off_t)-1;
    }
    TRACE_OK(ed, pos);
    return pos;
}

/**
 * Escritura hacia la salida estandar (descriptor 1) usando write(2).
 *
 * Se usa write() y no printf("%s") por una razon tecnica concreta: printf con
 * %s se detiene en el primer byte nulo, mientras que write() es seguro a nivel
 * de bytes y por lo tanto funciona incluso si el archivo contiene datos
 * binarios o bytes '\0' intercalados.
 *
 * El fflush(stdout) previo es OBLIGATORIO: printf acumula en un bufer de
 * espacio de usuario mientras que write() va directo al kernel. Sin el vaciado
 * el texto saldria desordenado, sobre todo cuando la salida es una tuberia.
 *
 * La traza se imprime DESPUES de la escritura porque el contenido del archivo
 * partiria la linea "[syscall] write(1, ...) ... = N" por la mitad.
 */
static int out_write(Editor *ed, const void *buf, size_t n)
{
    size_t total = 0;

    fflush(stdout);
    while (total < n) {
        ssize_t w = write(STDOUT_FILENO, (const char *)buf + total, n - total);
        if (w == -1) {
            if (errno == EINTR) continue;
            perror("editor: write(STDOUT)");
            return -1;
        }
        total += (size_t)w;
    }
    if (ed->trace)
        printf(C_SYSCALL "[syscall] write" C_RESET "(1, buf, %zu) ... = "
               C_OK "%zu" C_RESET "\n", n, total);
    return 0;
}

/* ==========================================================================
 * SECCION 2: INDICE DE LINEAS EN MEMORIA DINAMICA
 * ========================================================================== */

/* Agrega una linea al indice, duplicando la capacidad cuando se agota.
 * Duplicar (en lugar de crecer de uno en uno) amortiza el costo de realloc. */
static int index_push(Editor *ed, off_t start, off_t end)
{
    if (ed->nlines == ed->cap) {
        size_t ncap = (ed->cap == 0) ? 16 : ed->cap * 2;
        Line  *tmp  = realloc(ed->lines, ncap * sizeof(Line));

        /* Se asigna a una variable temporal: si realloc falla retorna NULL y
         * asignarlo directamente a ed->lines perderia el bloque original. */
        if (tmp == NULL) {
            perror("editor: realloc del indice de lineas");
            return -1;
        }
        ed->lines = tmp;
        ed->cap   = ncap;
    }
    ed->lines[ed->nlines].start = start;
    ed->lines[ed->nlines].end   = end;
    ed->nlines++;
    return 0;
}

/**
 * Reconstruye el indice recorriendo el archivo completo en bloques de 4 KB y
 * anotando la posicion de cada '\n'. Tambien recalcula el tamano real.
 * Se invoca al abrir y despues de cada modificacion.
 */
static int index_build(Editor *ed)
{
    char  buf[BUFSZ];
    off_t pos = 0, line_start = 0;

    ed->nlines = 0;   /* se reutiliza la capacidad ya reservada, no se libera */

    if (ed_lseek(ed, 0, SEEK_SET, "SEEK_SET") == (off_t)-1) {
        perror("editor: lseek al indexar");
        return -1;
    }

    for (;;) {
        ssize_t r = ed_read(ed, buf, sizeof buf);
        if (r == -1) { perror("editor: read al indexar"); return -1; }
        if (r == 0)  break;                                        /* EOF */

        for (ssize_t i = 0; i < r; i++) {
            pos++;                       /* pos = desplazamiento tras el byte */
            if (buf[i] == '\n') {
                if (index_push(ed, line_start, pos) == -1) return -1;
                line_start = pos;
            }
        }

        /* ed_read solo devuelve menos de lo pedido cuando alcanzo EOF, asi que
         * no hace falta una vuelta extra para confirmarlo: una syscall menos. */
        if ((size_t)r < sizeof buf) break;
    }

    /* Caso borde: el archivo termina sin '\n'. Esos bytes finales tambien son
     * una linea valida, aunque incompleta. */
    if (pos > line_start && index_push(ed, line_start, pos) == -1)
        return -1;

    ed->size = pos;
    return 0;
}

/* Verifica que haya un archivo abierto antes de operar. */
static int require_open(const Editor *ed)
{
    if (ed->fd == -1) {
        fprintf(stderr, C_ERR "editor: no hay ningun archivo abierto. "
                        "Usa 'o <archivo>' primero.\n" C_RESET);
        return 0;
    }
    return 1;
}

/* ==========================================================================
 * SECCION 3: OPERACIONES DEL EDITOR
 * ========================================================================== */

void editor_init(Editor *ed, int trace)
{
    ed->fd     = -1;
    ed->path   = NULL;
    ed->size   = 0;
    ed->lines  = NULL;
    ed->nlines = 0;
    ed->cap    = 0;
    ed->trace  = trace;
}

/**
 * COMANDO 'o <archivo>' -- abrir o crear.
 *
 * Syscalls: open(2), lseek(2), read(2) [al indexar], close(2) [si ya habia uno].
 *
 * Detalle critico: se usa O_RDWR | O_CREAT pero NUNCA O_TRUNC. Truncar al abrir
 * destruiria el contenido del usuario, que es justo lo contrario de lo que un
 * editor debe hacer. El modo 0644 (-rw-r--r--) solo se aplica si el archivo se
 * crea; si ya existe, sus permisos no se tocan.
 */
int editor_open(Editor *ed, const char *path)
{
    /* Si ya habia un archivo abierto se cierra primero. Omitir esto es la fuga
     * de descriptores mas comun: cada 'o' dejaria un fd huerfano. */
    if (ed->fd != -1) {
        printf(C_INFO "Cerrando el archivo anterior ('%s') antes de abrir el nuevo...\n"
               C_RESET, ed->path);
        editor_close(ed);
    }

    char *copy = strdup(path);     /* memoria dinamica: se libera en editor_close */
    if (copy == NULL) {
        perror("editor: strdup del nombre de archivo");
        return -1;
    }

    TRACE(ed, "open", "\"%s\", O_RDWR|O_CREAT, 0644", path);
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        int saved = errno;
        TRACE_ERR(ed);
        errno = saved;
        perror("editor: open");
        free(copy);
        return -1;
    }
    TRACE_OK(ed, fd);

    ed->fd   = fd;
    ed->path = copy;

    if (index_build(ed) == -1) {   /* si el indexado falla, no dejamos el fd abierto */
        editor_close(ed);
        return -1;
    }

    printf(C_OK "Archivo '%s' abierto." C_RESET
           C_INFO "  fd=%d  tamano=%lld bytes  lineas=%zu\n" C_RESET,
           ed->path, ed->fd, (long long)ed->size, ed->nlines);
    return 0;
}

/**
 * Imprime una linea del indice, precedida de su numero.
 *
 * El numero y el contenido se ensamblan en un UNICO bufer dinamico para
 * emitirlos con una sola llamada a write(2). Hacerlo en dos llamadas separadas
 * funcionaria igual, pero partiria visualmente la salida cuando la traza de
 * syscalls esta activa.
 */
static int print_line(Editor *ed, size_t idx)
{
    off_t  start = ed->lines[idx].start;
    size_t len   = (size_t)(ed->lines[idx].end - start);
    char   prefix[32];

    int plen = snprintf(prefix, sizeof prefix, "%6zu  ", idx + 1);

    /* +1 por si hay que agregar un '\n' que el archivo no tiene */
    char *buf = malloc((size_t)plen + len + 1);
    if (buf == NULL) { perror("editor: malloc"); return -1; }

    memcpy(buf, prefix, (size_t)plen);

    if (ed_lseek(ed, start, SEEK_SET, "SEEK_SET") == (off_t)-1) {
        perror("editor: lseek al imprimir");
        free(buf);
        return -1;
        
    }
    ssize_t r = ed_read(ed, buf + plen, len);
    if (r == -1) {
        perror("editor: read al imprimir");
        free(buf);
        return -1;
    }

    size_t total = (size_t)plen + (size_t)r;

    /* Si la ultima linea del archivo no trae '\n', se agrega SOLO en pantalla
     * para que el prompt no quede pegado al texto. El archivo no se modifica. */
    if (r == 0 || buf[total - 1] != '\n')
        buf[total++] = '\n';

    out_write(ed, buf, total);

    free(buf);
    return 0;
}

/**
 * COMANDO 'p [n]' -- imprimir.
 *
 * Syscalls: lseek(2), read(2), write(2) sobre el descriptor 1.
 * Con n <= 0 imprime el archivo completo recorriendo el indice de lineas.
 */
int editor_print(Editor *ed, long n)
{
    if (!require_open(ed)) return -1;

    if (ed->nlines == 0) {
        printf(C_INFO "(el archivo esta vacio)\n" C_RESET);
        return 0;
    }

    if (n > 0) {
        if ((size_t)n > ed->nlines) {
            fprintf(stderr, C_ERR "editor: la linea %ld no existe "
                            "(el archivo tiene %zu lineas).\n" C_RESET, n, ed->nlines);
            return -1;
        }
        return print_line(ed, (size_t)n - 1);
    }

    for (size_t i = 0; i < ed->nlines; i++)
        if (print_line(ed, i) == -1) return -1;

    return 0;
}

/**
 * COMANDO 'a <texto>' -- anadir una linea al final.
 *
 * Syscalls: lseek(2) con SEEK_END, read(2), write(2).
 *
 * Se resuelven dos detalles que suelen pasarse por alto:
 *
 * 1) Si el archivo no esta vacio y su ultimo byte NO es '\n', hay que insertar
 *    uno antes del texto nuevo; de lo contrario la linea anadida quedaria
 *    pegada a la anterior. Para averiguarlo se lee ese ultimo byte con
 *    lseek(fd, -1, SEEK_END) + read().
 *
 * 2) El salto previo, el texto y el '\n' final se arman en un unico bufer de
 *    memoria dinamica y se emiten con UNA sola llamada a write(). Menos
 *    cambios de contexto a modo kernel y menos riesgo de dejar el archivo a
 *    medio escribir si algo falla en el intermedio.
 *
 * Alternativa considerada: abrir con O_APPEND haria innecesario el lseek final,
 * pero se prefirio el lseek explicito porque hace visible el concepto de
 * puntero de archivo, que es el objetivo pedagogico del taller.
 */
int editor_append(Editor *ed, const char *text)
{
    if (!require_open(ed)) return -1;

    size_t tlen    = strlen(text);
    int    need_nl = 0;

    if (ed->size > 0) {
        char last;
        if (ed_lseek(ed, -1, SEEK_END, "SEEK_END") == (off_t)-1) {
            perror("editor: lseek al inspeccionar el ultimo byte");
            return -1;
        }
        if (ed_read(ed, &last, 1) == -1) {
            perror("editor: read del ultimo byte");
            return -1;
        }
        if (last != '\n') need_nl = 1;
    }

    size_t blen = (size_t)need_nl + tlen + 1;   /* ['\n'] + texto + '\n' */
    char  *buf  = malloc(blen);
    if (buf == NULL) { perror("editor: malloc"); return -1; }

    size_t off = 0;
    if (need_nl) buf[off++] = '\n';
    memcpy(buf + off, text, tlen);
    off += tlen;
    buf[off] = '\n';

    if (ed_lseek(ed, 0, SEEK_END, "SEEK_END") == (off_t)-1) {
        perror("editor: lseek al final del archivo");
        free(buf);
        return -1;
    }
    if (ed_write(ed, buf, blen) == -1) {
        perror("editor: write al anadir");
        free(buf);
        return -1;
    }
    free(buf);                                  /* sin fugas: se libera siempre */

    if (index_build(ed) == -1) return -1;

    printf(C_OK "Linea %zu anadida" C_RESET C_INFO " (%zu bytes escritos, "
           "tamano actual %lld bytes)\n" C_RESET,
           ed->nlines, blen, (long long)ed->size);
    return 0;
}


/**
 * COMANDO 'i <n> <texto>' -- anadir una linea en la posicion n.
 *
 * Syscalls: lseek(2) con SEEK_END, read(2), write(2).
 *
 * Se resuelven dos detalles que suelen pasarse por alto:
 *
 * 1) Si el archivo no esta vacio y su ultimo byte NO es '\n', hay que insertar
 *    uno antes del texto nuevo; de lo contrario la linea anadida quedaria
 *    pegada a la anterior. Para averiguarlo se lee ese ultimo byte con
 *    lseek(fd, -1, SEEK_END) + read().
 *
 * 2) El salto previo, el texto y el '\n' final se arman en un unico bufer de
 *    memoria dinamica y se emiten con UNA sola llamada a write(). Menos
 *    cambios de contexto a modo kernel y menos riesgo de dejar el archivo a
 *    medio escribir si algo falla en el intermedio.
 *
 * Alternativa considerada: abrir con O_APPEND haria innecesario el lseek final,
 * pero se prefirio el lseek explicito porque hace visible el concepto de
 * puntero de archivo, que es el objetivo pedagogico del taller.
 */
int editor_insert(Editor *ed, long n, const char *text)
{
    if (!require_open(ed)) return -1;

    /* Si el archivo está vacío o se inserta tras la última línea, es lo mismo que el append */
    if (ed->nlines == 0 || (size_t)n > ed->nlines) {
        return editor_append(ed, text);
    }

    size_t tlen    = strlen(text);
    size_t ins_len = tlen + 1; /* texto + '\n' */

    off_t target_pos = ed->lines[n-1].start;

    /*Construimos  el bufer que se va a insertar*/
    char *ins_buf = malloc(ins_len);
    if(ins_buf == NULL){
        pererror("editor: malloc al insertar"); return -1;
    }
    memcpy(ins_buf, text, tlen);
    ins_buf[tlen] = '\n';

    /* Desplazamos la cola del archivo hacia la derecha*/
    off_t bytes_to_move = ed->size - target_pos;
    char buf[BUFSZ];

    while (bytes_to_move > 0) {
        size_t chunk = (bytes_to_move < (off_t)sizeof(buf)) 
                       ? (size_t)bytes_to_move : sizeof(buf);

        off_t read_pos = target_pos + bytes_to_move - chunk;
        off_t write_pos = read_pos + ins_len;

        if (ed_lseek(ed, read_pos, SEEK_SET, "SEEK_SET") == (off_t)-1) {
            perror("editor: lseek de lectura al insertar");
            free(ins_buf);
            return -1;
        }
        ssize_t r = ed_read(ed, buf, chunk);
        if (r == -1) { perror("editor: read al insertar"); free(ins_buf); return -1; }

        if (ed_lseek(ed, write_pos, SEEK_SET, "SEEK_SET") == (off_t)-1) {
            perror("editor: lseek de escritura al insertar");
            free(ins_buf);
            return -1;
        }
        if (ed_write(ed, buf, (size_t)r) == -1) {
            perror("editor: write al insertar");
            free(ins_buf);
            return -1;
        }

        bytes_to_move -= chunk;
    }


    /*Escribimos la nueva linea en la posicion que liberamos*/
    if (ed_lseek(ed, target_pos, SEEK_SET, "SEEK_SET") == (off_t)-1) {
        perror("editor: lseek al escribir nueva linea");
        free(ins_buf);
        return -1;
    }

    if (ed_write(ed, ins_buf, ins_len) == -1) {
        perror("editor: write nueva linea");
        free(ins_buf);
        return -1;
    }

    free(ins_buf);

    if (index_build(ed) == -1) return -1;

    printf(C_OK "Linea insertada en la posicion %ld" C_RESET C_INFO " (%zu bytes escritos, "
           "tamano actual %lld bytes)\n" C_RESET,
           n, ins_len, (long long)ed->size);

    return 0;

}
/**
 * COMANDO 'd <n>' -- borrar la linea n.
 *
 * Syscalls: lseek(2), read(2), write(2), ftruncate(2).
 *
 * Los sistemas de archivos POSIX no permiten "sacar" bytes del medio de un
 * archivo: no existe una operacion de eliminacion en el interior. El
 * procedimiento correcto es de dos fases:
 *
 *   FASE 1 (desplazamiento): copiar toda la cola del archivo 'gap' bytes hacia
 *   atras, usando dos punteros logicos (lectura y escritura) que avanzan en
 *   paralelo bloque a bloque. Como solo hay un descriptor, cada iteracion
 *   necesita dos lseek: uno para leer y otro para escribir.
 *
 *   FASE 2 (recorte): ftruncate() elimina los 'gap' bytes sobrantes que
 *   quedaron duplicados al final.
 *
 * Caso borde: al borrar la ultima linea no hay cola que desplazar, el bucle
 * simplemente no se ejecuta y basta con el ftruncate.
 *
 * Alternativa considerada: pread(2)/pwrite(2) leen y escriben en un
 * desplazamiento dado sin mover el puntero, ahorrando la mitad de los lseek.
 * Se conservo lseek por ser el que el enunciado pide demostrar.
 */
int editor_delete(Editor *ed, long n)
{
    if (!require_open(ed)) return -1;

    if (ed->nlines == 0) {
        fprintf(stderr, C_ERR "editor: el archivo esta vacio, no hay nada que borrar.\n" C_RESET);
        return -1;
    }
    if (n < 1 || (size_t)n > ed->nlines) {
        fprintf(stderr, C_ERR "editor: la linea %ld no existe "
                        "(el archivo tiene %zu lineas).\n" C_RESET, n, ed->nlines);
        return -1;
    }

    off_t start = ed->lines[n - 1].start;
    off_t end   = ed->lines[n - 1].end;
    off_t gap   = end - start;             /* bytes que van a desaparecer */
    char  buf[BUFSZ];

    /* --- FASE 1: desplazar la cola hacia atras --- */
    off_t rp = end;      /* desde donde se lee   */
    off_t wp = start;    /* hacia donde se escribe */

    while (rp < ed->size) {
        off_t  remaining = ed->size - rp;
        size_t want = (remaining < (off_t)sizeof buf)
                        ? (size_t)remaining : sizeof buf;

        if (ed_lseek(ed, rp, SEEK_SET, "SEEK_SET") == (off_t)-1) {
            perror("editor: lseek de lectura al borrar");
            return -1;
        }
        ssize_t r = ed_read(ed, buf, want);
        if (r == -1) { perror("editor: read al borrar"); return -1; }
        if (r == 0)  break;

        if (ed_lseek(ed, wp, SEEK_SET, "SEEK_SET") == (off_t)-1) {
            perror("editor: lseek de escritura al borrar");
            return -1;
        }
        if (ed_write(ed, buf, (size_t)r) == -1) {
            perror("editor: write al borrar");
            return -1;
        }
        rp += r;
        wp += r;
    }

    /* --- FASE 2: recortar el sobrante --- */
    off_t newsize = ed->size - gap;
    TRACE(ed, "ftruncate", "%d, %lld", ed->fd, (long long)newsize);
    if (ftruncate(ed->fd, newsize) == -1) {
        int saved = errno;
        TRACE_ERR(ed);
        errno = saved;
        perror("editor: ftruncate");
        return -1;
    }
    TRACE_OK(ed, 0);

    ed->size = newsize;
    if (index_build(ed) == -1) return -1;

    printf(C_OK "Linea %ld borrada" C_RESET C_INFO " (%lld bytes eliminados, "
           "quedan %zu lineas / %lld bytes)\n" C_RESET,
           n, (long long)gap, ed->nlines, (long long)ed->size);
    return 0;
}

/**
 * COMANDO 'q' -- cerrar y liberar.
 *
 * Syscall: close(2).
 *
 * Libera absolutamente toda la memoria dinamica (nombre del archivo e indice de
 * lineas) y deja la estructura en un estado consistente, de modo que llamarla
 * dos veces seguidas es seguro (idempotente).
 */
void editor_close(Editor *ed)
{
    if (ed->fd != -1) {
        TRACE(ed, "close", "%d", ed->fd);
        if (close(ed->fd) == -1) {
            int saved = errno;
            TRACE_ERR(ed);
            errno = saved;
            perror("editor: close");
        } else {
            TRACE_OK(ed, 0);
        }
        ed->fd = -1;
    }

    free(ed->path);            /* free(NULL) es seguro segun el estandar C */
    free(ed->lines);
    ed->path   = NULL;
    ed->lines  = NULL;
    ed->nlines = 0;
    ed->cap    = 0;
    ed->size   = 0;
}

/* ==========================================================================
 * SECCION 4: CICLO INTERACTIVO (REPL)
 * ========================================================================== */

static void editor_help(void)
{
    printf(C_TITLE "\n--- Editor de texto CLI (comandos) ---\n" C_RESET);
    printf("  " C_PROMPT "o <archivo>" C_RESET "  Abre el archivo; si no existe lo crea.\n");
    printf("  " C_PROMPT "p [n]" C_RESET "        Imprime la linea n. Sin numero, todo el archivo.\n");
    printf("  " C_PROMPT "a <texto>" C_RESET "    Anade el texto como nueva linea al final.\n");
    printf("  " C_PROMPT "ins <n> <texto>" C_RESET "   Inserta el texto en la linea n especificada.\n");
    printf("  " C_PROMPT "d <n>" C_RESET "        Borra la linea n.\n");
    printf("  " C_PROMPT "i" C_RESET "            Muestra el estado interno del editor.\n");
    printf("  " C_PROMPT "t" C_RESET "            Activa o desactiva la traza de syscalls.\n");
    printf("  " C_PROMPT "h" C_RESET "            Muestra esta ayuda.\n");
    printf("  " C_PROMPT "q" C_RESET "            Cierra el archivo y sale del editor.\n");
    printf(C_INFO "  (Ctrl+D equivale a 'q')\n\n" C_RESET);
}

static void editor_info(const Editor *ed)
{
    printf(C_TITLE "--- Estado del editor ---\n" C_RESET);
    if (ed->fd == -1) {
        printf("  Sin archivo abierto.\n");
    } else {
        printf("  Archivo:            " C_OK "%s" C_RESET "\n", ed->path);
        printf("  Descriptor (fd):    %d\n", ed->fd);
        printf("  Tamano:             %lld bytes\n", (long long)ed->size);
        printf("  Lineas indexadas:   %zu\n", ed->nlines);
        printf("  Capacidad indice:   %zu (%zu bytes de heap)\n",
               ed->cap, ed->cap * sizeof(Line));
    }
    printf(C_TITLE "-------------------------\n" C_RESET);
}

/* Convierte un texto a numero de linea validando que sea realmente un entero.
 * Retorna 0 si el texto esta vacio (equivale a "sin argumento") y -1 si es
 * invalido. */
static long parse_line_number(const char *s, int *ok)
{
    char *endp;
    *ok = 1;

    while (*s == ' ' || *s == '\t') s++;
    if (*s == '\0') return 0;                       /* sin argumento */

    errno = 0;
    long v = strtol(s, &endp, 10);
    while (*endp == ' ' || *endp == '\t') endp++;

    if (endp == s || *endp != '\0' || errno == ERANGE) {
        *ok = 0;
        return 0;
    }
    return v;
}

/* Elimina espacios al inicio y al final (usado para nombres de archivo). */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    size_t l = strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t')) s[--l] = '\0';
    return s;
}

int editor_repl(const char *path, int trace)
{
    Editor ed;
    char   line[MAX_LINE];

    editor_init(&ed, trace);

    printf(C_TITLE "\n=== Editor de texto CLI (syscalls POSIX) ===\n" C_RESET);
    printf(C_INFO "Escribe 'h' para ver los comandos disponibles.\n\n" C_RESET);

    if (path != NULL && editor_open(&ed, path) == -1) {
        editor_close(&ed);
        return 1;
    }

    for (;;) {
        printf(C_PROMPT "edit%s%s> " C_RESET,
               ed.path ? ":" : "", ed.path ? ed.path : "");
        fflush(stdout);   /* el prompt no lleva '\n', hay que forzar el vaciado */

        /* fgets sobre STDIN esta permitido de forma explicita por el enunciado:
         * la restriccion aplica a la manipulacion del archivo, no al dialogo. */
        if (fgets(line, sizeof line, stdin) == NULL) {
            printf("\n" C_INFO "(fin de entrada)\n" C_RESET);
            break;
        }

        size_t l = strlen(line);
        if (l > 0 && line[l - 1] == '\n') {
            line[l - 1] = '\0';                       /* quitar el salto final */
        } else if (l == sizeof line - 1) {
            /* La linea excedio el bufer: se descarta el resto para que no se
             * interprete como un comando nuevo en la siguiente vuelta. */
            int c;
            while ((c = getchar()) != '\n' && c != EOF) { }
            fprintf(stderr, C_ERR "editor: linea demasiado larga, se ignora.\n" C_RESET);
            continue;
        }

        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') continue;                       /* linea vacia */

        char  cmd  = *p;
        char *rest = p + 1;

        /* Los comandos son de una sola letra: lo que sigue debe ser un espacio
         * o el fin de la linea. Asi 'oo' no se confunde con 'o'. */
        if (*rest != '\0' && *rest != ' ' && *rest != '\t') {
            fprintf(stderr, C_ERR "editor: comando '%s' no reconocido. "
                            "Escribe 'h' para la ayuda.\n" C_RESET, p);
            continue;
        }

        int  ok;
        long n;

        switch (cmd) {
        case 'o': {
            char *fname = trim(rest);
            if (*fname == '\0') {
                fprintf(stderr, C_ERR "Uso: o <archivo>\n" C_RESET);
                break;
            }
            editor_open(&ed, fname);
            break;
        }
        case 'p':
            n = parse_line_number(rest, &ok);
            if (!ok) { fprintf(stderr, C_ERR "Uso: p [n]  (n debe ser un entero)\n" C_RESET); break; }
            editor_print(&ed, n);
            break;

        case 'a':
            /* Se salta UN solo espacio separador; el resto del texto se toma de
             * forma literal para no perder la indentacion que escriba el usuario. */
            if (*rest == ' ') rest++;
            editor_append(&ed, rest);
            break;

        case 'd':
            n = parse_line_number(rest, &ok);
            if (!ok || n == 0) { fprintf(stderr, C_ERR "Uso: d <n>  (n debe ser un entero positivo)\n" C_RESET); break; }
            editor_delete(&ed, n);
            break;

        case 'i':
            editor_info(&ed);
            break;

        case 't':
            ed.trace = !ed.trace;
            printf(C_INFO "Traza de syscalls: %s\n" C_RESET,
                   ed.trace ? "ACTIVADA" : "DESACTIVADA");
            break;

        case 'h':
            editor_help();
            break;

        case 'q':
            editor_close(&ed);
            printf(C_INFO "Editor cerrado. Recursos liberados.\n" C_RESET);
            return 0;

        default:
            fprintf(stderr, C_ERR "editor: comando '%c' no reconocido. "
                            "Escribe 'h' para la ayuda.\n" C_RESET, cmd);
            break;
        }
    }

    /* Salida por Ctrl+D: hay que liberar igual que en 'q'. */
    editor_close(&ed);
    return 0;
}
