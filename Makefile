# Definicja kompilatora
CC = gcc

# Flagi kompilatora zgodne z wymaganiami zadania
CFLAGS = -Wall -Wextra -Wno-implicit-fallthrough -std=gnu17 -fPIC -O2

# Flagi linkera do tworzenia biblioteki dzielonej i przechwytywania funkcji pamięci
LDFLAGS = -shared -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=reallocarray \
          -Wl,--wrap=free -Wl,--wrap=strdup -Wl,--wrap=strndup

# Pliki źródłowe, które mają zostać skompilowane
SRCS = ma.c memory_tests.c

# Automatyczne generowanie listy plików obiektowych (.o) na podstawie listy plików źródłowych (.c)
OBJS = $(SRCS:.c=.o)

# Nazwa docelowej biblioteki
TARGET = libma.so

# Deklaracja pseudoceli, które nie są nazwami plików
# Umożliwia działanie `make libma.so` zgodnie z poleceniem
.PHONY: all clean libma.so

# Cel domyślny, uruchamiany po wpisaniu samego `make`
all: $(TARGET)

# Reguła budowania biblioteki `libma.so`
# Zależność: pliki obiektowe (.o)
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

# Reguła wzorcowa do kompilacji plików .c na pliki .o
# Jest wywoływana automatycznie dla każdej zależności .o
# $< to nazwa pierwszej zależności (pliku .c)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Cel `clean` do usuwania plików wygenerowanych przez make
clean:
	rm -f $(OBJS) $(TARGET)

