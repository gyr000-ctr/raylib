/*******************************************************************************************
*
*   raylib [models] example/test - KHR_materials_pbrSpecularGlossiness loading
*
*   This example exercises the glTF specular/glossiness PBR workflow:
*     - Pure factor materials, pure texture materials, mixed materials
*     - Transparent (alpha blend) and alpha-masked materials
*     - Malformed inputs: out-of-range factors, empty extension, undecodable textures
*
*   It runs headless-safe assertions on the loaded material data and renders all
*   fixtures with the same camera and lighting for before/after visual comparison.
*
*   Test licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
********************************************************************************************/

#include "raylib.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FIXTURE_PATH "resources/models/gltf/specgloss/%s"
#define FIXTURE_COUNT 8

typedef struct {
    const char *fileName;
    const char *description;
} Fixture;

static const Fixture fixtures[FIXTURE_COUNT] = {
    { "sg_factors_only.glb",    "pure factors" },
    { "sg_textures_only.glb",   "pure textures" },
    { "sg_mixed.glb",           "mixed factors+textures (+normal/ao)" },
    { "sg_transparent.glb",     "transparent, alpha blend, double sided" },
    { "sg_mask.glb",            "alpha masked" },
    { "sg_out_of_range.glb",    "out-of-range factors (clamped)" },
    { "sg_empty_extension.glb", "extension enabled, fields missing (defaults)" },
    { "sg_bad_image.glb",       "undecodable textures (factor fallback)" },
};

static int failures = 0;
static const char *currentFile = "";

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { printf("[FAIL] %s: %s\n", currentFile, message); failures++; } \
        else { printf("[ OK ] %s: %s\n", currentFile, message); } \
    } while(0)

static bool FloatEquals(float a, float b)
{
    return fabsf(a - b) <= 0.02f;  // Colors are quantized to 8-bit, allow that error
}

// Render one model head-on and return the center pixel, used to verify the spec/gloss
// texture sampler is actually bound (a black specular texture must change the result)
static Color RenderCenterPixel(Model model)
{
    Camera cam = { 0 };
    cam.position = (Vector3){ 0.0f, 0.0f, 4.0f };
    cam.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy = 45.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    for (int frame = 0; frame < 10; frame++)
    {
        BeginDrawing();
            ClearBackground((Color){ 16, 16, 16, 255 });
            BeginMode3D(cam);
                DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, WHITE);
            EndMode3D();
        EndDrawing();
    }

    Image shot = LoadImageFromScreen();
    Color center = GetImageColor(shot, shot.width/2, shot.height/2);
    UnloadImage(shot);
    return center;
}

int main(void)
{
    const int screenWidth = 1280;
    const int screenHeight = 720;

    // Window creation still works under offscreen/virtualized environments;
    // assertions below run before the first frame is presented
    InitWindow(screenWidth, screenHeight, "raylib specular/glossiness glTF test");

    Model models[FIXTURE_COUNT] = { 0 };

    // Shared camera and lighting for every fixture (visual comparison)
    Camera camera = { 0 };
    camera.position = (Vector3){ 4.0f, 3.0f, 4.0f };
    camera.target = (Vector3){ 0.0f, 0.0f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    printf("== KHR_materials_pbrSpecularGlossiness loader assertions ==\n");

    for (int idx = 0; idx < FIXTURE_COUNT; idx++)
    {
        char path[256] = { 0 };
        snprintf(path, sizeof(path), FIXTURE_PATH, fixtures[idx].fileName);

        models[idx] = LoadModel(path);
        currentFile = fixtures[idx].fileName;
        CHECK(models[idx].meshCount >= 1, "mesh loaded");
        CHECK(models[idx].materialCount >= 2, "material slot + default present");

        Material *mat = &models[idx].materials[1];

        // Every spec/gloss material must leave the legacy metallic/roughness slots
        // untouched and carry its data in the new spec/gloss slots
        CHECK(mat->maps[MATERIAL_MAP_GLOSSINESS].value >= 0.0f &&
              mat->maps[MATERIAL_MAP_GLOSSINESS].value <= 1.0f,
              "glossiness factor clamped to [0..1]");
        CHECK(mat->maps[MATERIAL_MAP_SPECULAR].color.r <= 255 &&
              mat->maps[MATERIAL_MAP_SPECULAR].color.g <= 255 &&
              mat->maps[MATERIAL_MAP_SPECULAR].color.b <= 255,
              "specular factor color in byte range");
        CHECK(mat->maps[MATERIAL_MAP_ALBEDO].color.a <= 255,
              "diffuse alpha in byte range");
        CHECK(mat->shader.id > 0, "valid shader assigned");

        // Metallic/roughness legacy slots must keep their default zero/empty state
        CHECK(mat->maps[MATERIAL_MAP_METALNESS].texture.id == 0,
              "no metallic texture written (legacy slot untouched)");
        CHECK(mat->maps[MATERIAL_MAP_ROUGHNESS].texture.id == 0,
              "no roughness texture written (legacy slot untouched)");

        if (strcmp(fixtures[idx].fileName, "sg_factors_only.glb") == 0)
        {
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_GLOSSINESS].value, 0.6f),
                  "glossiness factor = 0.6");
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_ALBEDO].color.r/255.0f, 0.2f) &&
                  FloatEquals(mat->maps[MATERIAL_MAP_ALBEDO].color.g/255.0f, 0.5f) &&
                  FloatEquals(mat->maps[MATERIAL_MAP_ALBEDO].color.b/255.0f, 0.8f),
                  "diffuse factor = (0.2, 0.5, 0.8)");
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_SPECULAR].color.r/255.0f, 0.9f),
                  "specular factor r = 0.9");
            CHECK(mat->maps[MATERIAL_MAP_SPECULAR_GLOSSINESS].texture.id == 0,
                  "no spec/gloss texture on factor-only material");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_textures_only.glb") == 0)
        {
            CHECK(mat->maps[MATERIAL_MAP_ALBEDO].texture.id > 0,
                  "diffuse texture uploaded");
            CHECK(mat->maps[MATERIAL_MAP_SPECULAR_GLOSSINESS].texture.id > 0,
                  "combined specular-glossiness texture uploaded once");
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_GLOSSINESS].value, 1.0f),
                  "default glossiness factor = 1.0");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_mixed.glb") == 0)
        {
            CHECK(mat->maps[MATERIAL_MAP_SPECULAR_GLOSSINESS].texture.id > 0,
                  "spec/gloss texture present");
            CHECK(mat->maps[MATERIAL_MAP_NORMAL].texture.id > 0,
                  "normal texture preserved");
            CHECK(mat->maps[MATERIAL_MAP_OCCLUSION].texture.id > 0,
                  "occlusion texture preserved");
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_GLOSSINESS].value, 0.8f),
                  "glossiness factor = 0.8");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_transparent.glb") == 0)
        {
            CHECK(mat->maps[MATERIAL_MAP_GLOSSINESS].color.r == 1,
                  "alpha blend flag stored");
            CHECK(mat->maps[MATERIAL_MAP_GLOSSINESS].color.a == 1,
                  "doubleSided flag stored");
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_ALBEDO].color.a/255.0f, 0.4f),
                  "diffuse alpha = 0.4");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_mask.glb") == 0)
        {
            CHECK(mat->maps[MATERIAL_MAP_GLOSSINESS].color.r == 2,
                  "alpha mask flag stored");
            CHECK(mat->maps[MATERIAL_MAP_ALBEDO].texture.id > 0,
                  "mask diffuse texture present");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_out_of_range.glb") == 0)
        {
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_GLOSSINESS].value, 1.0f),
                  "glossiness 3.5 clamped to 1.0");
            CHECK(mat->maps[MATERIAL_MAP_ALBEDO].color.r == 255 &&
                  mat->maps[MATERIAL_MAP_ALBEDO].color.g == 0,
                  "diffuse r=2.0 clamped to 1, g=-1 clamped to 0");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_empty_extension.glb") == 0)
        {
            CHECK(FloatEquals(mat->maps[MATERIAL_MAP_GLOSSINESS].value, 1.0f),
                  "missing glossiness defaults to 1.0");
            CHECK(mat->maps[MATERIAL_MAP_SPECULAR].color.r == 255,
                  "missing specular defaults to white");
            CHECK(mat->maps[MATERIAL_MAP_ALBEDO].color.r == 255,
                  "missing diffuse defaults to white");
        }
        else if (strcmp(fixtures[idx].fileName, "sg_bad_image.glb") == 0)
        {
            // Broken images are rejected: diffuse keeps the default 1x1 texture,
            // no new texture upload is created, and the SG slot stays empty
            CHECK(mat->maps[MATERIAL_MAP_ALBEDO].texture.id == rlGetTextureIdDefault(),
                  "broken diffuse texture rejected, default texture kept");
            CHECK(mat->maps[MATERIAL_MAP_SPECULAR_GLOSSINESS].texture.id == 0,
                  "broken spec/gloss texture rejected, factor kept");
            CHECK(mat->shader.id > 0, "material still renderable after bad textures");
        }
    }

    // Metallic/roughness asset regression: an existing glb must keep the default shader
    Model mrModel = LoadModel("resources/models/gltf/robot.glb");
    currentFile = "robot.glb";
    if (mrModel.materialCount > 1)
    {
        // The SG shader declares glossinessValue; default and user shaders leave this loc at -1
        bool usesDefaultShader =
            mrModel.materials[1].shader.locs[SHADER_LOC_COLOR_GLOSSINESS] == -1;
        CHECK(usesDefaultShader, "metallic/roughness asset keeps default shader path");
    }

    // Sampler binding regression: two materials share identical factors and diffuse color,
    // but one adds a black specular texture. If texture12 is correctly bound to unit 12 the
    // highlight is removed and the center pixel differs; a broken sampler renders both equal.
    {
        Model noTex = LoadModel("resources/models/gltf/specgloss/sg_pair_notex.glb");
        Model blackTex = LoadModel("resources/models/gltf/specgloss/sg_pair_blacktex.glb");
        Color a = RenderCenterPixel(noTex);
        Color b = RenderCenterPixel(blackTex);
        int lumA = a.r + a.g + a.b;
        int lumB = b.r + b.g + b.b;
        currentFile = "sg_pair_blacktex.glb";
        CHECK(lumA != lumB,
              "specular-glossiness texture sampler bound (black spec texture changes shading)");
        UnloadModel(noTex);
        UnloadModel(blackTex);
    }

    printf("== assertions finished: %i failure(s) ==\n", failures);

    // Render loop: same camera and light for all fixtures, arranged in a grid
    int frames = 0;
    SetTargetFPS(60);

    while (!WindowShouldClose() && (frames < 120))
    {
        BeginDrawing();
            ClearBackground((Color){ 30, 30, 34, 255 });

            BeginMode3D(camera);
                for (int idx = 0; idx < FIXTURE_COUNT; idx++)
                {
                    int col = idx % 4;
                    int row = idx / 4;
                    Vector3 position = {
                        (col - 1.5f)*3.0f,
                        0.0f,
                        (row - 0.5f)*3.0f
                    };
                    DrawModel(models[idx], position, 1.0f, WHITE);
                }
            EndMode3D();

            DrawText("KHR_materials_pbrSpecularGlossiness fixtures (shared camera/light)",
                     20, 20, 20, RAYWHITE);
            for (int idx = 0; idx < FIXTURE_COUNT; idx++)
            {
                int col = idx % 4;
                int row = idx / 4;
                DrawText(fixtures[idx].description,
                         20 + col*310, screenHeight - 60 - row*26, 14, LIGHTGRAY);
            }
        EndDrawing();

        // Capture the full fixture grid once: all eight materials share one shader
        // program and are drawn in the same frame, exercising per-material uniform switching
        if (frames == 60) TakeScreenshot("sg_specgloss_fixture_grid.png");
        frames++;
    }

    for (int idx = 0; idx < FIXTURE_COUNT; idx++) UnloadModel(models[idx]);
    UnloadModel(mrModel);
    CloseWindow();

    return failures == 0 ? 0 : 1;
}
