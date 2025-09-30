#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define INIT_CONNECTION_CAPACITY 8
#define MOORE_MAGIC 0xDEADBEEF
#define MOORE_DELETED 0x00000000
/* Forward declaration */
typedef struct moore moore_t;

/* Function pointer types for automaton operations */
typedef void (*transition_function_t)(uint64_t *next_state, uint64_t const *input,
                                      uint64_t const *state, size_t n, size_t s);

typedef void (*output_function_t)(uint64_t *output, uint64_t const *state,
                                  size_t m, size_t s);

/**
 * @brief Struktura opisująca połączenie jednego wejścia z wyjściem innego automatu
 *
 * @note Każde wejście może być połączone z maksymalnie jednym wyjściem
 */
typedef struct
{
    moore_t *source_automaton;  /* Wskaźnik na automat będący źródłem sygnału */
    size_t source_output_index; /* Numer wyjścia w automacie źródłowym */
} input_connection_info;

/**
 * @brief Struktura reprezentująca automat Moore'a
 *
 * Zawiera parametry automatu (n, m, s), funkcje przejść i wyjść,
 * bufory na stany i sygnały oraz informacje o połączeniach z innymi automatami.
 */
typedef struct moore
{
    /* Podstawowe parametry i logika automatu */
    size_t n;                /* Liczba wejść */
    size_t m;                /* Liczba wyjść */
    size_t s;                /* Liczba bitów stanu */
    transition_function_t t; /* Wskaźnik na funkcję przejść */
    output_function_t y;     /* Wskaźnik na funkcję wyjść */

    /* Bufory na dane */
    uint32_t magic;       /* Magic number: znacznik życia obiektu (MOORE_MAGIC po utworzeniu,
    MOORE_DELETED po usunięciu) */
    uint64_t *state;      /* Bufor na AKTUALNY stan automatu */
    uint64_t *next_state; /* Bufor na NASTĘPNY stan (obliczany w ma_step) */
    uint64_t *output;     /* Bufor na sygnały wyjściowe */

    /* Bufory do zarządzania wejściami */
    uint64_t *manual_input; /* Bufor na wartości ustawiane przez ma_set_input */
    uint64_t *final_input;  /* Bufor na finalny sygnał wejściowy */

    /* Zarządzanie połączeniami wejściowymi */
    input_connection_info *incoming_connections;

    /* Zarządzanie rozłączaniem przy usuwaniu automatu */
    struct moore **connected_to_me;
    size_t connected_to_me_count;    /* Liczba aktualnych połączeń */
    size_t connected_to_me_capacity; /* Pojemność zaalokowanej tablicy */
} moore_t;

/**
 * @brief Tworzy nowy automat Moore'a z pełną parametryzacją
 *
 * @param n Liczba sygnałów wejściowych
 * @param m Liczba sygnałów wyjściowych
 * @param s Liczba bitów stanu wewnętrznego
 * @param t Funkcja przejść
 * @param y Funkcja wyjść
 * @param q Początkowy stan automatu
 * @return Wskaźnik na nowy automat lub NULL przy błędzie
 */
moore_t *ma_create_full(size_t n, size_t m, size_t s,
                        transition_function_t t, output_function_t y,
                        uint64_t const *q)
{
    /* Sprawdź parametry wejściowe */
    if (!t || !y || !q || m == 0 || s == 0)
    {
        errno = EINVAL;
        return NULL;
    }

    /* Oblicz rozmiary buforów w elementach uint64_t */
    size_t n_elements = (n > 0) ? (n + 63) / 64 : 0;
    size_t m_elements = (m + 63) / 64;
    size_t s_elements = (s + 63) / 64;

    /* Sprawdź czy nie nastąpi overflow w obliczeniach */
    if (n > SIZE_MAX / 64 || m > SIZE_MAX / 64 || s > SIZE_MAX / 64)
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Sprawdź czy rozmiary buforów nie są zbyt duże */
    if ((n_elements > SIZE_MAX / sizeof(uint64_t)) ||
        (m_elements > SIZE_MAX / sizeof(uint64_t)) ||
        (s_elements > SIZE_MAX / sizeof(uint64_t)) ||
        (n > SIZE_MAX / sizeof(input_connection_info)))
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Alokuj główną strukturę */
    moore_t *new = calloc(1, sizeof(moore_t));
    if (!new)
    {
        errno = ENOMEM;
        return NULL; /* Nie można użyć goto, bo 'new' nie istnieje */
    }

    /* --- Sekwencja alokacji buforów --- */

    /* Alokuj bufory dla wejść (tylko jeśli n > 0) */
    if (n > 0)
    {
        new->manual_input = calloc(n_elements, sizeof(uint64_t));
        if (!new->manual_input)
            goto cleanup_fail;

        new->final_input = calloc(n_elements, sizeof(uint64_t));
        if (!new->final_input)
            goto cleanup_fail;

        new->incoming_connections = calloc(n, sizeof(input_connection_info));
        if (!new->incoming_connections)
            goto cleanup_fail;
    }

    /* Alokuj bufory dla stanów */
    new->state = calloc(s_elements, sizeof(uint64_t));
    if (!new->state)
        goto cleanup_fail;

    new->next_state = calloc(s_elements, sizeof(uint64_t));
    if (!new->next_state)
        goto cleanup_fail;

    /* Alokuj bufor wyjściowy */
    new->output = calloc(m_elements, sizeof(uint64_t));
    if (!new->output)
        goto cleanup_fail;

    /* Alokuj tablicę połączeń wyjściowych */
    new->connected_to_me_capacity = INIT_CONNECTION_CAPACITY;
    new->connected_to_me = calloc(new->connected_to_me_capacity, sizeof(moore_t *));
    if (!new->connected_to_me)
        goto cleanup_fail;

    /* --- Inicjalizacja pól po udanych alokacjach --- */
    new->n = n;
    new->m = m;
    new->s = s;
    new->t = t;
    new->y = y;
    new->connected_to_me_count = 0;
    new->magic = MOORE_MAGIC;

    /* Skopiuj stan początkowy */
    memcpy(new->state, q, s_elements * sizeof(uint64_t));

    /* Oblicz początkowe wyjście */
    new->y(new->output, new->state, new->m, new->s);

    return new;

cleanup_fail:
    /* Zwolnij wszystkie zaalokowane bufory */
    errno = ENOMEM;
    free(new->manual_input);
    free(new->final_input);
    free(new->incoming_connections);
    free(new->state);
    free(new->next_state);
    free(new->output);
    free(new->connected_to_me);
    free(new);
    return NULL;
}

/**
 * @brief Funkcja identycznościowa kopiująca stan na wyjście
 *
 * @param output Bufor wyjściowy
 * @param state Stan wewnętrzny do skopiowania
 * @param m Liczba bitów wyjściowych (musi równać się s)
 * @param s Liczba bitów stanu
 */
void identity_func(uint64_t *output, uint64_t const *state, size_t m, size_t s)
{
    if (m == s && s > 0)
    {
        size_t elements_to_copy = (s + 63) / 64;
        memcpy(output, state, elements_to_copy * sizeof(uint64_t));

        /* Wyzeruj nieużywane bity w ostatnim słowie */
        size_t used_bits = s % 64;
        if (used_bits != 0)
        {
            uint64_t mask = ((uint64_t)1 << used_bits) - 1;
            output[elements_to_copy - 1] &= mask;
        }
    }
}

/**
 * @brief Tworzy prosty automat Moore'a z funkcją wyjść jako identyczność
 *
 * @param n Liczba sygnałów wejściowych
 * @param s Liczba bitów stanu (i jednocześnie wyjść)
 * @param t Funkcja przejść
 * @return Wskaźnik na nowy automat lub NULL przy błędzie
 */
moore_t *ma_create_simple(size_t n, size_t s, transition_function_t t)
{
    if (s == 0 || !t)
    {
        errno = EINVAL;
        return NULL;
    }

    /* Stwórz tymczasowy, wyzerowany stan początkowy */
    size_t s_elements = (s + 63) / 64;
    uint64_t *q_zero = calloc(s_elements, sizeof(uint64_t));
    if (s > 0 && !q_zero)
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Wywołaj ma_create_full z odpowiednimi parametrami */
    moore_t *new_automaton = ma_create_full(n, s, s, t, identity_func, q_zero);

    /* Zwolnij tymczasowy bufor stanu */
    free(q_zero);

    /* Zwróć wynik (errno już ustawione przez ma_create_full w razie błędu) */
    return new_automaton;
}

/**
 * @brief Dodaje automat do listy połączeń wyjściowych
 *
 * @param a_out Automat wyjściowy (źródło sygnału)
 * @param a_in Automat wejściowy (odbiorca sygnału)
 * @return 0 przy sukcesie, -1 przy błędzie
 */
static int append_to_connected_list(moore_t *a_out, moore_t *a_in)
{
    /* Sprawdź czy połączenie już istnieje */
    for (size_t i = 0; i < a_out->connected_to_me_count; ++i)
    {
        if (a_out->connected_to_me[i] == a_in)
        {
            return 0; /* Już istnieje, operacja udana */
        }
    }

    /* Sprawdź czy trzeba zwiększyć pojemność */
    if (a_out->connected_to_me_count == a_out->connected_to_me_capacity)
    {
        /* Oblicz nową pojemność (podwój lub ustaw minimum 8) */
        size_t new_capacity = (a_out->connected_to_me_capacity == 0) ? 8 : a_out->connected_to_me_capacity * 2;

        moore_t **new_ptr = realloc(a_out->connected_to_me,
                                    new_capacity * sizeof(moore_t *));
        if (!new_ptr)
        {
            errno = ENOMEM;
            return -1;
        }

        a_out->connected_to_me = new_ptr;
        a_out->connected_to_me_capacity = new_capacity;
    }

    /* Dodaj nowe połączenie */
    a_out->connected_to_me[a_out->connected_to_me_count] = a_in;
    a_out->connected_to_me_count++;
    return 0;
}

/**
 * @brief Łączy wejścia jednego automatu z wyjściami drugiego
 *
 * @param a_in Automat docelowy (odbierający sygnały)
 * @param in Indeks pierwszego wejścia w a_in
 * @param a_out Automat źródłowy (wysyłający sygnały)
 * @param out Indeks pierwszego wyjścia w a_out
 * @param num Liczba łączonych sygnałów
 * @return 0 przy sukcesie, -1 przy błędzie
 */
int ma_connect(moore_t *a_in, size_t in, moore_t *a_out, size_t out, size_t num)
{
    /* Sprawdź podstawowe parametry */
    if (!a_in || !a_out || num == 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* Sprawdź czy indeksy są w zakresie */
    if (in >= a_in->n || out >= a_out->m ||
        num > a_in->n - in || num > a_out->m - out)
    {
        errno = EINVAL;
        return -1;
    }

    /* Utwórz połączenia */
    for (size_t i = 0; i < num; ++i)
    {
        a_in->incoming_connections[in + i].source_automaton = a_out;
        a_in->incoming_connections[in + i].source_output_index = out + i;
    }

    /* Dodaj do listy połączeń wyjściowych */
    if (append_to_connected_list(a_out, a_in) != 0)
    {
        /* errno już ustawione przez funkcję pomocniczą */
        return -1;
    }

    return 0;
}

/**
 * @brief Usuwa automat z listy połączeń wyjściowych
 *
 * @param a_in Automat do usunięcia z listy
 * @param a_out Automat właściciel listy połączeń
 */
void remove_connected(moore_t *a_in, moore_t *a_out)
{
    if (!a_in || !a_out)
        return;
    /* Kompaktuj tablicę usuwając wszystkie wystąpienia a_in */
    size_t j = 0;
    for (size_t i = 0; i < a_out->connected_to_me_count; i++)
    {
        if (a_out->connected_to_me[i] && a_out->connected_to_me[i] != a_in)
        {
            a_out->connected_to_me[j++] = a_out->connected_to_me[i];
        }
    }
    a_out->connected_to_me_count = j;
}

/**
 * @brief Usuwa automat i zwalnia całą używaną przez niego pamięć
 *
 * @param a Wskaźnik na automat do usunięcia (może być NULL)
 *
 * @note Automatycznie rozłącza wszystkie połączenia przed zwolnieniem
 * @note Po wywołaniu wskaźnik staje się nieważny
 */
void ma_delete(moore_t *a)
{
    if (!a || a->magic != MOORE_MAGIC)
        return;
    a->magic = MOORE_DELETED;

    /* Odłącz siebie od swoich źródeł: */
    for (size_t j = 0; j < a->n; j++)
    {
        moore_t *src = a->incoming_connections[j].source_automaton;
        if (src && src->magic == MOORE_MAGIC)
        {
            remove_connected(a, src);
        }
    }

    /* Odłącz wszystkie swoje połączenia wyjściowe: */
    for (size_t i = 0; i < a->connected_to_me_count; i++)
    {
        moore_t *in = a->connected_to_me[i];
        if (!in || in->magic != MOORE_MAGIC)
            continue;
        for (size_t j = 0; j < in->n; j++)
        {
            if (in->incoming_connections[j].source_automaton == a)
            {
                in->incoming_connections[j].source_automaton = NULL;
                in->incoming_connections[j].source_output_index = 0;
            }
        }
    }

    /* Zwolnij pamięć */
    free(a->state);
    free(a->next_state);
    free(a->output);
    free(a->manual_input);
    free(a->final_input);
    free(a->incoming_connections);
    free(a->connected_to_me);
    free(a);
}

/**
 * @brief Sprawdza czy automat ma jakiekolwiek połączenia wejściowe
 *
 * @param a_in Automat do sprawdzenia
 * @return Wskaźnik na pierwszy połączony automat źródłowy lub NULL
 */
moore_t *is_connected_to(moore_t *a_in)
{
    for (size_t i = 0; i < a_in->n; i++)
    {
        if (a_in->incoming_connections[i].source_automaton)
        {
            return a_in->incoming_connections[i].source_automaton;
        }
    }
    return NULL;
}

/**
 * @brief Rozłącza kolejne sygnały wejściowe od ich źródeł
 *
 * @param a_in Automat do rozłączenia
 * @param in Indeks pierwszego wejścia do rozłączenia
 * @param num Liczba wejść do rozłączenia
 * @return 0 przy sukcesie, -1 przy błędzie
 */
int ma_disconnect(moore_t *a_in, size_t in, size_t num)
{
    /* Sprawdź parametry */
    if (!a_in || num == 0)
    {
        errno = EINVAL;
        return -1;
    }

    if (in >= a_in->n || num > a_in->n - in)
    {
        errno = EINVAL;
        return -1;
    }

    /* Zbierz wszystkie źródła które będą rozłączone (unikaj duplikatów) */
    moore_t *sources_to_remove[num];
    size_t unique_sources = 0;

    for (size_t i = 0; i < num; i++)
    {
        moore_t *src = a_in->incoming_connections[in + i].source_automaton;
        if (src)
        {
            /* Sprawdź czy src już nie jest na liście */
            bool already_listed = false;
            for (size_t j = 0; j < unique_sources; j++)
            {
                if (sources_to_remove[j] == src)
                {
                    already_listed = true;
                    break;
                }
            }

            if (!already_listed)
            {
                sources_to_remove[unique_sources++] = src;
            }
        }

        /* Rozłącz wejście */
        a_in->incoming_connections[in + i].source_automaton = NULL;
        a_in->incoming_connections[in + i].source_output_index = 0;
    }

    /* Usuń z connected_to_me tylko jeśli automat nie ma już ŻADNYCH
       połączeń do danego źródła */
    for (size_t i = 0; i < unique_sources; i++)
    {
        moore_t *src = sources_to_remove[i];
        bool still_connected = false;

        for (size_t j = 0; j < a_in->n; j++)
        {
            if (a_in->incoming_connections[j].source_automaton == src)
            {
                still_connected = true;
                break;
            }
        }

        if (!still_connected)
        {
            remove_connected(a_in, src);
        }
    }

    return 0;
}

/**
 * @brief Ustawia wartości sygnałów na niepodłączonych wejściach automatu
 *
 * @param a Wskaźnik na automat
 * @param input Tablica z wartościami wejściowymi (n bitów)
 * @return 0 przy sukcesie, -1 przy błędzie
 */
int ma_set_input(moore_t *a, uint64_t const *input)
{
    if (!a || !input || a->n == 0)
    {
        errno = EINVAL;
        return -1;
    }

    size_t n_elements = (a->n + 63) / 64;
    memcpy(a->manual_input, input, n_elements * sizeof(uint64_t));
    return 0;
}

/**
 * @brief Ustawia stan wewnętrzny automatu
 *
 * @param a Wskaźnik na automat
 * @param state Nowy stan do ustawienia (s bitów)
 * @return 0 przy sukcesie, -1 przy błędzie
 */
int ma_set_state(moore_t *a, uint64_t const *state)
{
    if (!a || !state)
    {
        errno = EINVAL;
        return -1;
    }

    size_t s_elements = (a->s + 63) / 64;
    memcpy(a->state, state, s_elements * sizeof(uint64_t));
    a->y(a->output, a->state, a->m, a->s);
    return 0;
}

/**
 * @brief Zwraca wskaźnik do bufora sygnałów wyjściowych
 *
 * @param a Wskaźnik na automat
 * @return Wskaźnik na bufor wyjściowy lub NULL przy błędzie
 *
 * @note Wskaźnik ważny do wywołania ma_delete
 */
uint64_t const *ma_get_output(moore_t const *a)
{
    if (!a)
    {
        errno = EINVAL;
        return NULL;
    }

    return a->output;
}

/* Funkcje pomocnicze do operacji bitowych */
static inline int bit_word(size_t idx)
{
    return (int)(idx / 64);
}

static inline int bit_pos(size_t idx)
{
    return (int)(idx % 64);
}

/**
 * @brief Aktualizuje finalne sygnały wejściowe automatu
 *
 * @param a Automat do aktualizacji
 *
 * @note Łączy sygnały z połączeń i manual_input w jeden bufor final_input
 */
void update_final_input(moore_t *a)
{
    if (!a || a->n == 0 || !a->final_input ||
        !a->manual_input || !a->incoming_connections)
        return;

    size_t n_words = (a->n + 63) / 64;

    /* Wyczyść bufor wyjściowy */
    for (size_t w = 0; w < n_words; ++w)
        a->final_input[w] = 0;

    /* Przetwórz każde wejście */
    for (size_t i = 0; i < a->n; i++)
    {
        int w = bit_word(i), b = bit_pos(i);
        uint64_t mask = (1ULL << b), value = 0;
        moore_t *src = a->incoming_connections[i].source_automaton;

        /* Bezpieczny dostęp do src */
        if (src &&
            src->magic == MOORE_MAGIC &&
            src->output)
        {

            size_t src_idx = a->incoming_connections[i].source_output_index;
            if (src_idx < src->m)
            {
                /* Pobierz wartość z połączonego wyjścia */
                int sw = bit_word(src_idx), sb = bit_pos(src_idx);
                value = (src->output[sw] >> sb) & 1ULL;
            }
            else
            {
                /* Błędny indeks - użyj manual_input */
                value = (a->manual_input[w] >> b) & 1ULL;
            }
        }
        else
        {
            /* Niepołączone lub usunięte źródło - użyj manual_input */
            value = (a->manual_input[w] >> b) & 1ULL;
        }

        /* Ustaw bit w buforze finalnym */
        if (value)
            a->final_input[w] |= mask;
    }
}

/**
 * @brief Wykonuje jeden krok symulacji dla podanych automatów
 *
 * @param at Tablica wskaźników na automaty
 * @param num Liczba automatów w tablicy
 * @return 0 przy sukcesie, -1 przy błędzie
 *
 * @note Wszystkie automaty działają synchronicznie i równolegle
 */
int ma_step(moore_t *at[], size_t num)
{
    if (num == 0 || !at)
    {
        errno = EINVAL;
        return -1;
    }

    /* Aktualizuj wejścia wszystkich automatów */
    for (size_t i = 0; i < num; i++)
    {
        if (!at[i])
        {
            errno = EINVAL;
            return -1;
        }
        update_final_input(at[i]);
    }

    /* Oblicz następne stany */
    for (size_t j = 0; j < num; j++)
    {
        moore_t *a = at[j];
        a->t(a->next_state, a->final_input, a->state, a->n, a->s);
    }

    /* Aktualizuj stany i oblicz wyjścia */
    for (size_t k = 0; k < num; k++)
    {
        moore_t *a = at[k];

        /* Zamień bufory stanów */
        uint64_t *tmp = a->state;
        a->state = a->next_state;
        a->next_state = tmp;

        /* Oblicz nowe wyjście */
        a->y(a->output, a->state, a->m, a->s);
    }

    return 0;
}