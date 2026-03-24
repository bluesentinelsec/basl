#include "vigil_test.h"

#include <string.h>

#include "vigil/vigil.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

static vigil_object_t *MakeInstance(int *vigil_test_failed_, vigil_runtime_t *runtime, size_t field_count)
{
    vigil_object_t *obj = NULL;
    vigil_error_t error = {0};
    vigil_value_t fields[8];
    size_t i;

    for (i = 0; i < field_count && i < 8; i++)
        vigil_value_init_nil(&fields[i]);

    EXPECT_EQ(vigil_instance_object_new(runtime, 0, field_count > 0 ? fields : NULL, field_count, &obj, &error),
              VIGIL_STATUS_OK);
    EXPECT_NE(obj, NULL);
    return obj;
}

static vigil_object_t *MakeArray(int *vigil_test_failed_, vigil_runtime_t *runtime)
{
    vigil_object_t *obj = NULL;
    vigil_error_t error = {0};

    EXPECT_EQ(vigil_array_object_new(runtime, NULL, 0, &obj, &error), VIGIL_STATUS_OK);
    EXPECT_NE(obj, NULL);
    return obj;
}

static vigil_object_t *MakeMap(int *vigil_test_failed_, vigil_runtime_t *runtime)
{
    vigil_object_t *obj = NULL;
    vigil_error_t error = {0};

    EXPECT_EQ(vigil_map_object_new(runtime, &obj, &error), VIGIL_STATUS_OK);
    EXPECT_NE(obj, NULL);
    return obj;
}

/*
 * Set instance field to point to another object.
 * This retains the target (set_field copies the value).
 */
static void SetFieldToObject(vigil_object_t *instance, size_t index, vigil_object_t *target)
{
    vigil_error_t error = {0};
    vigil_value_t v;

    vigil_object_retain(target);
    v = 0;
    vigil_value_init_object(&v, &target);
    vigil_instance_object_set_field(instance, index, &v, &error);
    vigil_value_release(&v);
}

/* ── Tests ───────────────────────────────────────────────────────── */

TEST(VigilGcTest, CreateAndDestroy)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);
    EXPECT_NE(gc, NULL);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    EXPECT_EQ(gc, NULL);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CreateRejectsNullArgs)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    EXPECT_NE(vigil_gc_create(NULL, runtime, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    EXPECT_NE(vigil_gc_create(&gc, NULL, &error), VIGIL_STATUS_OK);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, DestroyNullIsSafe)
{
    (void)vigil_test_failed_;
    vigil_gc_destroy(NULL);
    vigil_gc_t *gc = NULL;
    vigil_gc_destroy(&gc);
}

TEST(VigilGcTest, TrackAndUntrack)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *obj = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_gc_track(gc, obj);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 1U);

    /* Double-track is a no-op. */
    vigil_gc_track(gc, obj);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 1U);

    vigil_gc_untrack(gc, obj);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    /* Untrack of non-tracked is safe. */
    vigil_gc_untrack(gc, obj);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_object_release(&obj);
    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectNoop)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    EXPECT_EQ(vigil_gc_collect(gc), 0U);
    EXPECT_EQ(vigil_gc_collect(NULL), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectDoesNotFreeExternallyReferencedObjects)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *obj = MakeInstance(vigil_test_failed_, runtime, 0);
    vigil_gc_track(gc, obj);

    EXPECT_EQ(vigil_gc_collect(gc), 0U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 1U);
    EXPECT_EQ(vigil_object_ref_count(obj), 1U);

    vigil_gc_untrack(gc, obj);
    vigil_object_release(&obj);
    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectsDirectInstanceCycle)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *a = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *b = MakeInstance(vigil_test_failed_, runtime, 1);

    /* A.field[0] = B, B.field[0] = A */
    SetFieldToObject(a, 0, b);
    SetFieldToObject(b, 0, a);

    vigil_gc_track(gc, a);
    vigil_gc_track(gc, b);

    /* Each has ref_count=2: our local + field from the other. */
    EXPECT_EQ(vigil_object_ref_count(a), 2U);
    EXPECT_EQ(vigil_object_ref_count(b), 2U);

    /* Drop local refs — only the cycle keeps them alive. */
    vigil_object_release(&a);
    vigil_object_release(&b);

    EXPECT_EQ(vigil_gc_collect(gc), 2U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectsIndirectCycleOfThree)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *a = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *b = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *c = MakeInstance(vigil_test_failed_, runtime, 1);

    /* A -> B -> C -> A */
    SetFieldToObject(a, 0, b);
    SetFieldToObject(b, 0, c);
    SetFieldToObject(c, 0, a);

    vigil_gc_track(gc, a);
    vigil_gc_track(gc, b);
    vigil_gc_track(gc, c);

    EXPECT_EQ(vigil_object_ref_count(a), 2U);

    vigil_object_release(&a);
    vigil_object_release(&b);
    vigil_object_release(&c);

    EXPECT_EQ(vigil_gc_collect(gc), 3U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, PreservesExternallyReachableCycleMember)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *a = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *b = MakeInstance(vigil_test_failed_, runtime, 1);

    SetFieldToObject(a, 0, b);
    SetFieldToObject(b, 0, a);

    vigil_gc_track(gc, a);
    vigil_gc_track(gc, b);

    /* Drop only B's local ref — A still held externally. */
    vigil_object_release(&b);

    /* A is externally reachable, B is reachable through A — nothing collected. */
    EXPECT_EQ(vigil_gc_collect(gc), 0U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 2U);

    /* Now drop A's local ref too — both become garbage. */
    vigil_object_release(&a);
    EXPECT_EQ(vigil_gc_collect(gc), 2U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectsArrayCycle)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *inst = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *arr = MakeArray(vigil_test_failed_, runtime);

    /* inst.field[0] = arr */
    SetFieldToObject(inst, 0, arr);

    /* arr.push(inst) */
    {
        vigil_value_t v;
        vigil_object_t *tmp = inst;
        vigil_object_retain(tmp);
        vigil_value_init_object(&v, &tmp);
        vigil_array_object_append(arr, &v, &error);
        vigil_value_release(&v);
    }

    vigil_gc_track(gc, inst);
    vigil_gc_track(gc, arr);

    vigil_object_release(&inst);
    vigil_object_release(&arr);

    EXPECT_EQ(vigil_gc_collect(gc), 2U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectsMapCycle)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *inst = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *map = MakeMap(vigil_test_failed_, runtime);

    /* inst.field[0] = map */
    SetFieldToObject(inst, 0, map);

    /* map[42] = inst */
    {
        vigil_value_t v, key;
        vigil_object_t *tmp = inst;
        vigil_value_init_int(&key, 42);
        vigil_object_retain(tmp);
        vigil_value_init_object(&v, &tmp);
        vigil_map_object_set(map, &key, &v, &error);
        vigil_value_release(&v);
    }

    vigil_gc_track(gc, inst);
    vigil_gc_track(gc, map);

    vigil_object_release(&inst);
    vigil_object_release(&map);

    EXPECT_EQ(vigil_gc_collect(gc), 2U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectsSelfReferencingInstance)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *obj = MakeInstance(vigil_test_failed_, runtime, 1);

    SetFieldToObject(obj, 0, obj);

    vigil_gc_track(gc, obj);
    EXPECT_EQ(vigil_object_ref_count(obj), 2U);

    vigil_object_release(&obj);

    EXPECT_EQ(vigil_gc_collect(gc), 1U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, MixedCycleAndNonCycleObjects)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *a = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *b = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *c = MakeInstance(vigil_test_failed_, runtime, 0);

    SetFieldToObject(a, 0, b);
    SetFieldToObject(b, 0, a);

    vigil_gc_track(gc, a);
    vigil_gc_track(gc, b);
    vigil_gc_track(gc, c);

    vigil_object_release(&a);
    vigil_object_release(&b);

    /* Only A and B should be collected; C survives. */
    EXPECT_EQ(vigil_gc_collect(gc), 2U);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 1U);
    EXPECT_EQ(vigil_object_ref_count(c), 1U);

    vigil_gc_untrack(gc, c);
    vigil_object_release(&c);
    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, MultipleCollectCyclesAreIdempotent)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_object_t *a = MakeInstance(vigil_test_failed_, runtime, 1);
    vigil_object_t *b = MakeInstance(vigil_test_failed_, runtime, 1);

    SetFieldToObject(a, 0, b);
    SetFieldToObject(b, 0, a);

    vigil_gc_track(gc, a);
    vigil_gc_track(gc, b);
    vigil_object_release(&a);
    vigil_object_release(&b);

    EXPECT_EQ(vigil_gc_collect(gc), 2U);
    EXPECT_EQ(vigil_gc_collect(gc), 0U);
    EXPECT_EQ(vigil_gc_collect(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, TrackNullIsSafe)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    vigil_gc_track(gc, NULL);
    vigil_gc_track(NULL, NULL);
    vigil_gc_untrack(gc, NULL);
    vigil_gc_untrack(NULL, NULL);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, CollectsLargeCycleRing)
{
    vigil_runtime_t *runtime = NULL;
    vigil_gc_t *gc = NULL;
    vigil_error_t error = {0};
    const size_t ring_size = 20;
    vigil_object_t *nodes[20];
    size_t i;

    ASSERT_EQ(vigil_runtime_open(&runtime, NULL, &error), VIGIL_STATUS_OK);
    ASSERT_EQ(vigil_gc_create(&gc, runtime, &error), VIGIL_STATUS_OK);

    for (i = 0; i < ring_size; i++)
    {
        nodes[i] = MakeInstance(vigil_test_failed_, runtime, 1);
        vigil_gc_track(gc, nodes[i]);
    }

    for (i = 0; i < ring_size; i++)
    {
        size_t next = (i + 1) % ring_size;
        SetFieldToObject(nodes[i], 0, nodes[next]);
    }

    for (i = 0; i < ring_size; i++)
        vigil_object_release(&nodes[i]);

    EXPECT_EQ(vigil_gc_collect(gc), (long long)ring_size);
    EXPECT_EQ(vigil_gc_tracked_count(gc), 0U);

    vigil_gc_destroy(&gc);
    vigil_runtime_close(&runtime);
}

TEST(VigilGcTest, TrackedCountReturnsZeroForNull)
{
    (void)vigil_test_failed_;
    EXPECT_EQ(vigil_gc_tracked_count(NULL), 0U);
}

/* ── Registration ────────────────────────────────────────────────── */

void register_gc_tests(void)
{
    REGISTER_TEST(VigilGcTest, CreateAndDestroy);
    REGISTER_TEST(VigilGcTest, CreateRejectsNullArgs);
    REGISTER_TEST(VigilGcTest, DestroyNullIsSafe);
    REGISTER_TEST(VigilGcTest, TrackAndUntrack);
    REGISTER_TEST(VigilGcTest, CollectNoop);
    REGISTER_TEST(VigilGcTest, CollectDoesNotFreeExternallyReferencedObjects);
    REGISTER_TEST(VigilGcTest, CollectsDirectInstanceCycle);
    REGISTER_TEST(VigilGcTest, CollectsIndirectCycleOfThree);
    REGISTER_TEST(VigilGcTest, PreservesExternallyReachableCycleMember);
    REGISTER_TEST(VigilGcTest, CollectsArrayCycle);
    REGISTER_TEST(VigilGcTest, CollectsMapCycle);
    REGISTER_TEST(VigilGcTest, CollectsSelfReferencingInstance);
    REGISTER_TEST(VigilGcTest, MixedCycleAndNonCycleObjects);
    REGISTER_TEST(VigilGcTest, MultipleCollectCyclesAreIdempotent);
    REGISTER_TEST(VigilGcTest, TrackNullIsSafe);
    REGISTER_TEST(VigilGcTest, CollectsLargeCycleRing);
    REGISTER_TEST(VigilGcTest, TrackedCountReturnsZeroForNull);
}
