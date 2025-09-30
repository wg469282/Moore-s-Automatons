# Moore Automata Library (libma)


🇬🇧 **A C library for simulating Moore finite state automata**


## 📖 Description


The `libma` library implements complete simulation of Moore finite state automata in C. Moore automata are a type of finite state automaton where the output signal depends only on the current state of the automaton, not on input signals.


### 🎯 Library features:


- **✅ Full Moore automaton implementation** - with transition and output functions
- **🔗 Automaton connection** - ability to create complex automaton networks
- **⚡ Synchronous simulation** - all automata operate in parallel
- **🛡️ Memory safety** - automatic memory management with magic numbers
- **📏 Scalable architecture** - support for automata with any number of states
- **🧪 Test-ready** - clean API with error handling


## 🚀 Quick start


### 1. Basic usage
```c
#include <ma.h>

// Transition function (example: binary counter)
void transition_counter(uint64_t *next_state, const uint64_t *input,
const uint64_t *state, size_t n, size_t s) {
    // Transition implementation
    *next_state = (*state + 1) % (1ULL << s);
}

// Main function
int main() {
    // Create automaton: 2 inputs, 3 state bits
    moore_t *counter = ma_create_simple(2, 3, transition_counter);
    if (!counter) {
        perror("Automaton creation error");
        return 1;
    }
    // Execute several simulation steps
    moore_t *automata[] = {counter};
    for (int i = 0; i < 8; i++) {
        printf("State %d: %lu\n", i, ma_get_output(counter));
        ma_step(automata, 1);
    }
    // Free memory
    ma_delete(counter);
        return 0;
}
```
### 2. Program compilation
```bash
gcc -o program program.c -lma

```


## 📚 API Reference


### 🏗️ Creating automata
```c
// Creating automaton with full parameterization
moore_t *ma_create_full(size_t n, size_t m, size_t s,
transition_function_t t, output_function_t y,
const uint64_t *q);

// Creating simple automaton (output = state)
moore_t *ma_create_simple(size_t n, size_t s, transition_function_t t);

// Deleting automaton
void ma_delete(moore_t *a);

```
### 🔗 Connecting automata
```c
// Connecting outputs to inputs
int ma_connect(moore_t *a_in, size_t in, moore_t *a_out, size_t out, size_t num);

// Disconnection
int ma_disconnect(moore_t *a_in, size_t in, size_t num);
```


### ⚙️ Automaton control

```c
// Setting input signals
int ma_set_input(moore_t *a, const uint64_t *input);

// Setting state
int ma_set_state(moore_t *a, const uint64_t *state);

// Reading output
const uint64_t *ma_get_output(const moore_t *a);
```


### 🎬 Simulation
```c
// Executing one step for all automata
int ma_step(moore_t *at[], size_t num);
```


## 🎓 Examples


### Simple counter

```c 
#include <stdio.h>

void counter_transition(uint64_t *next_state, const uint64_t *input,
const uint64_t *state, size_t n, size_t s) {
    // Counter modulo 2^s
    *next_state = (*state + 1) % (1ULL << s);
}

int main() {
    moore_t *counter = ma_create_simple(0, 4, counter_transition); // 4-bit counter
    if (!counter) return 1;

    moore_t *automata[] = {counter};

    printf("4-bit counter sequence:\n");
        for (int i = 0; i < 20; i++) {
            printf("Step %2d: %lu\n", i, ma_get_output(counter));
            ma_step(automata, 1);
        }

    ma_delete(counter);
        return 0;
}
```



### Connected automata
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



## 🏗️ Project structure

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



## 🔧 System requirements


- **Compiler**: GCC 7+ or Clang 6+
- **Standard**: GNU C17 (ISO C17 + GNU extensions)
- **System**: Linux, macOS, Windows (MinGW/Cygwin)
- **Memory**: Minimal requirements (automatically scales)


## 📋 Makefile targets

```make # Build library (default)
make clean # Clean temporary files
make install # Install system-wide (requires sudo)
make uninstall # Uninstall
make test # Run tests (if available)
make examples # Build examples
make docs # Generate documentation (requires Doxygen)
```


## ⚡ Performance


- **Memory**: O(n + m + s) per automaton, where n=inputs, m=outputs, s=states  
- **Time**: O(1) basic operations, O(n) step simulation
- **Scaling**: Support for automata with thousands of states
- **Safety**: Magic numbers prevent segmentation faults


## 📄 License


**MIT License** - see [LICENSE](LICENSE) file


Copyright (c) 2025 Wiktor Gerałtowski


Permission is hereby granted, free of charge, to any person obtaining a copy of this software...


## 👨‍💻 Author


**Wiktor Gerałtowski** - Computer Science student, University of Warsaw
