#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "ma.h"

#define INIT_CONNECTION_CAPACITY 8
#define MOORE_MAGIC 0xDEADBEEF
#define MOORE_DELETED 0x00000000

/**
 * @brief Structure representing a Moore automaton
 *
 * Contains automaton parameters (n, m, s), transition and output functions,
 * buffers for states and signals, and information about connections to other automata.
 */
typedef struct moore
{
    /* Basic automaton parameters and logic */
    size_t n;                /* Number of inputs */
    size_t m;                /* Number of outputs */
    size_t s;                /* Number of state bits */
    transition_function_t t; /* Pointer to transition function */
    output_function_t y;     /* Pointer to output function */

    /* Data buffers */
    uint32_t magic; /* Magic number: object lifetime marker (MOORE_MAGIC after creation,
                       MOORE_DELETED after deletion) */
    uint64_t *state;      /* Buffer for CURRENT automaton state */
    uint64_t *next_state; /* Buffer for NEXT state (calculated in ma_step) */
    uint64_t *output;     /* Buffer for output signals */

    /* Buffers for input management */
    uint64_t *manual_input; /* Buffer for values set by ma_set_input */
    uint64_t *final_input;  /* Buffer for final input signal */

    /* Input connection management */
    input_connection_info *incoming_connections;

    /* Disconnection management during automaton deletion */
    struct moore **connected_to_me;
    size_t connected_to_me_count;    /* Number of current connections */
    size_t connected_to_me_capacity; /* Capacity of allocated array */
} moore_t;

/**
 * @brief Creates a new Moore automaton with full parameterization
 *
 * @param n Number of input signals
 * @param m Number of output signals
 * @param s Number of internal state bits
 * @param t Transition function
 * @param y Output function
 * @param q Initial automaton state
 * @return Pointer to new automaton or NULL on error
 */
moore_t *ma_create_full(size_t n, size_t m, size_t s,
                        transition_function_t t, output_function_t y,
                        uint64_t const *q)
{
    /* Check input parameters */
    if (!t || !y || !q || m == 0 || s == 0)
    {
        errno = EINVAL;
        return NULL;
    }

    /* Calculate buffer sizes in uint64_t elements */
    const size_t n_elements = (n > 0) ? (n + 63) / 64 : 0;
    const size_t m_elements = (m + 63) / 64;
    const size_t s_elements = (s + 63) / 64;

    /* Check for overflow in calculations */
    if (n > SIZE_MAX / 64 || m > SIZE_MAX / 64 || s > SIZE_MAX / 64)
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Check if buffer sizes are not too large */
    if ((n_elements > SIZE_MAX / sizeof(uint64_t)) ||
        (m_elements > SIZE_MAX / sizeof(uint64_t)) ||
        (s_elements > SIZE_MAX / sizeof(uint64_t)) ||
        (n > SIZE_MAX / sizeof(input_connection_info)))
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Allocate main structure */
    moore_t *new = calloc(1, sizeof(moore_t));
    if (!new)
    {
        errno = ENOMEM;
        return NULL; /* Cannot use goto because 'new' doesn't exist */
    }

    /* --- Buffer allocation sequence --- */
    /* Allocate buffers for inputs (only if n > 0) */
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

    /* Allocate buffers for states */
    new->state = calloc(s_elements, sizeof(uint64_t));
    if (!new->state)
        goto cleanup_fail;

    new->next_state = calloc(s_elements, sizeof(uint64_t));
    if (!new->next_state)
        goto cleanup_fail;

    /* Allocate output buffer */
    new->output = calloc(m_elements, sizeof(uint64_t));
    if (!new->output)
        goto cleanup_fail;

    /* Allocate output connection array */
    new->connected_to_me_capacity = INIT_CONNECTION_CAPACITY;
    new->connected_to_me = calloc(new->connected_to_me_capacity, sizeof(moore_t *));
    if (!new->connected_to_me)
        goto cleanup_fail;

    /* --- Field initialization after successful allocations --- */
    new->n = n;
    new->m = m;
    new->s = s;
    new->t = t;
    new->y = y;
    new->connected_to_me_count = 0;
    new->magic = MOORE_MAGIC;

    /* Copy initial state */
    memcpy(new->state, q, s_elements * sizeof(uint64_t));

    /* Calculate initial output */
    new->y(new->output, new->state, new->m, new->s);

    return new;

cleanup_fail:
    /* Free all allocated buffers */
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
 * @brief Identity function copying state to output
 *
 * @param output Output buffer
 * @param state Internal state to copy
 * @param m Number of output bits (must equal s)
 * @param s Number of state bits
 */
void identity_func(uint64_t *output, uint64_t const *state, size_t m, size_t s)
{
    if (m == s && s > 0)
    {
        const size_t elements_to_copy = (s + 63) / 64;
        memcpy(output, state, elements_to_copy * sizeof(uint64_t));

        /* Clear unused bits in last word */
        const size_t used_bits = s % 64;
        if (used_bits != 0)
        {
            const uint64_t mask = ((uint64_t)1 << used_bits) - 1;
            output[elements_to_copy - 1] &= mask;
        }
    }
}

/**
 * @brief Creates a simple Moore automaton with identity output function
 *
 * @param n Number of input signals
 * @param s Number of state bits (and simultaneously outputs)
 * @param t Transition function
 * @return Pointer to new automaton or NULL on error
 */
moore_t *ma_create_simple(size_t n, size_t s, transition_function_t t)
{
    if (s == 0 || !t)
    {
        errno = EINVAL;
        return NULL;
    }

    /* Create temporary, zeroed initial state */
    const size_t s_elements = (s + 63) / 64;
    uint64_t *q_zero = calloc(s_elements, sizeof(uint64_t));
    if (s > 0 && !q_zero)
    {
        errno = ENOMEM;
        return NULL;
    }

    /* Call ma_create_full with appropriate parameters */
    moore_t *new_automaton = ma_create_full(n, s, s, t, identity_func, q_zero);

    /* Free temporary state buffer */
    free(q_zero);

    /* Return result (errno already set by ma_create_full on error) */
    return new_automaton;
}

/**
 * @brief Adds automaton to output connection list
 *
 * @param a_out Output automaton (signal source)
 * @param a_in Input automaton (signal receiver)
 * @return 0 on success, -1 on error
 */
static int append_to_connected_list(moore_t *a_out, moore_t *a_in)
{
    /* Check if connection already exists */
    for (size_t i = 0; i < a_out->connected_to_me_count; ++i)
    {
        if (a_out->connected_to_me[i] == a_in)
        {
            return 0; /* Already exists, operation successful */
        }
    }

    /* Check if capacity needs to be increased */
    if (a_out->connected_to_me_count == a_out->connected_to_me_capacity)
    {
        /* Calculate new capacity (double or set minimum 8) */
        const size_t new_capacity = (a_out->connected_to_me_capacity == 0) ? 8 : a_out->connected_to_me_capacity * 2;

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

    /* Add new connection */
    a_out->connected_to_me[a_out->connected_to_me_count] = a_in;
    a_out->connected_to_me_count++;
    return 0;
}

/**
 * @brief Connects inputs of one automaton with outputs of another
 *
 * @param a_in Target automaton (receiving signals)
 * @param in Index of first input in a_in
 * @param a_out Source automaton (sending signals)
 * @param out Index of first output in a_out
 * @param num Number of connected signals
 * @return 0 on success, -1 on error
 */
int ma_connect(moore_t *a_in, size_t in, moore_t *a_out, size_t out, size_t num)
{
    /* Check basic parameters */
    if (!a_in || !a_out || num == 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* Check if indices are in range */
    if (in >= a_in->n || out >= a_out->m ||
        num > a_in->n - in || num > a_out->m - out)
    {
        errno = EINVAL;
        return -1;
    }

    /* Create connections */
    for (size_t i = 0; i < num; ++i)
    {
        a_in->incoming_connections[in + i].source_automaton = a_out;
        a_in->incoming_connections[in + i].source_output_index = out + i;
    }

    /* Add to output connection list */
    if (append_to_connected_list(a_out, a_in) != 0)
    {
        /* errno already set by helper function */
        return -1;
    }

    return 0;
}

/**
 * @brief Removes automaton from output connection list
 *
 * @param a_in Automaton to remove from list
 * @param a_out Automaton owner of connection list
 */
void remove_connected(moore_t *a_in, moore_t *a_out)
{
    if (!a_in || !a_out)
        return;

    /* Compact array removing all occurrences of a_in */
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
 * @brief Deletes automaton and frees all memory used by it
 *
 * @param a Pointer to automaton to delete (can be NULL)
 *
 * @note Automatically disconnects all connections before freeing
 * @note After calling, the pointer becomes invalid
 */
void ma_delete(moore_t *a)
{
    if (!a || a->magic != MOORE_MAGIC)
        return;

    a->magic = MOORE_DELETED;

    /* Disconnect self from its sources: */
    for (size_t j = 0; j < a->n; j++)
    {
        moore_t *src = a->incoming_connections[j].source_automaton;
        if (src && src->magic == MOORE_MAGIC)
        {
            remove_connected(a, src);
        }
    }

    /* Disconnect all its output connections: */
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

    /* Free memory */
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
 * @brief Checks if automaton has any input connections
 *
 * @param a_in Automaton to check
 * @return Pointer to first connected source automaton or NULL
 */
moore_t *is_connected_to(moore_t const *a_in)
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
 * @brief Disconnects consecutive input signals from their sources
 *
 * @param a_in Automaton to disconnect
 * @param in Index of first input to disconnect
 * @param num Number of inputs to disconnect
 * @return 0 on success, -1 on error
 */
int ma_disconnect(moore_t *a_in, size_t in, size_t num)
{
    /* Check parameters */
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

    /* Collect all sources to be disconnected (avoid duplicates) */
    moore_t *sources_to_remove[num];
    size_t unique_sources = 0;

    for (size_t i = 0; i < num; i++)
    {
        moore_t *src = a_in->incoming_connections[in + i].source_automaton;
        if (src)
        {
            /* Check if src is not already on the list */
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

        /* Disconnect input */
        a_in->incoming_connections[in + i].source_automaton = NULL;
        a_in->incoming_connections[in + i].source_output_index = 0;
    }

    /* Remove from connected_to_me only if automaton has NO MORE
       connections to given source */
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
 * @brief Sets signal values on unconnected automaton inputs
 *
 * @param a Pointer to automaton
 * @param input Array with input values (n bits)
 * @return 0 on success, -1 on error
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
 * @brief Sets internal state of automaton
 *
 * @param a Pointer to automaton
 * @param state New state to set (s bits)
 * @return 0 on success, -1 on error
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
 * @brief Returns pointer to output signal buffer
 *
 * @param a Pointer to automaton
 * @return Pointer to output buffer or NULL on error
 *
 * @note Pointer valid until ma_delete is called
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

/* Helper functions for bit operations */
static inline int bit_word(size_t idx)
{
    return (int)(idx / 64);
}

static inline int bit_pos(size_t idx)
{
    return (int)(idx % 64);
}

/**
 * @brief Updates final input signals of automaton
 *
 * @param a Automaton to update
 *
 * @note Combines signals from connections and manual_input into one final_input buffer
 */
static void update_final_input(moore_t *a)
{
    if (!a || a->n == 0 || !a->final_input ||
        !a->manual_input || !a->incoming_connections)
        return;

    const size_t n_words = (a->n + 63) / 64;

    /* Clear output buffer */
    for (size_t w = 0; w < n_words; ++w)
        a->final_input[w] = 0;

    /* Process each input */
    for (size_t i = 0; i < a->n; i++)
    {
        const int w = bit_word(i), b = bit_pos(i);
        const uint64_t mask = (1ULL << b);
        uint64_t value = 0;

        moore_t *src = a->incoming_connections[i].source_automaton;

        /* Safe access to src */
        if (src &&
            src->magic == MOORE_MAGIC &&
            src->output)
        {
            const size_t src_idx = a->incoming_connections[i].source_output_index;
            if (src_idx < src->m)
            {
                /* Get value from connected output */
                const int sw = bit_word(src_idx), sb = bit_pos(src_idx);
                value = (src->output[sw] >> sb) & 1ULL;
            }
            else
            {
                /* Invalid index - use manual_input */
                value = (a->manual_input[w] >> b) & 1ULL;
            }
        }
        else
        {
            /* Unconnected or deleted source - use manual_input */
            value = (a->manual_input[w] >> b) & 1ULL;
        }

        /* Set bit in final buffer */
        if (value)
            a->final_input[w] |= mask;
    }
}

/**
 * @brief Executes one simulation step for given automata
 *
 * @param at Array of pointers to automata
 * @param num Number of automata in array
 * @return 0 on success, -1 on error
 *
 * @note All automata operate synchronously and in parallel
 */
int ma_step(moore_t *at[], size_t num)
{
    if (num == 0 || !at)
    {
        errno = EINVAL;
        return -1;
    }

    /* Update inputs of all automata */
    for (size_t i = 0; i < num; i++)
    {
        if (!at[i])
        {
            errno = EINVAL;
            return -1;
        }
        update_final_input(at[i]);
    }

    /* Calculate next states */
    for (size_t j = 0; j < num; j++)
    {
        moore_t *a = at[j];
        a->t(a->next_state, a->final_input, a->state, a->n, a->s);
    }

    /* Update states and calculate outputs */
    for (size_t k = 0; k < num; k++)
    {
        moore_t *a = at[k];

        /* Swap state buffers */
        uint64_t *tmp = a->state;
        a->state = a->next_state;
        a->next_state = tmp;

        /* Calculate new output */
        a->y(a->output, a->state, a->m, a->s);
    }

    return 0;
}
