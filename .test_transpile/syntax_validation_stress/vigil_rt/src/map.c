#include <stdint.h>
#include <string.h>

#include "internal/vigil_internal.h"
#include "vigil/map.h"

typedef enum vigil_map_entry_state
{
    VIGIL_MAP_ENTRY_EMPTY = 0,
    VIGIL_MAP_ENTRY_OCCUPIED = 1,
    VIGIL_MAP_ENTRY_TOMBSTONE = 2
} vigil_map_entry_state_t;

typedef struct vigil_map_entry
{
    vigil_map_entry_state_t state;
    uint64_t hash;
    vigil_value_t key;
    vigil_value_t value;
} vigil_map_entry_t;

static uint64_t vigil_map_hash_bytes(const char *key, size_t key_length)
{
    size_t index;
    uint64_t hash;

    hash = UINT64_C(14695981039346656037);
    for (index = 0U; index < key_length; index += 1U)
    {
        hash ^= (uint8_t)key[index];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static uint64_t vigil_map_mix_u64(uint64_t value)
{
    value ^= value >> 30U;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27U;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return value;
}

static int vigil_map_key_value_is_supported(const vigil_value_t *key)
{
    const vigil_object_t *object;

    if (key == NULL)
    {
        return 0;
    }

    switch (vigil_value_kind(key))
    {
    case VIGIL_VALUE_BOOL:
    case VIGIL_VALUE_INT:
    case VIGIL_VALUE_UINT:
        return 1;
    case VIGIL_VALUE_OBJECT:
        object = vigil_value_as_object(key);
        return object != NULL && vigil_object_type(object) == VIGIL_OBJECT_STRING;
    default:
        return 0;
    }
}

static uint64_t vigil_map_hash_value(const vigil_value_t *key)
{
    const vigil_object_t *object;

    if (!vigil_map_key_value_is_supported(key))
    {
        return 0U;
    }

    switch (vigil_value_kind(key))
    {
    case VIGIL_VALUE_BOOL:
        return vigil_map_mix_u64(vigil_value_as_bool(key) ? UINT64_C(1) : UINT64_C(0));
    case VIGIL_VALUE_INT:
        return vigil_map_mix_u64((uint64_t)vigil_value_as_int(key));
    case VIGIL_VALUE_UINT:
        return vigil_map_mix_u64(vigil_value_as_uint(key));
    case VIGIL_VALUE_OBJECT:
        object = vigil_value_as_object(key);
        return vigil_map_hash_bytes(vigil_string_object_c_str(object), vigil_string_object_length(object));
    default:
        return 0U;
    }
}

static int vigil_map_string_objects_equal(const vigil_object_t *left, const vigil_object_t *right)
{
    size_t left_length, right_length;
    const char *left_text, *right_text;

    if (left == NULL || right == NULL || vigil_object_type(left) != VIGIL_OBJECT_STRING ||
        vigil_object_type(right) != VIGIL_OBJECT_STRING)
    {
        return 0;
    }
    left_length = vigil_string_object_length(left);
    right_length = vigil_string_object_length(right);
    left_text = vigil_string_object_c_str(left);
    right_text = vigil_string_object_c_str(right);
    return left_length == right_length && left_text != NULL && right_text != NULL &&
           memcmp(left_text, right_text, left_length) == 0;
}

static int vigil_map_key_values_equal(const vigil_value_t *left, const vigil_value_t *right)
{
    if (left == NULL || right == NULL || vigil_value_kind(left) != vigil_value_kind(right))
    {
        return 0;
    }

    switch (vigil_value_kind(left))
    {
    case VIGIL_VALUE_BOOL:
        return vigil_value_as_bool(left) == vigil_value_as_bool(right);
    case VIGIL_VALUE_INT:
        return vigil_value_as_int(left) == vigil_value_as_int(right);
    case VIGIL_VALUE_UINT:
        return vigil_value_as_uint(left) == vigil_value_as_uint(right);
    case VIGIL_VALUE_OBJECT:
        return vigil_map_string_objects_equal(vigil_value_as_object(left), vigil_value_as_object(right));
    default:
        return 0;
    }
}

static int vigil_map_key_matches_string(const vigil_value_t *key, const char *text, size_t length)
{
    const vigil_object_t *object;
    const char *stored_text;

    if (key == NULL || text == NULL || vigil_value_kind(key) != VIGIL_VALUE_OBJECT)
    {
        return 0;
    }

    object = vigil_value_as_object(key);
    if (object == NULL || vigil_object_type(object) != VIGIL_OBJECT_STRING)
    {
        return 0;
    }

    stored_text = vigil_string_object_c_str(object);
    return stored_text != NULL && vigil_string_object_length(object) == length &&
           memcmp(stored_text, text, length) == 0;
}

static int vigil_map_validate_mutable(const vigil_map_t *map, vigil_error_t *error)
{
    vigil_error_clear(error);

    if (map == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map must not be null");
        return 0;
    }

    if (map->runtime == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map runtime must not be null");
        return 0;
    }

    return 1;
}

static vigil_map_entry_t *vigil_map_entries(vigil_map_t *map)
{
    return (vigil_map_entry_t *)map->entries;
}

static const vigil_map_entry_t *vigil_map_const_entries(const vigil_map_t *map)
{
    return (const vigil_map_entry_t *)map->entries;
}

static size_t vigil_map_next_capacity(size_t current_capacity)
{
    if (current_capacity == 0U)
    {
        return 16U;
    }

    return current_capacity * 2U;
}

static void vigil_map_entry_clear(vigil_map_entry_t *entry)
{
    if (entry == NULL)
    {
        return;
    }

    vigil_value_release(&entry->key);
    vigil_value_release(&entry->value);
    memset(entry, 0, sizeof(*entry));
}

static size_t vigil_map_find_index_value(const vigil_map_entry_t *entries, size_t capacity, const vigil_value_t *key,
                                         uint64_t hash, int *out_found)
{
    size_t index;
    size_t first_tombstone;

    *out_found = 0;
    first_tombstone = SIZE_MAX;
    index = (size_t)(hash & (uint64_t)(capacity - 1U));

    for (;;)
    {
        const vigil_map_entry_t *entry;

        entry = &entries[index];
        if (entry->state == VIGIL_MAP_ENTRY_EMPTY)
        {
            if (first_tombstone != SIZE_MAX)
            {
                return first_tombstone;
            }

            return index;
        }

        if (entry->state == VIGIL_MAP_ENTRY_TOMBSTONE)
        {
            if (first_tombstone == SIZE_MAX)
            {
                first_tombstone = index;
            }
        }
        else if (entry->hash == hash && vigil_map_key_values_equal(&entry->key, key))
        {
            *out_found = 1;
            return index;
        }

        index = (index + 1U) & (capacity - 1U);
    }
}

static size_t vigil_map_find_index_string(const vigil_map_entry_t *entries, size_t capacity, const char *key,
                                          size_t key_length, uint64_t hash, int *out_found)
{
    size_t index;
    size_t first_tombstone;

    *out_found = 0;
    first_tombstone = SIZE_MAX;
    index = (size_t)(hash & (uint64_t)(capacity - 1U));

    for (;;)
    {
        const vigil_map_entry_t *entry;

        entry = &entries[index];
        if (entry->state == VIGIL_MAP_ENTRY_EMPTY)
        {
            if (first_tombstone != SIZE_MAX)
            {
                return first_tombstone;
            }

            return index;
        }

        if (entry->state == VIGIL_MAP_ENTRY_TOMBSTONE)
        {
            if (first_tombstone == SIZE_MAX)
            {
                first_tombstone = index;
            }
        }
        else if (entry->hash == hash && vigil_map_key_matches_string(&entry->key, key, key_length))
        {
            *out_found = 1;
            return index;
        }

        index = (index + 1U) & (capacity - 1U);
    }
}

static vigil_status_t vigil_map_rebuild(vigil_map_t *map, size_t capacity, vigil_error_t *error)
{
    vigil_status_t status;
    vigil_map_entry_t *old_entries;
    vigil_map_entry_t *new_entries;
    size_t old_capacity;
    size_t index;
    void *memory;

    old_entries = vigil_map_entries(map);
    old_capacity = map->capacity;
    new_entries = NULL;
    memory = NULL;

    if (capacity > SIZE_MAX / sizeof(*new_entries))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map capacity would overflow");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_runtime_alloc(map->runtime, capacity * sizeof(*new_entries), &memory, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    new_entries = (vigil_map_entry_t *)memory;
    memset(new_entries, 0, capacity * sizeof(*new_entries));
    for (index = 0U; index < old_capacity; index += 1U)
    {
        vigil_map_entry_t *old_entry;
        vigil_map_entry_t *new_entry;
        int found;
        size_t new_index;

        old_entry = &old_entries[index];
        if (old_entry->state != VIGIL_MAP_ENTRY_OCCUPIED)
        {
            continue;
        }

        new_index = vigil_map_find_index_value(new_entries, capacity, &old_entry->key, old_entry->hash, &found);
        new_entry = &new_entries[new_index];
        *new_entry = *old_entry;
        memset(old_entry, 0, sizeof(*old_entry));
    }

    memory = old_entries;
    vigil_runtime_free(map->runtime, &memory);
    map->entries = new_entries;
    map->capacity = capacity;
    map->tombstone_count = 0U;
    return VIGIL_STATUS_OK;
}

static vigil_status_t vigil_map_ensure_capacity(vigil_map_t *map, vigil_error_t *error)
{
    size_t capacity;

    if (map->capacity != 0U)
    {
        if ((map->count + map->tombstone_count + 1U) * 4U <= map->capacity * 3U)
        {
            vigil_error_clear(error);
            return VIGIL_STATUS_OK;
        }
    }

    capacity = vigil_map_next_capacity(map->capacity);
    while ((map->count + 1U) * 4U > capacity * 3U)
    {
        if (capacity > SIZE_MAX / 2U)
        {
            vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map capacity would overflow");
            return VIGIL_STATUS_INVALID_ARGUMENT;
        }

        capacity *= 2U;
    }

    return vigil_map_rebuild(map, capacity, error);
}

void vigil_map_init(vigil_map_t *map, vigil_runtime_t *runtime)
{
    if (map == NULL)
    {
        return;
    }

    memset(map, 0, sizeof(*map));
    map->runtime = runtime;
}

void vigil_map_clear(vigil_map_t *map)
{
    size_t index;
    vigil_map_entry_t *entries;

    if (map == NULL || map->entries == NULL)
    {
        return;
    }

    entries = vigil_map_entries(map);
    for (index = 0U; index < map->capacity; index += 1U)
    {
        if (entries[index].state == VIGIL_MAP_ENTRY_OCCUPIED)
        {
            vigil_map_entry_clear(&entries[index]);
        }
        else if (entries[index].state == VIGIL_MAP_ENTRY_TOMBSTONE)
        {
            memset(&entries[index], 0, sizeof(entries[index]));
        }
    }

    map->count = 0U;
    map->tombstone_count = 0U;
}

void vigil_map_free(vigil_map_t *map)
{
    void *memory;

    if (map == NULL)
    {
        return;
    }

    vigil_map_clear(map);
    memory = map->entries;
    if (map->runtime != NULL)
    {
        vigil_runtime_free(map->runtime, &memory);
    }
    memset(map, 0, sizeof(*map));
}

size_t vigil_map_count(const vigil_map_t *map)
{
    if (map == NULL)
    {
        return 0U;
    }

    return map->count;
}

vigil_status_t vigil_map_set(vigil_map_t *map, const char *key, size_t key_length, const vigil_value_t *value,
                             vigil_error_t *error)
{
    vigil_object_t *key_object;
    vigil_value_t key_value;
    vigil_status_t status;
    size_t index;
    int found;
    uint64_t hash;
    vigil_map_entry_t *entry;
    vigil_value_t copied_value;

    if (!vigil_map_validate_mutable(map, error))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_map_ensure_capacity(map, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    hash = vigil_map_hash_bytes(key, key_length);
    index = vigil_map_find_index_string(vigil_map_entries(map), map->capacity, key, key_length, hash, &found);
    entry = &vigil_map_entries(map)[index];
    copied_value = vigil_value_copy(value);

    if (found)
    {
        vigil_value_release(&entry->value);
        entry->value = copied_value;
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    key_object = NULL;
    status = vigil_string_object_new(map->runtime, key, key_length, &key_object, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_value_release(&copied_value);
        return status;
    }

    if (entry->state == VIGIL_MAP_ENTRY_TOMBSTONE)
    {
        map->tombstone_count -= 1U;
    }

    vigil_value_init_object(&key_value, &key_object);
    entry->state = VIGIL_MAP_ENTRY_OCCUPIED;
    entry->hash = hash;
    entry->key = key_value;
    entry->value = copied_value;
    map->count += 1U;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_map_set_value(vigil_map_t *map, const vigil_value_t *key, const vigil_value_t *value,
                                   vigil_error_t *error)
{
    vigil_status_t status;
    uint64_t hash;
    size_t index;
    int found;
    vigil_map_entry_t *entry;
    vigil_value_t copied_value;

    if (!vigil_map_validate_mutable(map, error))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (!vigil_map_key_value_is_supported(key))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must be an integer, bool, or string");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (value == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map value must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    status = vigil_map_ensure_capacity(map, error);
    if (status != VIGIL_STATUS_OK)
    {
        return status;
    }

    hash = vigil_map_hash_value(key);
    index = vigil_map_find_index_value(vigil_map_entries(map), map->capacity, key, hash, &found);
    entry = &vigil_map_entries(map)[index];
    copied_value = vigil_value_copy(value);

    if (found)
    {
        vigil_value_release(&entry->value);
        entry->value = copied_value;
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    if (entry->state == VIGIL_MAP_ENTRY_TOMBSTONE)
    {
        map->tombstone_count -= 1U;
    }

    entry->state = VIGIL_MAP_ENTRY_OCCUPIED;
    entry->hash = hash;
    entry->key = vigil_value_copy(key);
    entry->value = copied_value;
    map->count += 1U;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_map_set_cstr(vigil_map_t *map, const char *key, const vigil_value_t *value, vigil_error_t *error)
{
    size_t key_length;

    if (key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    key_length = strlen(key);
    return vigil_map_set(map, key, key_length, value, error);
}

const vigil_value_t *vigil_map_get(const vigil_map_t *map, const char *key, size_t key_length)
{
    int found;
    size_t index;
    uint64_t hash;
    const vigil_map_entry_t *entries;

    if (map == NULL || key == NULL || map->capacity == 0U || map->entries == NULL)
    {
        return NULL;
    }

    entries = vigil_map_const_entries(map);
    hash = vigil_map_hash_bytes(key, key_length);
    index = vigil_map_find_index_string(entries, map->capacity, key, key_length, hash, &found);
    if (!found)
    {
        return NULL;
    }

    return &entries[index].value;
}

const vigil_value_t *vigil_map_get_cstr(const vigil_map_t *map, const char *key)
{
    if (key == NULL)
    {
        return NULL;
    }

    return vigil_map_get(map, key, strlen(key));
}

const vigil_value_t *vigil_map_get_value(const vigil_map_t *map, const vigil_value_t *key)
{
    int found;
    size_t index;
    uint64_t hash;
    const vigil_map_entry_t *entries;

    if (map == NULL || key == NULL || !vigil_map_key_value_is_supported(key) || map->capacity == 0U ||
        map->entries == NULL)
    {
        return NULL;
    }

    entries = vigil_map_const_entries(map);
    hash = vigil_map_hash_value(key);
    index = vigil_map_find_index_value(entries, map->capacity, key, hash, &found);
    if (!found)
    {
        return NULL;
    }

    return &entries[index].value;
}

int vigil_map_contains(const vigil_map_t *map, const char *key, size_t key_length)
{
    return vigil_map_get(map, key, key_length) != NULL;
}

int vigil_map_contains_value(const vigil_map_t *map, const vigil_value_t *key)
{
    return vigil_map_get_value(map, key) != NULL;
}

int vigil_map_contains_cstr(const vigil_map_t *map, const char *key)
{
    return vigil_map_get_cstr(map, key) != NULL;
}

int vigil_map_entry_at(const vigil_map_t *map, size_t index, const char **out_key, size_t *out_key_length,
                       const vigil_value_t **out_value)
{
    const vigil_map_entry_t *entries;
    size_t entry_index;
    size_t seen;

    if (out_key != NULL)
    {
        *out_key = NULL;
    }
    if (out_key_length != NULL)
    {
        *out_key_length = 0U;
    }
    if (out_value != NULL)
    {
        *out_value = NULL;
    }

    if (map == NULL || out_key == NULL || out_key_length == NULL || out_value == NULL || index >= map->count ||
        map->entries == NULL)
    {
        return 0;
    }

    entries = vigil_map_const_entries(map);
    seen = 0U;
    for (entry_index = 0U; entry_index < map->capacity; entry_index += 1U)
    {
        if (entries[entry_index].state != VIGIL_MAP_ENTRY_OCCUPIED)
        {
            continue;
        }
        if (seen == index)
        {
            if (vigil_value_kind(&entries[entry_index].key) != VIGIL_VALUE_OBJECT ||
                vigil_value_as_object(&entries[entry_index].key) == NULL ||
                vigil_object_type(vigil_value_as_object(&entries[entry_index].key)) != VIGIL_OBJECT_STRING)
            {
                return 0;
            }
            *out_key = vigil_string_object_c_str(vigil_value_as_object(&entries[entry_index].key));
            *out_key_length = vigil_string_object_length(vigil_value_as_object(&entries[entry_index].key));
            *out_value = &entries[entry_index].value;
            return 1;
        }
        seen += 1U;
    }

    return 0;
}

int vigil_map_entry_value_at(const vigil_map_t *map, size_t index, const vigil_value_t **out_key,
                             const vigil_value_t **out_value)
{
    const vigil_map_entry_t *entries;
    size_t entry_index;
    size_t seen;

    if (out_key != NULL)
    {
        *out_key = NULL;
    }
    if (out_value != NULL)
    {
        *out_value = NULL;
    }

    if (map == NULL || out_key == NULL || out_value == NULL || index >= map->count || map->entries == NULL)
    {
        return 0;
    }

    entries = vigil_map_const_entries(map);
    seen = 0U;
    for (entry_index = 0U; entry_index < map->capacity; entry_index += 1U)
    {
        if (entries[entry_index].state != VIGIL_MAP_ENTRY_OCCUPIED)
        {
            continue;
        }
        if (seen == index)
        {
            *out_key = &entries[entry_index].key;
            *out_value = &entries[entry_index].value;
            return 1;
        }
        seen += 1U;
    }

    return 0;
}

vigil_status_t vigil_map_remove_value(vigil_map_t *map, const vigil_value_t *key, int *out_removed,
                                      vigil_error_t *error)
{
    uint64_t hash;
    size_t index;
    int found;
    vigil_map_entry_t *entry;

    if (out_removed != NULL)
    {
        *out_removed = 0;
    }

    if (!vigil_map_validate_mutable(map, error))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (!vigil_map_key_value_is_supported(key))
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must be an integer, bool, or string");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_removed == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_removed must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (map->capacity == 0U || map->entries == NULL)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    hash = vigil_map_hash_value(key);
    index = vigil_map_find_index_value(vigil_map_entries(map), map->capacity, key, hash, &found);
    if (!found)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    entry = &vigil_map_entries(map)[index];
    vigil_value_release(&entry->key);
    vigil_value_release(&entry->value);
    entry->state = VIGIL_MAP_ENTRY_TOMBSTONE;
    entry->hash = 0U;
    map->count -= 1U;
    map->tombstone_count += 1U;
    *out_removed = 1;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_map_remove(vigil_map_t *map, const char *key, size_t key_length, int *out_removed,
                                vigil_error_t *error)
{
    uint64_t hash;
    size_t index;
    int found;
    vigil_map_entry_t *entry;

    if (out_removed != NULL)
    {
        *out_removed = 0;
    }

    if (!vigil_map_validate_mutable(map, error))
    {
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (out_removed == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "out_removed must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    if (map->capacity == 0U || map->entries == NULL)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    hash = vigil_map_hash_bytes(key, key_length);
    index = vigil_map_find_index_string(vigil_map_entries(map), map->capacity, key, key_length, hash, &found);
    if (!found)
    {
        vigil_error_clear(error);
        return VIGIL_STATUS_OK;
    }

    entry = &vigil_map_entries(map)[index];
    vigil_value_release(&entry->key);
    vigil_value_release(&entry->value);
    entry->state = VIGIL_MAP_ENTRY_TOMBSTONE;
    entry->hash = 0U;
    map->count -= 1U;
    map->tombstone_count += 1U;
    *out_removed = 1;
    vigil_error_clear(error);
    return VIGIL_STATUS_OK;
}

vigil_status_t vigil_map_remove_cstr(vigil_map_t *map, const char *key, int *out_removed, vigil_error_t *error)
{
    if (key == NULL)
    {
        vigil_error_set_literal(error, VIGIL_STATUS_INVALID_ARGUMENT, "map key must not be null");
        return VIGIL_STATUS_INVALID_ARGUMENT;
    }

    return vigil_map_remove(map, key, strlen(key), out_removed, error);
}
