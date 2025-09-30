# Biblioteka Automatów Moore'a (libma)

🇵🇱 **Biblioteka w języku C do symulacji skończonych automatów Moore'a**

## 📖 Opis

Biblioteka `libma` implementuje kompletną symulację skończonych automatów Moore'a w języku C. Automaty Moore'a to typ skończonych automatów stanów, gdzie sygnał wyjściowy zależy tylko od aktualnego stanu automatu, nie od sygnałów wejściowych.

### 🎯 Cechy biblioteki:

- **✅ Pełna implementacja automatów Moore'a** - z funkcjami przejść i wyjść
- **🔗 Łączenie automatów** - możliwość tworzenia złożonych sieci automatów
- **⚡ Synchroniczna symulacja** - wszystkie automaty działają równolegle
- **🛡️ Bezpieczna pamięć** - automatyczne zarządzanie pamięcią z magic numbers
- **📏 Skalowalna architektura** - obsługa automatów o dowolnej liczbie stanów
- **🧪 Gotowa do testów** - czyste API z obsługą błędów

## 🚀 Szybki start

### 1. Podstawowe użycie
```c
#include <ma.h>

// Funkcja przejść (przykład: licznik binarny)
void transition_counter(uint64_t *next_state, const uint64_t *input,
const uint64_t *state, size_t n, size_t s) {
// Implementacja przejść
*next_state = (*state + 1) % (1ULL << s);
}

// Główna funkcja
int main() {
    // Utwórz automat: 2 wejścia, 3 bity stanu
    moore_t *counter = ma_create_simple(2, 3, transition_counter);
    if (!counter) {
        perror("Błąd tworzenia automatu");
        return 1;
    }
    // Wykonaj kilka kroków symulacji
    moore_t *automata[] = {counter};
    for (int i = 0; i < 8; i++) {
        printf("Stan %d: %lu\n", i, ma_get_output(counter));
        ma_step(automata, 1);
    }

    // Zwolnij pamięć
    ma_delete(counter);
    return 0;
}
```
### 2. Kompilacja programu
``` bash
gcc -o program program.c -lma
```

## 📚 API Reference

### 🏗️ Tworzenie automatów
```c
// Tworzenie automatu z pełną parametryzacją
moore_t *ma_create_full(size_t n, size_t m, size_t s,
transition_function_t t, output_function_t y,
const uint64_t *q);

// Tworzenie prostego automatu (wyjście = stan)
moore_t *ma_create_simple(size_t n, size_t s, transition_function_t t);

// Usuwanie automatu
void ma_delete(moore_t *a);
```
### 🔗 Łączenie automatów
```c
// Połączenie wyjść z wejściami
int ma_connect(moore_t *a_in, size_t in, moore_t *a_out, size_t out, size_t num);

// Rozłączenie
int ma_disconnect(moore_t *a_in, size_t in, size_t num);
```

### ⚙️ Kontrola automatów

```c
// Ustawienie sygnałów wejściowych
int ma_set_input(moore_t *a, const uint64_t *input);

// Ustawienie stanu
int ma_set_state(moore_t *a, const uint64_t *state);

// Odczytanie wyjścia
const uint64_t *ma_get_output(const moore_t *a);

```

### 🎬 Symulacja

```c 
// Wykonanie jednego kroku dla wszystkich automatów
int ma_step(moore_t *at[], size_t num);
```

## 🎓 Przykłady

### Prosty licznik

```c 
#include <stdio.h>

void counter_transition(uint64_t *next_state, const uint64_t *input,
const uint64_t *state, size_t n, size_t s) {
    // Licznik modulo 2^s
    *next_state = (*state + 1) % (1ULL << s);
}

int main() {
    moore_t *counter = ma_create_simple(0, 4, counter_transition); // 4-bitowy licznik
    if (!counter) return 1;

    moore_t *automata[] = {counter};

    printf("Sekwencja licznika 4-bitowego:\n");
    for (int i = 0; i < 20; i++) {
        printf("Krok %2d: %lu\n", i, ma_get_output(counter));
        ma_step(automata, 1);
    }

    ma_delete(counter);
        return 0;
}
```

### Połączone automaty
```c 
#include <ma.h>
#include <stdio.h>

// Generator impulsów co 4 takty
void pulse_generator(uint64_t *next_state, const uint64_t *input,
const uint64_t *state, size_t n, size_t s) {
    *next_state = (*state + 1) % 4;
}

void pulse_output(uint64_t *output, const uint64_t *state, size_t m, size_t s) {
    *output = (*state == 3) ? 1 : 0; // Impuls na stanie 3
}

// Licznik z enable
void enabled_counter(uint64_t *next_state, const uint64_t *input,
const uint64_t *state, size_t n, size_t s) {
    if (*input & 1) { // Jeśli enable = 1
        *next_state = (*state + 1) % 16;
    } else {
        *next_state = *state; // Zatrzymaj licznik
    }
}

int main() {
// Stan początkowy
    uint64_t initial_state[] = {0};
    // Generator impulsów
    moore_t *pulse_gen = ma_create_full(0, 1, 2, pulse_generator, pulse_output, initial_state);

    // Licznik z enable
    moore_t *counter = ma_create_simple(1, 4, enabled_counter);

    if (!pulse_gen || !counter) return 1;

    // Połącz wyjście generatora z wejściem licznika
    ma_connect(counter, 0, pulse_gen, 0, 1);

    moore_t *automata[] = {pulse_gen, counter};

    printf("Generator impulsów steruje licznikiem:\n");
    printf("Krok | Generator | Licznik\n");
    printf("-----|-----------|--------\n");

    for (int i = 0; i < 20; i++) {
        printf("%4d |    %lu      |   %lu\n", i, 
            ma_get_output(pulse_gen), 
            ma_get_output(counter));
        ma_step(automata, 2);
    }

    ma_delete(pulse_gen);
    ma_delete(counter);
    return 0;
}
```


## 🏗️ Struktura projektu

```
libma/
├── ma.h # Nagłówek z deklaracjami API
├── ma.c # Implementacja biblioteki
├── Makefile # System budowania
├── README.md # Ten plik
├── examples/ # Przykłady użycia
│ ├── counter.c # Prosty licznik
│ ├── connected.c # Połączone automaty
│ └── complex.c # Złożona sieć automatów
├── tests/ # Testy jednostkowe
└── docs/ # Dokumentacja szczegółowa
```


## 🔧 Wymagania systemowe

- **Kompilator**: GCC 7+ lub Clang 6+
- **Standard**: GNU C17 (ISO C17 + rozszerzenia GNU)
- **System**: Linux, macOS, Windows (MinGW/Cygwin)
- **Pamięć**: Minimalne wymagania (automatycznie skaluje)

## 📋 Cele Makefile
```
make # Zbuduj bibliotekę (domyślnie)
make clean # Wyczyść pliki tymczasowe
make install # Zainstaluj systemowo (wymaga sudo)
make uninstall # Odinstaluj
make test # Uruchom testy (jeśli dostępne)
make examples # Zbuduj przykłady
make docs # Wygeneruj dokumentację (wymaga Doxygen)
```

## ⚡ Wydajność

- **Pamięć**: O(n + m + s) na automat, gdzie n=wejścia, m=wyjścia, s=stany  
- **Czas**: O(1) operacje podstawowe, O(n) symulacja kroku
- **Skalowanie**: Obsługa automatów z tysiącami stanów
- **Bezpieczeństwo**: Magic numbers zapobiegają błędom segmentacji

## 📄 Licencja

**MIT License** - zobacz plik [LICENSE](LICENSE)

## 👨‍💻 Autor

** Wiktor Gerałtowski** - student informatyki, Uniwersytet Warszawski




