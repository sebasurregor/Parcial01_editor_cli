# =====================================================================================
# Makefile - Shell Educativo de Syscalls + Editor de Texto CLI
# Universidad EAFIT - Sistemas Operativos - C2666-SI2004-4328
# Taller 01: Desarrollo de un Editor de Texto en Unix
# =====================================================================================
#
# Targets disponibles:
#   make          Compila los dos binarios (equivale a 'make all')
#   make run      Compila y ejecuta el shell integrado
#   make editor   Compila solo el editor autonomo
#   make test     Compila y ejecuta el script de pruebas
#   make valgrind Ejecuta el editor bajo valgrind para verificar fugas de memoria
#   make clean    Elimina binarios, objetos y archivos temporales de prueba
#
# Los binarios quedan en bin/ y los objetos intermedios en build/, de modo que el
# arbol de fuentes se mantiene limpio.
# =====================================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -std=gnu99 -g -D_GNU_SOURCE -Iinclude

# NOTA: no se usa una variable llamada SHELL. En GNU Make esa variable es especial
# (define el interprete con el que se ejecutan las recetas) y sobrescribirla romperia
# la compilacion. De ahi los nombres BIN_SHELL / BIN_EDITOR.
BIN_DIR   = bin
OBJ_DIR   = build
BIN_SHELL  = $(BIN_DIR)/eafitOS
BIN_EDITOR = $(BIN_DIR)/editor

# El motor del editor (editor.o) se enlaza en AMBOS binarios. Es la evidencia
# practica de que no depende del shell.
SRCS_SHELL  = src/shell/main.c src/shell/cat_datos.c src/shell/cat_memoria.c \
              src/shell/cat_monitoreo.c src/shell/cat_util.c src/shell/cat_edicion.c \
              src/editor/editor.c
SRCS_EDITOR = src/editor/editor_main.c src/editor/editor.c

OBJS_SHELL  = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS_SHELL))
OBJS_EDITOR = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS_EDITOR))

all: $(BIN_SHELL) $(BIN_EDITOR)

editor: $(BIN_EDITOR)

$(BIN_SHELL): $(OBJS_SHELL) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BIN_EDITOR): $(OBJS_EDITOR) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

# Regla generica: cada .c de src/ produce su .o en build/, respetando subcarpetas
$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Dependencias de cabeceras: si cambia un .h se recompila lo que lo usa
$(OBJ_DIR)/shell/main.o $(OBJ_DIR)/shell/cat_datos.o $(OBJ_DIR)/shell/cat_memoria.o \
$(OBJ_DIR)/shell/cat_monitoreo.o $(OBJ_DIR)/shell/cat_util.o: include/shell.h
$(OBJ_DIR)/shell/cat_edicion.o: include/shell.h include/editor.h
$(OBJ_DIR)/editor/editor.o $(OBJ_DIR)/editor/editor_main.o: include/editor.h

# Compilar y ejecutar quedan separados a proposito: 'make' no debe lanzar el
# programa, porque impediria usarlo dentro de un script o de un pipeline.
run: $(BIN_SHELL)
	./$(BIN_SHELL)

test: $(BIN_EDITOR)
	./tests/test_editor.sh

valgrind: $(BIN_EDITOR)
	@mkdir -p pruebas_editor
	@printf 'o pruebas_editor/vg.txt\na linea uno\na linea dos\np\nd 1\np\nq\n' | \
	 valgrind --leak-check=full --show-leak-kinds=all ./$(BIN_EDITOR) -q > /dev/null

# Archivos de demostracion que la guia de verificacion pide crear a mano en la
# raiz del proyecto. Se listan uno por uno, en lugar de usar un comodin como
# *.txt, para no borrar por accidente ningun archivo del usuario.
DEMO_FILES = demo.txt desde_shell.txt verif.txt prueba_trunc.txt prueba_nl.txt

clean:
	rm -rf $(BIN_DIR) $(OBJ_DIR) pruebas_editor
	rm -f $(DEMO_FILES)

.PHONY: all editor run test valgrind clean
