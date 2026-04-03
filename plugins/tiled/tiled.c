/*
 * Vigil plugin: tiled
 *
 * Tiled map editor parser (.tmj JSON and .tmx XML).
 * Covers the full Tiled 1.11 schema — tile layers, object layers,
 * image layers, group layers, tilesets, animations, properties,
 * infinite maps, flip flags, and Wang sets.
 *
 * See: https://github.com/bluesentinelsec/vigil/issues/367
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vigil/json.h"
#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_xml.h"
#include "platform/platform.h"

/* ── GID flip flag masks ─────────────────────────────────────────── */

#define TILED_FLIP_H UINT32_C(0x80000000)
#define TILED_FLIP_V UINT32_C(0x40000000)
#define TILED_FLIP_D UINT32_C(0x20000000)
#define TILED_GID_MASK UINT32_C(0x1FFFFFFF)

/* ── String helpers ──────────────────────────────────────────────── */

static char *tiled_strdup(const char *s)
{
    size_t len;
    char *c;
    if (s == NULL)
        return NULL;
    len = strlen(s);
    c = malloc(len + 1U);
    if (c != NULL)
        memcpy(c, s, len + 1U);
    return c;
}

static const char *json_str(const vigil_json_value_t *obj, const char *key)
{
    const vigil_json_value_t *v = vigil_json_object_get(obj, key);
    if (v == NULL || vigil_json_type(v) != VIGIL_JSON_STRING)
        return NULL;
    return vigil_json_string_value(v);
}

static int32_t json_int(const vigil_json_value_t *obj, const char *key, int32_t def)
{
    const vigil_json_value_t *v = vigil_json_object_get(obj, key);
    if (v == NULL || vigil_json_type(v) != VIGIL_JSON_NUMBER)
        return def;
    return (int32_t)vigil_json_number_value(v);
}

static double json_num(const vigil_json_value_t *obj, const char *key, double def)
{
    const vigil_json_value_t *v = vigil_json_object_get(obj, key);
    if (v == NULL || vigil_json_type(v) != VIGIL_JSON_NUMBER)
        return def;
    return vigil_json_number_value(v);
}

static int json_bool(const vigil_json_value_t *obj, const char *key, int def)
{
    const vigil_json_value_t *v = vigil_json_object_get(obj, key);
    if (v == NULL || vigil_json_type(v) != VIGIL_JSON_BOOL)
        return def;
    return vigil_json_bool_value(v);
}

/* ── Tiled data structures ───────────────────────────────────────── */

typedef struct tiled_property
{
    char *name;
    char *type;  /* "string","int","float","bool","color","file","object","class" */
    char *value; /* always stored as string; caller interprets by type */
} tiled_property_t;

typedef struct tiled_object
{
    int32_t id;
    char *name;
    char *type;
    double x, y, width, height;
    double rotation;
    int visible;
    int32_t gid; /* 0 if not a tile object */
    /* Polygon/polyline points stored as flat x,y pairs */
    double *points;
    size_t point_count; /* number of x,y pairs */
    int is_ellipse;
    int is_point;
    char *text_string;
    tiled_property_t *properties;
    size_t property_count;
} tiled_object_t;

typedef struct tiled_frame
{
    int32_t tile_id;
    int32_t duration;
} tiled_frame_t;

typedef struct tiled_tile
{
    int32_t id;
    char *type;
    char *image;
    int32_t image_width, image_height;
    tiled_frame_t *animation;
    size_t frame_count;
    tiled_property_t *properties;
    size_t property_count;
    tiled_object_t *collision_objects;
    size_t collision_count;
} tiled_tile_t;

typedef struct tiled_tileset
{
    int32_t first_gid;
    char *name;
    char *source; /* external tileset path, NULL if embedded */
    char *image;
    int32_t image_width, image_height;
    int32_t tile_width, tile_height;
    int32_t tile_count, columns;
    int32_t spacing, margin;
    tiled_tile_t *tiles;
    size_t tile_entry_count;
    tiled_property_t *properties;
    size_t property_count;
} tiled_tileset_t;

typedef struct tiled_chunk
{
    int32_t x, y, width, height;
    uint32_t *data;
    size_t data_count;
} tiled_chunk_t;

typedef struct tiled_layer tiled_layer_t;

struct tiled_layer
{
    char *name;
    char *type; /* "tilelayer","objectgroup","imagelayer","group" */
    int32_t id;
    int32_t x, y, width, height;
    double opacity;
    int visible;
    double offset_x, offset_y;
    double parallax_x, parallax_y;
    char *tint_color;
    /* Tile layer data */
    uint32_t *data;
    size_t data_count;
    tiled_chunk_t *chunks;
    size_t chunk_count;
    /* Object layer */
    tiled_object_t *objects;
    size_t object_count;
    char *draw_order;
    /* Image layer */
    char *image;
    int repeat_x, repeat_y;
    /* Group layer */
    tiled_layer_t *layers;
    size_t layer_count;
    /* Properties */
    tiled_property_t *properties;
    size_t property_count;
};

typedef struct tiled_map
{
    int32_t width, height;
    int32_t tile_width, tile_height;
    char *orientation;  /* orthogonal, isometric, staggered, hexagonal */
    char *render_order; /* right-down, right-up, left-down, left-up */
    char *background_color;
    int infinite;
    char *stagger_axis;  /* x or y */
    char *stagger_index; /* odd or even */
    int32_t hex_side_length;
    tiled_layer_t *layers;
    size_t layer_count;
    tiled_tileset_t *tilesets;
    size_t tileset_count;
    tiled_property_t *properties;
    size_t property_count;
} tiled_map_t;

/* ── Free helpers ────────────────────────────────────────────────── */

static void tiled_free_properties(tiled_property_t *props, size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++)
    {
        free(props[i].name);
        free(props[i].type);
        free(props[i].value);
    }
    free(props);
}

static void tiled_free_objects(tiled_object_t *objs, size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++)
    {
        free(objs[i].name);
        free(objs[i].type);
        free(objs[i].points);
        free(objs[i].text_string);
        tiled_free_properties(objs[i].properties, objs[i].property_count);
    }
    free(objs);
}

static void tiled_free_tiles(tiled_tile_t *tiles, size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++)
    {
        free(tiles[i].type);
        free(tiles[i].image);
        free(tiles[i].animation);
        tiled_free_properties(tiles[i].properties, tiles[i].property_count);
        tiled_free_objects(tiles[i].collision_objects, tiles[i].collision_count);
    }
    free(tiles);
}

static void tiled_free_layers(tiled_layer_t *layers, size_t count);

static void tiled_free_layer_contents(tiled_layer_t *l)
{
    size_t i;
    free(l->name);
    free(l->type);
    free(l->tint_color);
    free(l->data);
    for (i = 0U; i < l->chunk_count; i++)
        free(l->chunks[i].data);
    free(l->chunks);
    tiled_free_objects(l->objects, l->object_count);
    free(l->draw_order);
    free(l->image);
    tiled_free_layers(l->layers, l->layer_count);
    tiled_free_properties(l->properties, l->property_count);
}

static void tiled_free_layers(tiled_layer_t *layers, size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++)
        tiled_free_layer_contents(&layers[i]);
    free(layers);
}

static void tiled_free_tilesets(tiled_tileset_t *ts, size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++)
    {
        free(ts[i].name);
        free(ts[i].source);
        free(ts[i].image);
        tiled_free_tiles(ts[i].tiles, ts[i].tile_entry_count);
        tiled_free_properties(ts[i].properties, ts[i].property_count);
    }
    free(ts);
}

static void tiled_map_free(tiled_map_t *map)
{
    if (map == NULL)
        return;
    free(map->orientation);
    free(map->render_order);
    free(map->background_color);
    free(map->stagger_axis);
    free(map->stagger_index);
    tiled_free_layers(map->layers, map->layer_count);
    tiled_free_tilesets(map->tilesets, map->tileset_count);
    tiled_free_properties(map->properties, map->property_count);
    free(map);
}

/* ── JSON parsing ────────────────────────────────────────────────── */

static tiled_property_t *parse_json_properties(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    tiled_property_t *props;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    if (count == 0U)
        return NULL;
    props = calloc(count, sizeof(*props));
    if (props == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
    {
        const vigil_json_value_t *p = vigil_json_array_get(arr, i);
        const char *val_str;
        const vigil_json_value_t *val;

        props[i].name = tiled_strdup(json_str(p, "name"));
        props[i].type = tiled_strdup(json_str(p, "type"));
        val = vigil_json_object_get(p, "value");
        if (val != NULL)
        {
            if (vigil_json_type(val) == VIGIL_JSON_STRING)
                props[i].value = tiled_strdup(vigil_json_string_value(val));
            else if (vigil_json_type(val) == VIGIL_JSON_BOOL)
                props[i].value = tiled_strdup(vigil_json_bool_value(val) ? "true" : "false");
            else if (vigil_json_type(val) == VIGIL_JSON_NUMBER)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%g", vigil_json_number_value(val));
                props[i].value = tiled_strdup(buf);
            }
            else
            {
                val_str = json_str(p, "value");
                props[i].value = tiled_strdup(val_str);
            }
        }
    }
    *out_count = count;
    return props;
}

static double *parse_json_points(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    double *pts;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    pts = calloc(count * 2U, sizeof(double));
    if (pts == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
    {
        const vigil_json_value_t *pt = vigil_json_array_get(arr, i);
        pts[i * 2U] = json_num(pt, "x", 0.0);
        pts[i * 2U + 1U] = json_num(pt, "y", 0.0);
    }
    *out_count = count;
    return pts;
}

static tiled_object_t *parse_json_objects(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    tiled_object_t *objs;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    if (count == 0U)
        return NULL;
    objs = calloc(count, sizeof(*objs));
    if (objs == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
    {
        const vigil_json_value_t *o = vigil_json_array_get(arr, i);
        objs[i].id = json_int(o, "id", 0);
        objs[i].name = tiled_strdup(json_str(o, "name"));
        objs[i].type = tiled_strdup(json_str(o, "type"));
        objs[i].x = json_num(o, "x", 0.0);
        objs[i].y = json_num(o, "y", 0.0);
        objs[i].width = json_num(o, "width", 0.0);
        objs[i].height = json_num(o, "height", 0.0);
        objs[i].rotation = json_num(o, "rotation", 0.0);
        objs[i].visible = json_bool(o, "visible", 1);
        objs[i].gid = json_int(o, "gid", 0);
        objs[i].is_ellipse = json_bool(o, "ellipse", 0);
        objs[i].is_point = json_bool(o, "point", 0);
        objs[i].points = parse_json_points(vigil_json_object_get(o, "polygon"), &objs[i].point_count);
        if (objs[i].points == NULL)
            objs[i].points = parse_json_points(vigil_json_object_get(o, "polyline"), &objs[i].point_count);
        {
            const vigil_json_value_t *txt = vigil_json_object_get(o, "text");
            if (txt != NULL)
                objs[i].text_string = tiled_strdup(json_str(txt, "text"));
        }
        objs[i].properties = parse_json_properties(vigil_json_object_get(o, "properties"), &objs[i].property_count);
    }
    *out_count = count;
    return objs;
}

static uint32_t *parse_json_tile_data(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    uint32_t *data;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    data = calloc(count, sizeof(*data));
    if (data == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
    {
        const vigil_json_value_t *v = vigil_json_array_get(arr, i);
        if (v != NULL && vigil_json_type(v) == VIGIL_JSON_NUMBER)
            data[i] = (uint32_t)vigil_json_number_value(v);
    }
    *out_count = count;
    return data;
}

static tiled_chunk_t *parse_json_chunks(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    tiled_chunk_t *chunks;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    chunks = calloc(count, sizeof(*chunks));
    if (chunks == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
    {
        const vigil_json_value_t *c = vigil_json_array_get(arr, i);
        chunks[i].x = json_int(c, "x", 0);
        chunks[i].y = json_int(c, "y", 0);
        chunks[i].width = json_int(c, "width", 0);
        chunks[i].height = json_int(c, "height", 0);
        chunks[i].data = parse_json_tile_data(vigil_json_object_get(c, "data"), &chunks[i].data_count);
    }
    *out_count = count;
    return chunks;
}

static tiled_layer_t *parse_json_layers(const vigil_json_value_t *arr, size_t *out_count);

static void parse_json_layer(const vigil_json_value_t *j, tiled_layer_t *l)
{
    memset(l, 0, sizeof(*l));
    l->name = tiled_strdup(json_str(j, "name"));
    l->type = tiled_strdup(json_str(j, "type"));
    l->id = json_int(j, "id", 0);
    l->x = json_int(j, "x", 0);
    l->y = json_int(j, "y", 0);
    l->width = json_int(j, "width", 0);
    l->height = json_int(j, "height", 0);
    l->opacity = json_num(j, "opacity", 1.0);
    l->visible = json_bool(j, "visible", 1);
    l->offset_x = json_num(j, "offsetx", 0.0);
    l->offset_y = json_num(j, "offsety", 0.0);
    l->parallax_x = json_num(j, "parallaxx", 1.0);
    l->parallax_y = json_num(j, "parallaxy", 1.0);
    l->tint_color = tiled_strdup(json_str(j, "tintcolor"));
    l->data = parse_json_tile_data(vigil_json_object_get(j, "data"), &l->data_count);
    l->chunks = parse_json_chunks(vigil_json_object_get(j, "chunks"), &l->chunk_count);
    l->objects = parse_json_objects(vigil_json_object_get(j, "objects"), &l->object_count);
    l->draw_order = tiled_strdup(json_str(j, "draworder"));
    l->image = tiled_strdup(json_str(j, "image"));
    l->repeat_x = json_bool(j, "repeatx", 0);
    l->repeat_y = json_bool(j, "repeaty", 0);
    l->layers = parse_json_layers(vigil_json_object_get(j, "layers"), &l->layer_count);
    l->properties = parse_json_properties(vigil_json_object_get(j, "properties"), &l->property_count);
}

static tiled_layer_t *parse_json_layers(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    tiled_layer_t *layers;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    if (count == 0U)
        return NULL;
    layers = calloc(count, sizeof(*layers));
    if (layers == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
        parse_json_layer(vigil_json_array_get(arr, i), &layers[i]);
    *out_count = count;
    return layers;
}

static void parse_json_tile_entry(const vigil_json_value_t *j, tiled_tile_t *t)
{
    const vigil_json_value_t *anim;
    const vigil_json_value_t *coll;
    size_t k;

    memset(t, 0, sizeof(*t));
    t->id = json_int(j, "id", 0);
    t->type = tiled_strdup(json_str(j, "type"));
    t->image = tiled_strdup(json_str(j, "image"));
    t->image_width = json_int(j, "imagewidth", 0);
    t->image_height = json_int(j, "imageheight", 0);
    t->properties = parse_json_properties(vigil_json_object_get(j, "properties"), &t->property_count);

    anim = vigil_json_object_get(j, "animation");
    if (anim != NULL && vigil_json_type(anim) == VIGIL_JSON_ARRAY)
    {
        t->frame_count = vigil_json_array_count(anim);
        t->animation = calloc(t->frame_count, sizeof(*t->animation));
        if (t->animation != NULL)
        {
            for (k = 0U; k < t->frame_count; k++)
            {
                const vigil_json_value_t *f = vigil_json_array_get(anim, k);
                t->animation[k].tile_id = json_int(f, "tileid", 0);
                t->animation[k].duration = json_int(f, "duration", 0);
            }
        }
    }

    coll = vigil_json_object_get(j, "objectgroup");
    if (coll != NULL)
        t->collision_objects = parse_json_objects(vigil_json_object_get(coll, "objects"), &t->collision_count);
}

static tiled_tileset_t *parse_json_tilesets(const vigil_json_value_t *arr, size_t *out_count)
{
    size_t count, i;
    tiled_tileset_t *tilesets;

    *out_count = 0U;
    if (arr == NULL || vigil_json_type(arr) != VIGIL_JSON_ARRAY)
        return NULL;
    count = vigil_json_array_count(arr);
    if (count == 0U)
        return NULL;
    tilesets = calloc(count, sizeof(*tilesets));
    if (tilesets == NULL)
        return NULL;
    for (i = 0U; i < count; i++)
    {
        const vigil_json_value_t *ts = vigil_json_array_get(arr, i);
        const vigil_json_value_t *tiles_arr;
        tilesets[i].first_gid = json_int(ts, "firstgid", 1);
        tilesets[i].name = tiled_strdup(json_str(ts, "name"));
        tilesets[i].source = tiled_strdup(json_str(ts, "source"));
        tilesets[i].image = tiled_strdup(json_str(ts, "image"));
        tilesets[i].image_width = json_int(ts, "imagewidth", 0);
        tilesets[i].image_height = json_int(ts, "imageheight", 0);
        tilesets[i].tile_width = json_int(ts, "tilewidth", 0);
        tilesets[i].tile_height = json_int(ts, "tileheight", 0);
        tilesets[i].tile_count = json_int(ts, "tilecount", 0);
        tilesets[i].columns = json_int(ts, "columns", 0);
        tilesets[i].spacing = json_int(ts, "spacing", 0);
        tilesets[i].margin = json_int(ts, "margin", 0);
        tilesets[i].properties =
            parse_json_properties(vigil_json_object_get(ts, "properties"), &tilesets[i].property_count);

        tiles_arr = vigil_json_object_get(ts, "tiles");
        if (tiles_arr != NULL && vigil_json_type(tiles_arr) == VIGIL_JSON_ARRAY)
        {
            size_t tc = vigil_json_array_count(tiles_arr);
            size_t k;
            tilesets[i].tiles = calloc(tc, sizeof(*tilesets[i].tiles));
            if (tilesets[i].tiles != NULL)
            {
                tilesets[i].tile_entry_count = tc;
                for (k = 0U; k < tc; k++)
                    parse_json_tile_entry(vigil_json_array_get(tiles_arr, k), &tilesets[i].tiles[k]);
            }
        }
    }
    *out_count = count;
    return tilesets;
}

static tiled_map_t *parse_json_map(const vigil_json_value_t *root)
{
    tiled_map_t *map = calloc(1U, sizeof(*map));
    if (map == NULL)
        return NULL;
    map->width = json_int(root, "width", 0);
    map->height = json_int(root, "height", 0);
    map->tile_width = json_int(root, "tilewidth", 0);
    map->tile_height = json_int(root, "tileheight", 0);
    map->orientation = tiled_strdup(json_str(root, "orientation"));
    map->render_order = tiled_strdup(json_str(root, "renderorder"));
    map->background_color = tiled_strdup(json_str(root, "backgroundcolor"));
    map->infinite = json_bool(root, "infinite", 0);
    map->stagger_axis = tiled_strdup(json_str(root, "staggeraxis"));
    map->stagger_index = tiled_strdup(json_str(root, "staggerindex"));
    map->hex_side_length = json_int(root, "hexsidelength", 0);
    map->layers = parse_json_layers(vigil_json_object_get(root, "layers"), &map->layer_count);
    map->tilesets = parse_json_tilesets(vigil_json_object_get(root, "tilesets"), &map->tileset_count);
    map->properties = parse_json_properties(vigil_json_object_get(root, "properties"), &map->property_count);
    return map;
}

/* ── Map handle registry ─────────────────────────────────────────── */

#define TILED_MAX_MAPS 64
static tiled_map_t *g_maps[TILED_MAX_MAPS];
static int32_t g_map_count = 0;

static int32_t tiled_store_map(tiled_map_t *map)
{
    int32_t i;
    for (i = 0; i < TILED_MAX_MAPS; i++)
    {
        if (g_maps[i] == NULL)
        {
            g_maps[i] = map;
            if (i >= g_map_count)
                g_map_count = i + 1;
            return i;
        }
    }
    return -1;
}

static tiled_map_t *tiled_get_map(int32_t handle)
{
    if (handle < 0 || handle >= TILED_MAX_MAPS)
        return NULL;
    return g_maps[handle];
}

/* ── VM helpers ──────────────────────────────────────────────────── */

static vigil_status_t tiled_push_string(vigil_vm_t *vm, const char *text, vigil_error_t *error)
{
    vigil_object_t *str = NULL;
    vigil_value_t val;
    vigil_status_t status;
    if (text == NULL)
        text = "";
    status = vigil_string_object_new(vigil_vm_runtime(vm), text, strlen(text), &str, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &str);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

static vigil_status_t tiled_push_i32(vigil_vm_t *vm, int32_t v, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_int(&val, (int64_t)v);
    return vigil_vm_stack_push(vm, &val, error);
}

/* Suppress unused warnings for helpers that will be used in later phases. */
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

static vigil_status_t tiled_push_f64(vigil_vm_t *vm, double v, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_float(&val, v);
    return vigil_vm_stack_push(vm, &val, error);
}

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

static vigil_status_t tiled_push_bool(vigil_vm_t *vm, int v, vigil_error_t *error)
{
    vigil_value_t val;
    vigil_value_init_bool(&val, v);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t tiled_push_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_object_t *err_obj = NULL;
    vigil_value_t val;
    vigil_status_t status;
    status = vigil_error_object_new_cstr(vigil_vm_runtime(vm), msg, 10, &err_obj, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    vigil_value_init_object(&val, &err_obj);
    status = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return status;
}

static vigil_status_t tiled_push_ok_err(vigil_vm_t *vm, vigil_error_t *error)
{
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

/* ── Core parse-from-text helper ──────────────────────────────────── */

static tiled_map_t *tiled_parse_text(const char *text, size_t text_len, const char *format, char *err_msg,
                                     size_t err_msg_size)
{
    int is_json = 0;

    if (format != NULL && format[0] != '\0')
        is_json = (strcmp(format, "json") == 0);
    else
        is_json = (text_len > 0U && text[0] == '{');

    if (is_json)
    {
        vigil_json_value_t *json = NULL;
        vigil_error_t parse_err = {0};
        tiled_map_t *map;
        if (vigil_json_parse(NULL, text, text_len, &json, &parse_err) != VIGIL_STATUS_OK || json == NULL)
        {
            snprintf(err_msg, err_msg_size, "failed to parse tiled JSON");
            vigil_json_free(&json);
            return NULL;
        }
        map = parse_json_map(json);
        vigil_json_free(&json);
        if (map == NULL)
            snprintf(err_msg, err_msg_size, "failed to build tiled map from JSON");
        return map;
    }
    else
    {
        snprintf(err_msg, err_msg_size, "XML Tiled map parsing is not yet implemented");
        return NULL;
    }
}

static vigil_status_t tiled_store_and_push(vigil_vm_t *vm, tiled_map_t *map, vigil_error_t *error)
{
    int32_t handle = tiled_store_map(map);
    vigil_status_t status;
    if (handle < 0)
    {
        tiled_map_free(map);
        status = tiled_push_i32(vm, -1, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return tiled_push_err(vm, "too many tiled maps open", error);
    }
    status = tiled_push_i32(vm, handle, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    return tiled_push_ok_err(vm, error);
}

/* ── tiled.load(path) -> (i32, err) ──────────────────────────────── */

static vigil_status_t tiled_load_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t path_val = vigil_vm_stack_get(vm, base);
    vigil_object_t *path_obj = vigil_value_as_object(&path_val);
    const char *path;
    size_t path_len;
    char *file_data = NULL;
    size_t file_len = 0U;
    vigil_status_t status;
    tiled_map_t *map;
    char err_msg[128];
    const char *format;

    if (path_obj == NULL || vigil_object_type(path_obj) != VIGIL_OBJECT_STRING)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        status = tiled_push_i32(vm, -1, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return tiled_push_err(vm, "tiled.load() requires a string path", error);
    }
    path = vigil_string_object_c_str(path_obj);
    path_len = vigil_string_object_length(path_obj);

    status = vigil_platform_read_file(NULL, path, &file_data, &file_len, error);
    if (status != VIGIL_STATUS_OK)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        status = tiled_push_i32(vm, -1, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return tiled_push_err(vm, "failed to read tiled map file", error);
    }

    format = "json";
    if (path_len >= 4U && (strcmp(path + path_len - 4U, ".tmx") == 0))
        format = "xml";

    err_msg[0] = '\0';
    map = tiled_parse_text(file_data, file_len, format, err_msg, sizeof(err_msg));
    free(file_data);

    vigil_vm_stack_pop_n(vm, arg_count);
    if (map == NULL)
    {
        status = tiled_push_i32(vm, -1, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return tiled_push_err(vm, err_msg[0] != '\0' ? err_msg : "failed to parse tiled map", error);
    }
    return tiled_store_and_push(vm, map, error);
}

/* ── tiled.parse(text, format) -> (i32, err) ─────────────────────── */

static vigil_status_t tiled_parse_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t text_val = vigil_vm_stack_get(vm, base);
    vigil_value_t fmt_val = vigil_vm_stack_get(vm, base + 1U);
    vigil_object_t *text_obj = vigil_value_as_object(&text_val);
    vigil_object_t *fmt_obj = vigil_value_as_object(&fmt_val);
    const char *text;
    size_t text_len;
    const char *format;
    tiled_map_t *map;
    char err_msg[128];
    vigil_status_t status;

    if (text_obj == NULL || vigil_object_type(text_obj) != VIGIL_OBJECT_STRING || fmt_obj == NULL ||
        vigil_object_type(fmt_obj) != VIGIL_OBJECT_STRING)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        status = tiled_push_i32(vm, -1, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return tiled_push_err(vm, "tiled.parse() requires (string text, string format)", error);
    }
    text = vigil_string_object_c_str(text_obj);
    text_len = vigil_string_object_length(text_obj);
    format = vigil_string_object_c_str(fmt_obj);

    err_msg[0] = '\0';
    map = tiled_parse_text(text, text_len, format, err_msg, sizeof(err_msg));

    vigil_vm_stack_pop_n(vm, arg_count);
    if (map == NULL)
    {
        status = tiled_push_i32(vm, -1, error);
        if (status != VIGIL_STATUS_OK)
            return status;
        return tiled_push_err(vm, err_msg[0] != '\0' ? err_msg : "failed to parse tiled map", error);
    }
    return tiled_store_and_push(vm, map, error);
}

/* ── Map accessor functions ──────────────────────────────────────── */

static tiled_map_t *tiled_get_map_arg(vigil_vm_t *vm, size_t base)
{
    vigil_value_t val = vigil_vm_stack_get(vm, base);
    return tiled_get_map((int32_t)vigil_value_as_int(&val));
}

static vigil_status_t tiled_map_width_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, map != NULL ? map->width : 0, error);
}

static vigil_status_t tiled_map_height_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, map != NULL ? map->height : 0, error);
}

static vigil_status_t tiled_map_tile_width_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, map != NULL ? map->tile_width : 0, error);
}

static vigil_status_t tiled_map_tile_height_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, map != NULL ? map->tile_height : 0, error);
}

static vigil_status_t tiled_map_orientation_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_string(vm, map != NULL ? map->orientation : "", error);
}

static vigil_status_t tiled_map_layer_count_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, map != NULL ? (int32_t)map->layer_count : 0, error);
}

static vigil_status_t tiled_map_tileset_count_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, map != NULL ? (int32_t)map->tileset_count : 0, error);
}

static vigil_status_t tiled_map_infinite_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_map_t *map = tiled_get_map_arg(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_bool(vm, map != NULL ? map->infinite : 0, error);
}

/* ── Layer accessor functions ────────────────────────────────────── */

static tiled_layer_t *tiled_get_layer(vigil_vm_t *vm, size_t base)
{
    vigil_value_t map_val = vigil_vm_stack_get(vm, base);
    vigil_value_t idx_val = vigil_vm_stack_get(vm, base + 1U);
    tiled_map_t *map = tiled_get_map((int32_t)vigil_value_as_int(&map_val));
    int32_t idx = (int32_t)vigil_value_as_int(&idx_val);
    if (map == NULL || idx < 0 || (size_t)idx >= map->layer_count)
        return NULL;
    return &map->layers[idx];
}

static vigil_status_t tiled_layer_name_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_layer_t *l = tiled_get_layer(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_string(vm, l != NULL ? l->name : "", error);
}

static vigil_status_t tiled_layer_type_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_layer_t *l = tiled_get_layer(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_string(vm, l != NULL ? l->type : "", error);
}

static vigil_status_t tiled_layer_width_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_layer_t *l = tiled_get_layer(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, l != NULL ? l->width : 0, error);
}

static vigil_status_t tiled_layer_height_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_layer_t *l = tiled_get_layer(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, l != NULL ? l->height : 0, error);
}

static vigil_status_t tiled_layer_data_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    tiled_layer_t *l = tiled_get_layer(vm, base);
    vigil_object_t *arr = NULL;
    vigil_value_t arr_val;
    vigil_status_t status;
    size_t i;

    vigil_vm_stack_pop_n(vm, arg_count);
    status = vigil_array_object_new(vigil_vm_runtime(vm), NULL, 0U, &arr, error);
    if (status != VIGIL_STATUS_OK)
        return status;
    if (l != NULL && l->data != NULL)
    {
        for (i = 0U; i < l->data_count; i++)
        {
            vigil_value_t v;
            vigil_value_init_int(&v, (int64_t)(l->data[i] & TILED_GID_MASK));
            status = vigil_array_object_append(arr, &v, error);
            vigil_value_release(&v);
            if (status != VIGIL_STATUS_OK)
            {
                vigil_object_release(&arr);
                return status;
            }
        }
    }
    vigil_value_init_object(&arr_val, &arr);
    status = vigil_vm_stack_push(vm, &arr_val, error);
    vigil_value_release(&arr_val);
    return status;
}

static vigil_status_t tiled_layer_object_count_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_layer_t *l = tiled_get_layer(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, l != NULL ? (int32_t)l->object_count : 0, error);
}

/* ── Tileset accessor functions ──────────────────────────────────── */

static tiled_tileset_t *tiled_get_tileset(vigil_vm_t *vm, size_t base)
{
    vigil_value_t map_val = vigil_vm_stack_get(vm, base);
    vigil_value_t idx_val = vigil_vm_stack_get(vm, base + 1U);
    tiled_map_t *map = tiled_get_map((int32_t)vigil_value_as_int(&map_val));
    int32_t idx = (int32_t)vigil_value_as_int(&idx_val);
    if (map == NULL || idx < 0 || (size_t)idx >= map->tileset_count)
        return NULL;
    return &map->tilesets[idx];
}

static vigil_status_t tiled_tileset_name_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_string(vm, ts != NULL ? ts->name : "", error);
}

static vigil_status_t tiled_tileset_first_gid_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, ts != NULL ? ts->first_gid : 0, error);
}

static vigil_status_t tiled_tileset_image_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_string(vm, ts != NULL ? ts->image : "", error);
}

static vigil_status_t tiled_tileset_tile_width_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, ts != NULL ? ts->tile_width : 0, error);
}

static vigil_status_t tiled_tileset_tile_height_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, ts != NULL ? ts->tile_height : 0, error);
}

static vigil_status_t tiled_tileset_tile_count_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, ts != NULL ? ts->tile_count : 0, error);
}

static vigil_status_t tiled_tileset_columns_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    tiled_tileset_t *ts = tiled_get_tileset(vm, vigil_vm_stack_depth(vm) - arg_count);
    vigil_vm_stack_pop_n(vm, arg_count);
    return tiled_push_i32(vm, ts != NULL ? ts->columns : 0, error);
}

/* ── Close function ──────────────────────────────────────────────── */

static vigil_status_t tiled_close_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    vigil_value_t val = vigil_vm_stack_get(vm, base);
    int32_t handle = (int32_t)vigil_value_as_int(&val);
    vigil_vm_stack_pop_n(vm, arg_count);
    if (handle >= 0 && handle < TILED_MAX_MAPS && g_maps[handle] != NULL)
    {
        tiled_map_free(g_maps[handle]);
        g_maps[handle] = NULL;
    }
    return tiled_push_i32(vm, 0, error);
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int i32_param[] = {VIGIL_TYPE_I32};
static const int str_param[] = {VIGIL_TYPE_STRING};
static const int str_str_param[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int i32_i32_param[] = {VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int i32_err_returns[] = {VIGIL_TYPE_I32, VIGIL_TYPE_ERR};

static const char *path_names[] = {"path"};
static const char *text_format_names[] = {"text", "format"};
static const char *handle_names[] = {"handle"};
static const char *handle_index_names[] = {"handle", "index"};

static const vigil_native_symbol_doc_t tiled_module_doc = {
    "Tiled map parser.", "Parse Tiled .tmj/.tmx map files. Returns an integer handle used to query map data.",
    "i32 h, err e = tiled.load(\"level.tmj\");\n"
    "i32 w = tiled.map_width(h);\n"
    "string name = tiled.layer_name(h, 0);\n"
    "tiled.close(h);"};
static const vigil_native_symbol_doc_t tiled_load_doc = {
    "Load a Tiled map from a file.",
    "Parses a .tmj (JSON) or .tmx (XML) file and returns a map handle. "
    "The format is detected from the file extension.",
    "i32 h, err e = tiled.load(\"level.tmj\");"};
static const vigil_native_symbol_doc_t tiled_parse_doc = {
    "Parse a Tiled map from a string.",
    "Parses Tiled map data from a string in memory. The format argument must be \"json\" or \"xml\". "
    "Use this when loading from archives, network, or embedded data.",
    "i32 h, err e = tiled.parse(map_text, \"json\");"};
static const vigil_native_symbol_doc_t tiled_close_doc = {
    "Close a Tiled map.", "Frees the parsed map data associated with the handle.", NULL};
static const vigil_native_symbol_doc_t tiled_width_doc = {"Map width in tiles.",
                                                          "Returns the number of tile columns in the map.", NULL};
static const vigil_native_symbol_doc_t tiled_height_doc = {"Map height in tiles.",
                                                           "Returns the number of tile rows in the map.", NULL};
static const vigil_native_symbol_doc_t tiled_tw_doc = {"Tile width in pixels.",
                                                       "Returns the width of each tile in pixels.", NULL};
static const vigil_native_symbol_doc_t tiled_th_doc = {"Tile height in pixels.",
                                                       "Returns the height of each tile in pixels.", NULL};
static const vigil_native_symbol_doc_t tiled_orient_doc = {
    "Map orientation.", "Returns \"orthogonal\", \"isometric\", \"staggered\", or \"hexagonal\".", NULL};
static const vigil_native_symbol_doc_t tiled_lc_doc = {"Number of top-level layers.",
                                                       "Returns the count of layers at the root of the map.", NULL};
static const vigil_native_symbol_doc_t tiled_tsc_doc = {"Number of tilesets.",
                                                        "Returns the count of tilesets referenced by the map.", NULL};
static const vigil_native_symbol_doc_t tiled_inf_doc = {
    "Whether the map is infinite.", "Infinite maps use chunked tile data instead of a flat array.", NULL};
static const vigil_native_symbol_doc_t tiled_ln_doc = {"Layer name.",
                                                       "Returns the name of the layer at the given index.", NULL};
static const vigil_native_symbol_doc_t tiled_lt_doc = {
    "Layer type.", "Returns \"tilelayer\", \"objectgroup\", \"imagelayer\", or \"group\".", NULL};
static const vigil_native_symbol_doc_t tiled_lw_doc = {"Layer width in tiles.", "Returns the width of the tile layer.",
                                                       NULL};
static const vigil_native_symbol_doc_t tiled_lh_doc = {"Layer height in tiles.",
                                                       "Returns the height of the tile layer.", NULL};
static const vigil_native_symbol_doc_t tiled_ld_doc = {
    "Tile layer GID data.",
    "Returns an array of tile GIDs for the layer. Flip flags are masked off. A GID of 0 means an empty tile.", NULL};
static const vigil_native_symbol_doc_t tiled_loc_doc = {
    "Object count in layer.", "Returns the number of objects in an object group layer.", NULL};
static const vigil_native_symbol_doc_t tiled_tsn_doc = {"Tileset name.",
                                                        "Returns the name of the tileset at the given index.", NULL};
static const vigil_native_symbol_doc_t tiled_tsg_doc = {
    "Tileset first GID.",
    "Returns the first global tile ID for this tileset. Subtract from a tile GID to get the local tile index.", NULL};
static const vigil_native_symbol_doc_t tiled_tsi_doc = {"Tileset image path.",
                                                        "Returns the relative path to the tileset image file.", NULL};
static const vigil_native_symbol_doc_t tiled_tstw_doc = {
    "Tileset tile width.", "Returns the width of each tile in the tileset in pixels.", NULL};
static const vigil_native_symbol_doc_t tiled_tsth_doc = {
    "Tileset tile height.", "Returns the height of each tile in the tileset in pixels.", NULL};
static const vigil_native_symbol_doc_t tiled_tstc_doc = {"Tileset tile count.",
                                                         "Returns the total number of tiles in the tileset.", NULL};
static const vigil_native_symbol_doc_t tiled_tscol_doc = {
    "Tileset columns.", "Returns the number of tile columns in the tileset image.", NULL};

static const vigil_native_module_function_t tiled_functions[] = {
    /* load(path) -> (i32, err) */
    {"load", 4U, tiled_load_fn, 1U, str_param, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, NULL, 0U, path_names, NULL,
     NULL, &tiled_load_doc},
    /* parse(text, format) -> (i32, err) */
    {"parse", 5U, tiled_parse_fn, 2U, str_str_param, VIGIL_TYPE_I32, 2U, i32_err_returns, 0, NULL, NULL, 0U,
     text_format_names, NULL, NULL, &tiled_parse_doc},
    /* close(handle) -> i32 */
    {"close", 5U, tiled_close_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, handle_names, NULL, NULL,
     &tiled_close_doc},
    /* Map accessors: map_width(handle) -> i32 */
    {"map_width", 9U, tiled_map_width_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, handle_names,
     NULL, NULL, &tiled_width_doc},
    {"map_height", 10U, tiled_map_height_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U, handle_names,
     NULL, NULL, &tiled_height_doc},
    {"map_tile_width", 14U, tiled_map_tile_width_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_names, NULL, NULL, &tiled_tw_doc},
    {"map_tile_height", 15U, tiled_map_tile_height_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_names, NULL, NULL, &tiled_th_doc},
    {"map_orientation", 15U, tiled_map_orientation_fn, 1U, i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     handle_names, NULL, NULL, &tiled_orient_doc},
    {"map_layer_count", 15U, tiled_map_layer_count_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_names, NULL, NULL, &tiled_lc_doc},
    {"map_tileset_count", 17U, tiled_map_tileset_count_fn, 1U, i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_names, NULL, NULL, &tiled_tsc_doc},
    {"map_infinite", 12U, tiled_map_infinite_fn, 1U, i32_param, VIGIL_TYPE_BOOL, 1U, NULL, 0, NULL, NULL, 0U,
     handle_names, NULL, NULL, &tiled_inf_doc},
    /* Layer accessors: layer_name(handle, index) -> string */
    {"layer_name", 10U, tiled_layer_name_fn, 2U, i32_i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_ln_doc},
    {"layer_type", 10U, tiled_layer_type_fn, 2U, i32_i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_lt_doc},
    {"layer_width", 11U, tiled_layer_width_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_lw_doc},
    {"layer_height", 12U, tiled_layer_height_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_lh_doc},
    {"layer_data", 10U, tiled_layer_data_fn, 2U, i32_i32_param, VIGIL_TYPE_OBJECT, 1U, NULL, 0, NULL, NULL,
     VIGIL_TYPE_I32, handle_index_names, NULL, NULL, &tiled_ld_doc},
    {"layer_object_count", 18U, tiled_layer_object_count_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL,
     0U, handle_index_names, NULL, NULL, &tiled_loc_doc},
    /* Tileset accessors: tileset_name(handle, index) -> string */
    {"tileset_name", 12U, tiled_tileset_name_fn, 2U, i32_i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_tsn_doc},
    {"tileset_first_gid", 17U, tiled_tileset_first_gid_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL,
     0U, handle_index_names, NULL, NULL, &tiled_tsg_doc},
    {"tileset_image", 13U, tiled_tileset_image_fn, 2U, i32_i32_param, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_tsi_doc},
    {"tileset_tile_width", 18U, tiled_tileset_tile_width_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL,
     0U, handle_index_names, NULL, NULL, &tiled_tstw_doc},
    {"tileset_tile_height", 19U, tiled_tileset_tile_height_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL,
     NULL, 0U, handle_index_names, NULL, NULL, &tiled_tsth_doc},
    {"tileset_tile_count", 18U, tiled_tileset_tile_count_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL,
     0U, handle_index_names, NULL, NULL, &tiled_tstc_doc},
    {"tileset_columns", 15U, tiled_tileset_columns_fn, 2U, i32_i32_param, VIGIL_TYPE_I32, 1U, NULL, 0, NULL, NULL, 0U,
     handle_index_names, NULL, NULL, &tiled_tscol_doc},
};

VIGIL_API const vigil_native_module_t vigil_plugin_tiled = {
    "tiled", 5U, tiled_functions, sizeof(tiled_functions) / sizeof(tiled_functions[0]), NULL, 0U, &tiled_module_doc,
    NULL,    0U};
