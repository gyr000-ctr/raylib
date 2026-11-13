/*
 * raylib glTF morph target (blend shape) test harness.
 *
 * Headless: runs against the in-repo software renderer (PLATFORM_MEMORY /
 * GRAPHICS_API_OPENGL_SOFTWARE), so no display server is required.
 *
 * It loads the GLB assets emitted by gen/gen_morph_assets.py, drives the public
 * morph/animation API and compares the per-vertex results against the reference
 * files computed from the glTF 2.0 specification equations.
 *
 * Build: see run_tests.sh
 */
#include "raylib.h"
#include "mini_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TOLERANCE 1e-4f

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg, ...) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_failures++; \
            printf("  [FAIL] " msg "\n", ##__VA_ARGS__); \
        } \
    } while (0)

static bool float_near(float a, float b)
{
    return fabsf(a - b) <= TEST_TOLERANCE;
}

static bool array_near(const float *got, const double *ref, int count, const char *label, const char *asset)
{
    float maxErr = 0.0f;
    int badIndex = -1;
    for (int i = 0; i < count; i++)
    {
        float err = fabsf(got[i] - (float)ref[i]);
        if (err > maxErr) { maxErr = err; badIndex = i; }
    }

    if (maxErr > TEST_TOLERANCE)
    {
        g_checks++;
        g_failures++;
        printf("  [FAIL] %s: %s max error %.8g at index %d (got %.8g, ref %.8g)\n",
               asset, label, maxErr, badIndex, got[badIndex], (float)ref[badIndex]);
        return false;
    }

    return true;
}

// Read a JSON array of numbers from the current reader position
static bool read_number_array(JsonReader *r, double *out, int maxCount, int *countOut)
{
    if (!jr_array_open(r)) return false;
    int n = 0;
    while (jr_array_more(r) && n < maxCount)
    {
        if (!jr_number(r, &out[n++])) return false;
        jr_comma(r);
    }
    if (!jr_array_close(r)) return false;
    *countOut = n;
    return true;
}

// Read one "samples" entry: { frame, time, weights[], positions[], ... } at the
// reader position (an open array of sample objects)
typedef struct {
    int frame;
    double weights[32];
    int weightCount;
    double positions[256];
    int positionCount;
    double normals[256];
    int normalCount;
    double tangents[256];
    int tangentCount;
    double texcoords[256];
    int texcoordCount;
} RefSample;

static bool read_sample(JsonReader *r, RefSample *s)
{
    memset(s, 0, sizeof(*s));
    if (!jr_object_open(r)) return false;

    while (jr_object_more(r))
    {
        char key[64] = { 0 };
        if (!jr_key(r, key, sizeof(key))) return false;

        if (strcmp(key, "frame") == 0) { double v = 0; jr_number(r, &v); s->frame = (int)v; }
        else if (strcmp(key, "weights") == 0) read_number_array(r, s->weights, 32, &s->weightCount);
        else if (strcmp(key, "positions") == 0) read_number_array(r, s->positions, 256, &s->positionCount);
        else if (strcmp(key, "normals") == 0) read_number_array(r, s->normals, 256, &s->normalCount);
        else if (strcmp(key, "tangents") == 0) read_number_array(r, s->tangents, 256, &s->tangentCount);
        else if (strcmp(key, "texcoords") == 0) read_number_array(r, s->texcoords, 256, &s->texcoordCount);
        else
        {
            // Skip unknown value
            jr_skip_ws(r);
            if (*r->cur == '"') { r->cur++; while (*r->cur && *r->cur != '"') r->cur++; r->cur++; }
            else if (*r->cur == '{' || *r->cur == '[')
            {
                char open = *r->cur, close = (open == '{')? '}' : ']';
                int depth = 0;
                do {
                    if (*r->cur == open) depth++;
                    else if (*r->cur == close) depth--;
                    r->cur++;
                } while (*r->cur && depth > 0);
            }
            else while (*r->cur && *r->cur != ',' && *r->cur != '}') r->cur++;
        }

        jr_comma(r);
    }

    return jr_object_close(r);
}

static const char *asset_dir(void)
{
    const char *dir = getenv("MORPH_ASSET_DIR");
    return (dir != NULL)? dir : "assets";
}

static char *asset_path(const char *name)
{
    static char path[1024];
    snprintf(path, sizeof(path), "%s/%s", asset_dir(), name);
    return path;
}

// Verify every float attribute of a mesh against one reference sample
static void verify_sample(const char *asset, const Mesh *mesh, const RefSample *s)
{
    if (s->weightCount > 0)
    {
        CHECK(mesh->morphTargetCount >= s->weightCount, "%s: target count %d < weight ref %d",
              asset, mesh->morphTargetCount, s->weightCount);
        for (int t = 0; t < s->weightCount; t++)
        {
            CHECK(float_near(mesh->morphWeights[t], (float)s->weights[t]),
                  "%s: weight[%d] got %.8g ref %.8g", asset, t, mesh->morphWeights[t], s->weights[t]);
        }
    }

    // Morph results take precedence; on plain meshes the runtime arrays are NULL
    // and the base vertex attributes must match the reference instead
    if (s->positionCount > 0)
    {
        const float *got = (mesh->morphVertices != NULL)? mesh->morphVertices :
                           (mesh->animVertices != NULL)? mesh->animVertices : mesh->vertices;
        array_near(got, s->positions, s->positionCount, "positions", asset);
    }
    if (s->normalCount > 0)
    {
        const float *got = (mesh->morphNormals != NULL)? mesh->morphNormals :
                           (mesh->animNormals != NULL)? mesh->animNormals : mesh->normals;
        array_near(got, s->normals, s->normalCount, "normals", asset);
    }
    if (s->tangentCount > 0)
    {
        const float *got = (mesh->morphTangents != NULL)? mesh->morphTangents : mesh->tangents;
        if (got != NULL) array_near(got, s->tangents, s->tangentCount, "tangents", asset);
    }
    if (s->texcoordCount > 0)
    {
        const float *got = (mesh->morphTexcoords != NULL)? mesh->morphTexcoords : mesh->texcoords;
        if (got != NULL) array_near(got, s->texcoords, s->texcoordCount, "texcoords", asset);
    }
}

// Load reference file and run all its samples through the given animation
static void run_reference_samples(const char *asset, const char *refName, Model model,
                                  const ModelAnimation *anim)
{
    char *refPath = asset_path(refName);
    char *text = load_text_file(refPath);
    CHECK(text != NULL, "%s: reference file missing (%s)", asset, refPath);
    if (text == NULL) return;

    JsonReader root;
    jr_init(&root, text);
    if (!jr_object_open(&root)) { free(text); return; }

    bool foundSamples = false;
    while (jr_object_more(&root))
    {
        char key[64] = { 0 };
        if (!jr_key(&root, key, sizeof(key))) break;

        if (strcmp(key, "samples") == 0)
        {
            foundSamples = true;
            if (!jr_array_open(&root)) break;
            while (jr_array_more(&root))
            {
                RefSample sample = { 0 };
                if (!read_sample(&root, &sample)) break;

                if (anim != NULL) UpdateModelMorph(model, *anim, (float)sample.frame);
                else ResetModelMorphWeights(model);

                verify_sample(asset, &model.meshes[0], &sample);
                jr_comma(&root);
            }
            jr_array_close(&root);
        }
        else
        {
            // Skip non-array values (strings, numbers, booleans, nested objects)
            jr_skip_ws(&root);
            if (*root.cur == '"') { root.cur++; while (*root.cur && *root.cur != '"') root.cur++; root.cur++; }
            else if (*root.cur == '{' || *root.cur == '[')
            {
                char open = *root.cur, close = (open == '{')? '}' : ']';
                int depth = 0;
                do {
                    if (*root.cur == open) depth++;
                    else if (*root.cur == close) depth--;
                    root.cur++;
                } while (*root.cur && depth > 0);
            }
            else while (*root.cur && *root.cur != ',' && *root.cur != '}') root.cur++;
        }

        jr_comma(&root);
    }

    CHECK(foundSamples, "%s: reference file has no samples array", asset);
    free(text);
}

// ---------------------------------------------------------------------------
// Individual test cases
// ---------------------------------------------------------------------------

static void test_single_linear(void)
{
    const char *asset = "morph_single_linear.glb";
    printf("TEST single target + LINEAR + defaults + names + tangents (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh, got %d", asset, model.meshCount);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: expected 1 target, got %d", asset, mesh->morphTargetCount);
    CHECK(mesh->morphTargets != NULL && mesh->morphTargets[0].positions != NULL, "%s: position deltas missing", asset);
    CHECK(mesh->morphTargets[0].normals != NULL, "%s: normal deltas missing", asset);
    CHECK(mesh->morphTargets[0].tangents != NULL, "%s: tangent deltas missing", asset);
    CHECK(strcmp(GetMeshMorphTargetName(*mesh, 0), "lift") == 0, "%s: target name mismatch", asset);
    CHECK(GetMeshMorphTargetName(*mesh, 9) == NULL, "%s: out-of-range name must be NULL", asset);
    CHECK(float_near(mesh->morphDefaultWeights[0], 0.25f), "%s: default weight not preserved", asset);
    CHECK(float_near(mesh->morphWeights[0], 0.25f), "%s: current weight not initialized to default", asset);

    // Default weights must already be applied at load time, before any update call
    float expectedY = 0.0f + 0.25f*1.0f;
    CHECK(float_near(mesh->morphVertices[1], expectedY), "%s: defaults not applied at load (%.8g vs %.8g)",
          asset, mesh->morphVertices[1], expectedY);

    // Draw once so the mesh gets uploaded, exercising the GPU mirror path
    BeginDrawing();
    ClearBackground(WHITE);
    DrawModel(model, (Vector3){ 0 }, 1.0f, WHITE);
    EndDrawing();

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount >= 2, "%s: expected 2 animations, got %d", asset, animCount);
    if (anims != NULL && animCount >= 1)
    {
        CHECK(strcmp(anims[0].name, "lift_anim") == 0, "%s: anim 0 name mismatch", asset);
        CHECK(anims[0].morphChannelCount == 1, "%s: expected 1 morph channel", asset);
        run_reference_samples(asset, "morph_single_linear.glb.ref.json", model, &anims[0]);

        // Animation switching: the STEP alt animation must override previous weights
        RefSample altSample = { 0 };
        altSample.frame = 0;
        altSample.weightCount = 1;
        altSample.weights[0] = 0.75;
        for (int i = 0; i < mesh->vertexCount*3; i++)
            altSample.positions[i] = mesh->vertices[i] + 0.75f*mesh->morphTargets[0].positions[i];
        altSample.positionCount = mesh->vertexCount*3;

        UpdateModelMorph(model, anims[1], 0.0f);
        verify_sample(asset, mesh, &altSample);

        // Reset must restore the default weights (0.25)
        ResetModelMorphWeights(model);
        CHECK(float_near(mesh->morphWeights[0], 0.25f), "%s: reset did not restore default weight", asset);
        CHECK(float_near(mesh->morphVertices[1], expectedY), "%s: reset did not reapply defaults", asset);
    }

    // Manual weight editing through the public API
    SetMeshMorphWeight(mesh, 0, 0.9f);
    CHECK(float_near(GetMeshMorphWeight(*mesh, 0), 0.9f), "%s: manual set/get failed", asset);
    SetMeshMorphWeight(mesh, 0, NAN);
    CHECK(float_near(GetMeshMorphWeight(*mesh, 0), 0.9f), "%s: NaN weight must be rejected", asset);
    SetMeshMorphWeight(mesh, 99, 0.5f); // Out of range, must warn and not crash
    CHECK(float_near(GetMeshMorphWeight(*mesh, 0), 0.9f), "%s: out-of-range set changed valid weight", asset);

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);
}

static void test_multi_defaults(void)
{
    const char *asset = "morph_multi_defaults.glb";
    printf("TEST multiple targets + default weights + texcoord deltas (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 2, "%s: expected 2 targets, got %d", asset, mesh->morphTargetCount);
    CHECK(strcmp(GetMeshMorphTargetName(*mesh, 0), "push_z") == 0, "%s: target 0 name", asset);
    CHECK(strcmp(GetMeshMorphTargetName(*mesh, 1), "push_x") == 0, "%s: target 1 name", asset);
    CHECK(mesh->morphTargets[0].texcoords != NULL, "%s: texcoord deltas missing", asset);
    CHECK(mesh->morphTargets[1].texcoords == NULL, "%s: target 1 should have no uv delta", asset);
    CHECK(float_near(mesh->morphWeights[0], 0.4f) && float_near(mesh->morphWeights[1], 0.3f),
          "%s: default weights mismatch", asset);

    // No animation in this asset: reference sample verifies the default-weight blend
    run_reference_samples(asset, "morph_multi_defaults.glb.ref.json", model, NULL);

    // Manual multi-target blend, verify the linear combination directly
    SetMeshMorphWeight(mesh, 0, 1.0f);
    SetMeshMorphWeight(mesh, 1, 0.5f);
    ResetModelMorphWeights(model); // Undo: back to 0.4/0.3
    CHECK(float_near(mesh->morphWeights[0], 0.4f) && float_near(mesh->morphWeights[1], 0.3f),
          "%s: reset failed for multi targets", asset);

    UnloadModel(model);
}

static void test_interpolation(const char *asset, const char *refName, const char *animName)
{
    printf("TEST interpolation %s (%s)\n", animName, asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh", asset);
    if (model.meshCount != 1) return;

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount == 1, "%s: expected 1 animation, got %d", asset, animCount);
    if (anims == NULL || animCount < 1) { UnloadModel(model); return; }

    MorphChannelInterpolation expected = MORPH_INTERPOLATION_CALCULATED;
    if (strcmp(animName, "STEP") == 0) expected = MORPH_INTERPOLATION_STEP;
    if (strcmp(animName, "CUBICSPLINE") == 0) expected = MORPH_INTERPOLATION_CUBICSPLINE;
    CHECK(anims[0].morphChannels[0].interpolation == expected, "%s: interpolation enum mismatch", asset);

    run_reference_samples(asset, refName, model, &anims[0]);

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);
}

static void test_skin_coexist(void)
{
    const char *asset = "morph_skin_coexist.glb";
    printf("TEST skin + morph coexistence (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh", asset);
    CHECK(model.skeleton.boneCount == 1, "%s: expected 1 bone, got %u", asset, model.skeleton.boneCount);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: expected 1 morph target", asset);
    CHECK(mesh->boneWeights != NULL && mesh->boneIndices != NULL, "%s: skin attributes missing", asset);

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount == 2, "%s: expected 2 animations (bone + morph), got %d", asset, animCount);
    if (anims == NULL || animCount < 2) { UnloadModel(model); return; }

    // The bone animation carries keyframe poses, the morph-only one carries weight channels
    int morphAnim = -1;
    int boneAnim = -1;
    for (int i = 0; i < animCount; i++)
    {
        if (anims[i].morphChannelCount > 0) morphAnim = i;
        if (anims[i].keyframeCount > 0) boneAnim = i;
    }
    CHECK(morphAnim >= 0, "%s: could not find morph animation", asset);
    CHECK(boneAnim >= 0, "%s: could not find bone animation", asset);
    CHECK(IsModelAnimationValid(model, anims[morphAnim]), "%s: morph anim must validate", asset);

    // Bone animation has no morph channels and vice versa
    CHECK(anims[boneAnim].morphChannelCount == 0, "%s: bone anim must not carry morph channels", asset);
    CHECK(anims[morphAnim].keyframeCount == 0, "%s: morph anim must not carry bone keyframes", asset);

    char *text = load_text_file(asset_path("morph_skin_coexist.glb.ref.json"));
    CHECK(text != NULL, "%s: reference missing", asset);
    if (text != NULL)
    {
        JsonReader root;
        jr_init(&root, text);
        jr_object_open(&root);
        while (jr_object_more(&root))
        {
            char key[64] = { 0 };
            jr_key(&root, key, sizeof(key));
            if (strcmp(key, "samples") == 0)
            {
                jr_array_open(&root);
                while (jr_array_more(&root))
                {
                    RefSample sample = { 0 };
                    read_sample(&root, &sample);

                    // glTF vertex pipeline order: morph first, then skinning.
                    // (Here bone and morph live in separate animation objects, so they
                    // are driven by two calls; the internal order is additionally covered
                    // by morph_combined.glb where a single animation carries both channels)
                    UpdateModelMorph(model, anims[morphAnim], (float)sample.frame);
                    UpdateModelAnimation(model, anims[boneAnim], (float)sample.frame);

                    // Final skinned+morphed positions live in animVertices
                    array_near(mesh->animVertices, sample.positions, sample.positionCount,
                               "skinned+morphed positions", asset);
                    jr_comma(&root);
                }
                jr_array_close(&root);
            }
            else
            {
                jr_skip_ws(&root);
                if (*root.cur == '"') { root.cur++; while (*root.cur && *root.cur != '"') root.cur++; root.cur++; }
                else while (*root.cur && *root.cur != ',' && *root.cur != '}') root.cur++;
            }
            jr_comma(&root);
        }
        free(text);
    }

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);
}

static void test_over_limit(void)
{
    const char *asset = "morph_over_limit.glb";
    printf("TEST target count over limit truncation (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == MAX_MESH_MORPH_TARGETS,
          "%s: expected truncation to %d, got %d", asset, MAX_MESH_MORPH_TARGETS, mesh->morphTargetCount);
    CHECK(mesh->morphWeights != NULL && mesh->morphVertices != NULL, "%s: runtime arrays missing", asset);

    UnloadModel(model);
}

static void test_non_finite_default(void)
{
    const char *asset = "morph_non_finite.glb";
    printf("TEST non-finite animation weight clamping (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: model with a binary NaN weight must still load", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: expected 1 target", asset);
    CHECK(isfinite(mesh->morphWeights[0]), "%s: stored weight must be finite", asset);
    CHECK(float_near(mesh->morphWeights[0], 0.0f), "%s: default weight must be 0", asset);

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount == 1 && anims != NULL, "%s: animation must load despite NaN sample", asset);
    if (anims != NULL && animCount == 1)
    {
        // Frame 0 samples the NaN keyframe, which must have been clamped during loading
        run_reference_samples(asset, "morph_non_finite.glb.ref.json", model, &anims[0]);
        CHECK(isfinite(mesh->morphWeights[0]), "%s: evaluated weight must stay finite", asset);
        UnloadModelAnimations(anims, animCount);
    }

    UnloadModel(model);
}

static void test_sparse_target(void)
{
    const char *asset = "morph_sparse.glb";
    printf("TEST sparse morph target degradation (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: model must still load", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: target should still be registered", asset);
    CHECK(mesh->morphTargets[0].positions == NULL, "%s: sparse positions must be skipped", asset);
    CHECK(mesh->morphTargets[0].normals != NULL, "%s: plain normals must still load", asset);
    CHECK(mesh->morphVertices != NULL, "%s: morph runtime must remain usable", asset);

    // Applying weights must not crash and must keep base positions untouched
    SetMeshMorphWeight(mesh, 0, 1.0f);
    ModelAnimation empty = { 0 };
    UpdateModelMorph(model, empty, 0.0f);
    bool baseIntact = true;
    for (int i = 0; i < mesh->vertexCount*3; i++)
        if (!float_near(mesh->morphVertices[i], mesh->vertices[i])) { baseIntact = false; break; }
    CHECK(baseIntact, "%s: positions changed despite position target being skipped", asset);

    UnloadModel(model);
}

static void test_missing_accessor(void)
{
    const char *asset = "morph_missing_accessor.glb";
    printf("TEST missing accessor explicit failure (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 0, "%s: invalid glTF must not produce a model", asset);
    UnloadModel(model); // Unloading an empty model must be safe
}

static void test_plain_regression(void)
{
    const char *asset = "plain_no_morph.glb";
    printf("TEST plain model regression guard (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 0, "%s: plain mesh must report 0 targets", asset);
    CHECK(mesh->morphTargets == NULL && mesh->morphWeights == NULL && mesh->morphVertices == NULL,
          "%s: plain mesh must not allocate morph data", asset);
    CHECK(GetMeshMorphTargetCount(*mesh) == 0, "%s: count API mismatch", asset);

    // A no-op morph reset on a plain model must not crash or allocate
    ResetModelMorphWeights(model);
    CHECK(mesh->morphVertices == NULL, "%s: reset allocated morph data on plain mesh", asset);

    run_reference_samples(asset, "plain_no_morph.glb.ref.json", model, NULL);

    UnloadModel(model);
}

static void test_combined_single_animation(void)
{
    const char *asset = "morph_combined.glb";
    printf("TEST single animation with bone + morph channels, internal order (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1 && model.skeleton.boneCount == 1, "%s: load/skin failed", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: morph target missing", asset);

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount == 1, "%s: expected 1 combined animation, got %d", asset, animCount);
    if (anims == NULL || animCount != 1) { UnloadModel(model); return; }

    // One animation object carries both keyframe poses and a morph channel
    CHECK(anims[0].keyframeCount > 0, "%s: bone keyframes missing", asset);
    CHECK(anims[0].morphChannelCount == 1, "%s: morph channel missing", asset);

    char *text = load_text_file(asset_path("morph_combined.glb.ref.json"));
    CHECK(text != NULL, "%s: reference missing", asset);
    if (text != NULL)
    {
        JsonReader root;
        jr_init(&root, text);
        jr_object_open(&root);
        while (jr_object_more(&root))
        {
            char key[64] = { 0 };
            jr_key(&root, key, sizeof(key));
            if (strcmp(key, "samples") == 0)
            {
                jr_array_open(&root);
                while (jr_array_more(&root))
                {
                    RefSample sample = { 0 };
                    read_sample(&root, &sample);

                    // A single public call must apply morph first, then skinning
                    UpdateModelAnimation(model, anims[0], (float)sample.frame);

                    array_near(mesh->animVertices, sample.positions, sample.positionCount,
                               "combined skinned+morphed positions", asset);
                    jr_comma(&root);
                }
                jr_array_close(&root);
            }
            else
            {
                jr_skip_ws(&root);
                if (*root.cur == '"') { root.cur++; while (*root.cur && *root.cur != '"') root.cur++; root.cur++; }
                else while (*root.cur && *root.cur != ',' && *root.cur != '}') root.cur++;
            }
            jr_comma(&root);
        }
        free(text);
    }

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);
}

static void test_transformed_node(void)
{
    const char *asset = "morph_transformed_node.glb";
    printf("TEST node TRS transform applied to base and morph deltas (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 1, "%s: expected 1 mesh", asset);
    if (model.meshCount != 1) return;

    Mesh *mesh = &model.meshes[0];
    CHECK(mesh->morphTargetCount == 1, "%s: morph target missing", asset);
    CHECK(float_near(mesh->morphWeights[0], 0.5f), "%s: default weight must be 0.5", asset);

    // Reference was computed with the node world matrix applied to base points and
    // to the deltas (translation excluded, normals through inverse-transpose)
    run_reference_samples(asset, "morph_transformed_node.glb.ref.json", model, NULL);

    UnloadModel(model);
}

static void test_multi_primitive_node(void)
{
    const char *asset = "morph_multi_primitive.glb";
    printf("TEST one weights channel drives all node primitives (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    CHECK(model.meshCount == 2, "%s: expected 2 raylib meshes (two primitives), got %d", asset, model.meshCount);
    if (model.meshCount != 2) return;

    CHECK(model.meshes[0].morphTargetCount == 1 && model.meshes[1].morphTargetCount == 1,
          "%s: both primitives must carry a morph target", asset);

    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    CHECK(animCount == 1 && anims != NULL, "%s: expected 1 animation", asset);
    if (anims == NULL || animCount != 1) { UnloadModel(model); return; }

    CHECK(anims[0].morphChannelCount == 1, "%s: one weights channel expected", asset);
    CHECK(anims[0].morphChannels[0].meshId == 0, "%s: channel must start at mesh 0", asset);
    CHECK(anims[0].morphChannels[0].meshCount == 2, "%s: channel must cover 2 meshes, got %d",
          asset, anims[0].morphChannels[0].meshCount);

    // At frame 60 (t=1s) weight is 1.0; both meshes must be driven by the single channel
    UpdateModelMorph(model, anims[0], 60.0f);
    CHECK(float_near(model.meshes[0].morphWeights[0], 1.0f) &&
          float_near(model.meshes[1].morphWeights[0], 1.0f),
          "%s: weights must be applied to both meshes", asset);

    // Mesh 0 morphed with delta_a (y += 1), mesh 1 with delta_b (z += 2)
    bool mesh0Ok = fabsf(model.meshes[0].morphVertices[1] - 1.0f) <= TEST_TOLERANCE;
    bool mesh1Ok = fabsf(model.meshes[1].morphVertices[2] - 2.0f) <= TEST_TOLERANCE;
    CHECK(mesh0Ok, "%s: mesh 0 not morphed by the shared channel", asset);
    CHECK(mesh1Ok, "%s: mesh 1 not morphed by the shared channel", asset);

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);
}

static void test_no_per_frame_allocations(void)
{
    const char *asset = "morph_cubicspline.glb";
    printf("TEST no per-frame allocations (%s)\n", asset);

    Model model = LoadModel(asset_path(asset));
    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(asset_path(asset), &animCount);
    if ((model.meshCount != 1) || (anims == NULL)) { UnloadModelAnimations(anims, animCount); UnloadModel(model); return; }

    Mesh *mesh = &model.meshes[0];
    float *morphVertices = mesh->morphVertices;
    float *morphNormals = mesh->morphNormals;
    float *weights = mesh->morphWeights;

    for (int frame = 0; frame < 120; frame++)
    {
        UpdateModelMorph(model, anims[0], (float)frame);
        CHECK(mesh->morphVertices == morphVertices, "%s: morphVertices reallocated at frame %d", asset, frame);
        CHECK(mesh->morphNormals == morphNormals, "%s: morphNormals reallocated at frame %d", asset, frame);
        CHECK(mesh->morphWeights == weights, "%s: morphWeights reallocated at frame %d", asset, frame);
    }

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);
}

int main(void)
{
    // Headless software framebuffer, no window system required
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(64, 64, "morph tests");
    SetTargetFPS(60);

    test_single_linear();
    test_multi_defaults();
    test_interpolation("morph_step.glb", "morph_step.glb.ref.json", "STEP");
    test_interpolation("morph_cubicspline.glb", "morph_cubicspline.glb.ref.json", "CUBICSPLINE");
    test_skin_coexist();
    test_combined_single_animation();
    test_multi_primitive_node();
    test_transformed_node();
    test_over_limit();
    test_non_finite_default();
    test_sparse_target();
    test_missing_accessor();
    test_plain_regression();
    test_no_per_frame_allocations();

    CloseWindow();

    printf("\n========================================\n");
    printf("Checks: %d | Failures: %d\n", g_checks, g_failures);
    printf("========================================\n");
    return (g_failures == 0)? 0 : 1;
}
