#include "shell.h"
#include <unistd.h>     /* read, close, sbrk                                    */
#include <fcntl.h>      /* open, O_RDONLY                                       */
#include <sys/mman.h>   /* mmap, munmap                                         */
#include <errno.h>      /* errno                                                */
#include <string.h>     /* memchr, strncmp, strerror                            */
#include <stdio.h>      /* printf: SOLO para la salida por consola, nunca para  */
#include <stdlib.h>     /* acceder a archivos (malloc, realloc, free)           */

/**
 * ====================================================================================
 * COMANDO: m_sbrk <incremento_bytes>
 * ====================================================================================
 * Demuestra el crecimiento de la memoria dinámica (heap) del proceso.
 * 
 * Explicación teórica:
 * Tradicionalmente, la memoria dinámica del heap en Unix tiene una frontera llamada
 * "program break". Detrás de esta dirección se encuentra la memoria física asignada al heap.
 * 
 * Syscalls explicadas:
 * 1. sbrk(2): Modifica el límite del segmento de datos del proceso (program break).
 *    - sbrk(0): Devuelve la dirección actual del program break sin modificarla.
 *    - sbrk(increment): Suma 'increment' bytes al program break actual, reservando
 *      físicamente memoria en el heap o devolviéndola al kernel si es negativo.
 *      Devuelve la dirección del program break *anterior*.
 */
int cmd_m_sbrk(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: m_sbrk <incremento_bytes>\n" COLOR_RESET);
        return 1;
    }
    long increment = strtol(argv[1], NULL, 10);

    /* 1. Obtener la dirección actual del program break sin cambiar nada */
    LOG_SYSCALL("sbrk", "0");
    void *current_break = sbrk(0);
    if (current_break == (void *)-1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(current_break);

    /* 2. Modificar la dirección de corte (program break) */
    LOG_SYSCALL("sbrk", "%ld", increment);
    void *new_break = sbrk(increment);
    if (new_break == (void *)-1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(new_break); /* Retorna el break de antes de aplicar el incremento */

    /* 3. Obtener el nuevo program break para confirmar que se aplicó el incremento */
    LOG_SYSCALL("sbrk", "0");
    void *confirmed_break = sbrk(0);
    if (confirmed_break == (void *)-1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(confirmed_break);

    printf(COLOR_RESULT "Dirección anterior del break: %p\n" COLOR_RESET, current_break);
    printf(COLOR_RESULT "Dirección nueva del break:    %p\n" COLOR_RESET, confirmed_break);
    printf(COLOR_INFO "El heap cambió en %ld bytes.\n" COLOR_RESET, (long)((char *)confirmed_break - (char *)current_break));

    return 0;
}

/**
 * ====================================================================================
 * COMANDO: m_mmap <tamaño_bytes>
 * ====================================================================================
 * Demuestra la reserva directa de páginas de memoria virtual anónima.
 * 
 * Explicación teórica:
 * Para reservas de memoria grandes (usualmente > 128KB en malloc), el sistema operativo
 * prefiere usar mmap en lugar de sbrk para mapear páginas físicas completas de forma anónima
 * y evitar la fragmentación en el heap.
 * 
 * Syscalls explicadas:
 * 1. mmap(2): Mapea archivos o dispositivos en memoria virtual, o mapea memoria en blanco (anónima).
 *    - addr = NULL: El kernel elige la dirección virtual de alineación óptima.
 *    - size: Cantidad de bytes a reservar (se redondea al tamaño de página del sistema, e.g. 4KB).
 *    - PROT_READ|PROT_WRITE: Permisos de protección (Lectura y Escritura permitidas en las páginas).
 *    - MAP_PRIVATE: Los cambios no se comparten con otros procesos.
 *    - MAP_ANONYMOUS: No hay archivo físico de respaldo. La memoria se inicializa en 0.
 *    - fd = -1, offset = 0: Requerido cuando se usa mapeo anónimo.
 * 2. munmap(2): Libera las páginas virtuales mapeadas devolviéndolas al kernel.
 */
int cmd_m_mmap(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, COLOR_ERROR "Uso: m_mmap <tamaño_bytes>\n" COLOR_RESET);
        return 1;
    }
    size_t size = (size_t)strtoul(argv[1], NULL, 10);
    if (size == 0) {
        fprintf(stderr, COLOR_ERROR "El tamaño debe ser mayor que 0.\n" COLOR_RESET);
        return 1;
    }

    /* 1. Mapear memoria virtual */
    LOG_SYSCALL("mmap", "NULL, %zu, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0", size);
    void *addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT_PTR(addr);

    printf(COLOR_INFO "Memoria mapeada en la dirección: %p\n" COLOR_RESET, addr);
    printf(COLOR_INFO "Escribiendo patrón en la memoria mapeada...\n" COLOR_RESET);
    
    /* Escribir un patrón en las páginas mapeadas para verificar que se puede leer/escribir físicamente */
    char *ptr = (char *)addr;
    for (size_t i = 0; i < size - 1; i++) {
        ptr[i] = 'A' + (i % 26);
    }
    ptr[size - 1] = '\0';

    if (size > 50) {
        printf(COLOR_INFO "Muestra del contenido (primeros 40 caracteres): %.40s...\n" COLOR_RESET, ptr);
    } else {
        printf(COLOR_INFO "Contenido: %s\n" COLOR_RESET, ptr);
    }

    /* 2. Desmapear (liberar) la memoria mapeada */
    LOG_SYSCALL("munmap", "%p, %zu", addr, size);
    int res = munmap(addr, size);
    if (res == -1) {
        LOG_SYSCALL_ERROR(strerror(errno));
        return 1;
    }
    LOG_SYSCALL_RESULT(res);

    printf(COLOR_RESULT "Memoria desmapeada exitosamente.\n" COLOR_RESET);
    return 0;
}

/**
 * ====================================================================================
 * LECTOR DE ARCHIVOS VIRTUALES DEL KERNEL (solo llamadas al sistema)
 * ====================================================================================
 * Lee por completo un archivo del sistema de ficheros virtual /proc usando
 * exclusivamente open(2), read(2) y close(2). Devuelve un bloque de memoria
 * dinamica terminado en '\0' que quien llama debe liberar con free().
 *
 * Por que hace falta un bucle de lectura y no basta con un solo read():
 *
 *   1. Los archivos de /proc no existen en disco: el kernel los genera al
 *      vuelo. Por eso stat(2) informa un tamano de 0 bytes y no se puede
 *      reservar de antemano el bloque exacto; hay que leer hasta que read(2)
 *      devuelva 0, que es la senal de fin de archivo.
 *   2. read(2) puede devolver MENOS bytes de los solicitados sin que eso sea un
 *      error, y puede ser interrumpida por una senal retornando -1 con
 *      errno == EINTR. Ambos casos se manejan aqui.
 *
 * Este es exactamente el trabajo que fgets(3) hacia por nosotros a cambio de
 * ocultar el mecanismo; hacerlo a mano es el objetivo pedagogico del ejercicio.
 */
#define BLOQUE_LECTURA 1024

static char *proc_leer_completo(const char *ruta, size_t *longitud)
{
    /* 1. LLAMADA AL SISTEMA: open */
    LOG_SYSCALL("open", "\"%s\", O_RDONLY", ruta);
    int fd = open(ruta, O_RDONLY);
    if (fd == -1) {
        int guardado = errno;
        LOG_SYSCALL_ERROR(strerror(errno));
        errno = guardado;
        perror("m_info: open");
        return NULL;
    }
    LOG_SYSCALL_RESULT(fd);

    size_t capacidad = BLOQUE_LECTURA * 2;
    size_t usados    = 0;
    char  *buffer    = malloc(capacidad);
    if (buffer == NULL) {
        perror("m_info: malloc");
        close(fd);
        return NULL;
    }

    /* 2. LLAMADA AL SISTEMA: read (en bucle hasta el fin de archivo) */
    for (;;) {
        /* Se reserva siempre un byte extra para el '\0' terminador */
        while (capacidad - usados < BLOQUE_LECTURA + 1) {
            size_t nueva = capacidad * 2;
            char  *tmp   = realloc(buffer, nueva);
            /* Se asigna a una variable temporal: si realloc falla devuelve NULL
             * y asignarlo directo a 'buffer' perderia el bloque anterior. */
            if (tmp == NULL) {
                perror("m_info: realloc");
                free(buffer);
                close(fd);
                return NULL;
            }
            buffer    = tmp;
            capacidad = nueva;
        }

        LOG_SYSCALL("read", "%d, buffer+%zu, %d", fd, usados, BLOQUE_LECTURA);
        ssize_t leidos = read(fd, buffer + usados, BLOQUE_LECTURA);

        if (leidos == -1) {
            if (errno == EINTR) {   /* interrumpida por una senal: reintentar */
                printf("= " COLOR_ERROR "EINTR" COLOR_RESET " (reintentando)\n");
                continue;
            }
            int guardado = errno;
            LOG_SYSCALL_ERROR(strerror(errno));
            errno = guardado;
            perror("m_info: read");
            free(buffer);
            close(fd);
            return NULL;
        }
        LOG_SYSCALL_RESULT(leidos);

        if (leidos == 0) break;              /* fin de archivo alcanzado */
        usados += (size_t)leidos;
    }

    buffer[usados] = '\0';

    /* 3. LLAMADA AL SISTEMA: close */
    LOG_SYSCALL("close", "%d", fd);
    if (close(fd) == -1) {
        int guardado = errno;
        LOG_SYSCALL_ERROR(strerror(errno));
        errno = guardado;
        perror("m_info: close");
    } else {
        LOG_SYSCALL_RESULT(0);
    }

    if (longitud) *longitud = usados;
    return buffer;
}

/* Indica si una linea de /proc/self/status es una de las estadisticas de
 * memoria que nos interesa mostrar. Recibe la longitud explicita porque la
 * linea NO esta terminada en '\0': es una porcion del bloque leido. */
static int es_linea_de_interes(const char *linea, size_t largo)
{
    static const char *claves[] = {
        "VmPeak:",  /* Pico de memoria virtual ocupada                          */
        "VmSize:",  /* Memoria virtual total asignada                           */
        "VmLck:",   /* Paginas bloqueadas fisicamente en RAM (mlock)            */
        "VmHWM:",   /* Pico de memoria fisica residente (High Water Mark)       */
        "VmRSS:",   /* Memoria fisica residente actualmente ocupada en RAM      */
        "VmData:",  /* Tamano del segmento de datos (heap)                      */
        "VmStk:",   /* Tamano del segmento de pila (stack)                      */
        "VmExe:",   /* Tamano del segmento de codigo de texto                   */
        "VmLib:",   /* Memoria de las librerias compartidas asociadas           */
        "VmPTE:"    /* Tamano de las tablas de paginas de traduccion            */
    };
    const size_t total = sizeof(claves) / sizeof(claves[0]);

    for (size_t i = 0; i < total; i++) {
        size_t n = strlen(claves[i]);
        if (largo >= n && strncmp(linea, claves[i], n) == 0) return 1;
    }
    return 0;
}

/**
 * ====================================================================================
 * COMANDO: m_info
 * ====================================================================================
 * Lee estadisticas del estado del mapa de memoria del proceso.
 *
 * Explicacion teorica:
 * El sistema de archivos virtual /proc en Linux expone informacion del kernel como
 * si fueran ficheros. `/proc/self/status` describe el estado del proceso actual
 * (self). Leerlo consulta dinamicamente los metadatos del espacio de direcciones.
 *
 * Syscalls explicadas:
 * 1. open(2):  abre el archivo virtual generado por el kernel.
 * 2. read(2):  copia los bytes generados al vuelo hacia un bufer en RAM.
 * 3. close(2): libera la entrada en la tabla de descriptores del proceso.
 *
 * Nota sobre la restriccion de E/S del taller: este comando NO usa fopen, fgets
 * ni fclose. El troceado en lineas, que antes hacia fgets(3), se realiza aqui a
 * mano recorriendo el bufer con memchr(3), que opera sobre memoria ya leida y no
 * sobre el archivo.
 */
int cmd_m_info(int argc, char **argv) {
    (void)argc;
    (void)argv;

    size_t longitud = 0;
    char  *contenido = proc_leer_completo("/proc/self/status", &longitud);
    if (contenido == NULL) return 1;

    printf(COLOR_TITLE "--- Estado de Memoria del Shell (/proc/self/status) ---\n" COLOR_RESET);

    /* Troceado manual en lineas. Se usa memchr y no strchr para trabajar sobre
     * una cantidad explicita de bytes: asi el recorrido es seguro incluso si el
     * archivo contuviera un byte nulo intercalado. */
    const char *inicio = contenido;
    const char *fin    = contenido + longitud;

    while (inicio < fin) {
        const char *salto = memchr(inicio, '\n', (size_t)(fin - inicio));
        size_t largo = salto ? (size_t)(salto - inicio) : (size_t)(fin - inicio);

        if (es_linea_de_interes(inicio, largo)) {
            /* %.*s imprime exactamente 'largo' bytes, sin depender de un '\0' */
            printf("  %.*s\n", (int)largo, inicio);
        }

        if (salto == NULL) break;
        inicio = salto + 1;
    }

    printf(COLOR_TITLE "--------------------------------------------------------\n" COLOR_RESET);

    free(contenido);   /* sin fugas: el bufer se libera siempre */
    return 0;
}
