#include "shell.h"
#include "editor.h"

/**
 * ====================================================================================
 * CATEGORIA: EDICION  --  COMANDO: edit [archivo]
 * ====================================================================================
 *
 * JUSTIFICACION DE LA CATEGORIA NUEVA (decision arquitectonica del taller)
 * -----------------------------------------------------------------------
 * Las cuatro categorias que ya existian en el shell estan organizadas por
 * subsistema del kernel:
 *
 *      datos       -> sistema de archivos      (open, read, write, stat)
 *      memoria     -> gestion de memoria virtual (sbrk, mmap)
 *      monitoreo   -> procesos y senales       (fork, execvp, kill, getrusage)
 *      utilidades  -> miscelaneos              (getuid, time)
 *
 * Todas comparten una invariante de diseno: cada comando es ATOMICO y SIN
 * ESTADO. Se ejecuta, imprime la traza de sus syscalls, retorna, y el control
 * vuelve de inmediato al prompt 'eafitOS>'. Ningun comando conserva un
 * descriptor abierto entre invocaciones ni toma posesion de la entrada estandar.
 *
 * El editor rompe esa invariante en dos puntos simultaneos:
 *
 *   1. Abre un ciclo REPL ANIDADO: mientras esta activo, es el editor y no el
 *      shell quien consume STDIN.
 *   2. Mantiene ESTADO PERSISTENTE entre comandos: un file descriptor vivo y un
 *      indice de lineas en memoria dinamica.
 *
 * Clasificarlo dentro de 'datos' habria sido tematicamente defendible (al fin y
 * al cabo manipula archivos), pero habria mezclado dos naturalezas distintas:
 * demostraciones puntuales de una syscall frente a una subaplicacion completa.
 * Ademas habria degradado la utilidad pedagogica de 'help datos', donde hoy cada
 * entrada se explica en una sola linea.
 *
 * Por eso se crea la categoria 'edicion', definida como "aplicaciones
 * interactivas construidas sobre syscalls", abierta a futuras herramientas del
 * mismo tipo (un visor tipo 'less', un 'hexdump' interactivo, etc.).
 *
 * ALTERNATIVA DESCARTADA
 * ----------------------
 * Tambien se evaluo ejecutar el editor como proceso externo con fork(2) +
 * execvp(3), reutilizando 'p_exec'. Ofrece aislamiento (un fallo del editor no
 * tumbaria el shell) pero convierte al editor en un binario cualquiera y no en
 * una funcionalidad integrada, ademas de depender de la ruta del ejecutable. Se
 * eligio la integracion como comando interno; aun asi, como editor.c no depende
 * de shell.h, esa alternativa sigue disponible mediante:
 *
 *      eafitOS> p_exec ./editor archivo.txt
 */
int cmd_edit(int argc, char **argv)
{
    if (argc > 2) {
        fprintf(stderr, COLOR_ERROR "Uso: edit [archivo]\n" COLOR_RESET);
        return 1;
    }

    const char *path = (argc == 2) ? argv[1] : NULL;

    printf(COLOR_INFO "Cediendo el control de STDIN al editor. "
           "Escribe 'q' dentro del editor para volver al shell.\n" COLOR_RESET);

    /* El segundo parametro activa la traza de syscalls, para que el editor
     * conserve el estilo pedagogico del resto del shell. */
    int result = editor_repl(path, 1);

    printf(COLOR_INFO "De vuelta en el shell. El proceso nunca termino: el editor "
           "solo retorno de su ciclo REPL.\n" COLOR_RESET);

    return result;
}
