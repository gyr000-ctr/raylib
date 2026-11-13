/*******************************************************************************************
*
*   models_morph_targets.c
*
*   glTF 2.0 morph target (blend shape) loading and animation.
*
*   This example shows the morph target support in the glTF loader:
*     - Default weights stored in the model are applied automatically on load
*     - LINEAR, STEP and CUBICSPLINE weight channels drive the same mesh
*     - Manual weight editing through SetMeshMorphWeight()
*     - Resetting back to the default weights
*     - Animation switching without leftover weights from the previous clip
*
*   The GLB asset is generated reproducibly by the script under
*   tests/morph/gen/gen_morph_assets.py and copied into this resources folder.
*
*   Example originally created with raylib 5.5
*
*   Example licensed under an unmodified zlib/libpng license, which is an OSI-certified,
*   BSD-like license that allows static linking with closed source software
*
*   Copyright (c) 2026 raylib contributors
*
********************************************************************************************/

#include "raylib.h"
#include "raymath.h"

#include <stdio.h>

// The three assets use the same geometry with one morph target each,
// differing only in the interpolation of their weight channel
#define ASSET_LINEAR      "resources/glTF/morph_single_linear.glb"
#define ASSET_STEP        "resources/glTF/morph_step.glb"
#define ASSET_CUBICSPLINE "resources/glTF/morph_cubicspline.glb"

typedef struct {
    const char *path;
    const char *label;
} MorphDemo;

int main(void)
{
    const int screenWidth = 960;
    const int screenHeight = 540;

    InitWindow(screenWidth, screenHeight, "raylib [models] example - morph targets (blend shapes)");

    MorphDemo demos[] = {
        { ASSET_LINEAR, "LINEAR interpolation" },
        { ASSET_STEP, "STEP interpolation" },
        { ASSET_CUBICSPLINE, "CUBICSPLINE interpolation" },
    };
    const int demoCount = (int)(sizeof(demos)/sizeof(demos[0]));
    int demoIndex = 0;

    Model model = LoadModel(demos[demoIndex].path);
    int animCount = 0;
    ModelAnimation *anims = LoadModelAnimations(demos[demoIndex].path, &animCount);

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 2.5f, 2.0f, 4.0f };
    camera.target = (Vector3){ 0.0f, 0.4f, 0.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    int targetCount = (model.meshCount > 0)? GetMeshMorphTargetCount(model.meshes[0]) : 0;

    // 0 = animation drives the weights, 1 = the slider drives them manually
    bool manualMode = false;
    float manualWeight = 0.5f;
    float animTimeSec = 0.0f;

    SetTargetFPS(60);

    while (!WindowShouldClose())
    {
        // Switch interpolation asset
        if (IsKeyPressed(KEY_TAB))
        {
            UnloadModelAnimations(anims, animCount);
            UnloadModel(model);

            demoIndex = (demoIndex + 1) % demoCount;
            model = LoadModel(demos[demoIndex].path);
            anims = LoadModelAnimations(demos[demoIndex].path, &animCount);
            targetCount = (model.meshCount > 0)? GetMeshMorphTargetCount(model.meshes[0]) : 0;
            animTimeSec = 0.0f;
        }

        if (IsKeyPressed(KEY_SPACE)) manualMode = !manualMode;
        if (IsKeyPressed(KEY_R))
        {
            // Restore the weights stored in the glTF file
            ResetModelMorphWeights(model);
            animTimeSec = 0.0f;
        }

        if (manualMode)
        {
            manualWeight += GetMouseWheelMove()*0.1f;
            manualWeight = Clamp(manualWeight, 0.0f, 1.0f);
            for (int m = 0; m < model.meshCount; m++)
            {
                for (int t = 0; t < GetMeshMorphTargetCount(model.meshes[m]); t++)
                    SetMeshMorphWeight(&model.meshes[m], t, manualWeight);
            }
            // Re-apply the edited weights to the vertex buffers
            if (animCount > 0) UpdateModelMorph(model, anims[0], 0.0f);
            else
            {
                ModelAnimation empty = { 0 };
                UpdateModelMorph(model, empty, 0.0f);
            }
        }
        else if ((animCount > 0) && (targetCount > 0))
        {
            animTimeSec += GetFrameTime();

            // Frame index at the glTF 60 fps sampling convention
            float frame = animTimeSec*60.0f;
            float totalFrames = (float)anims[0].keyframeCount;

            // Morph-only clips have no baked bone keyframes; loop by channel times instead
            if (totalFrames <= 0.0f)
            {
                if ((anims[0].morphChannelCount > 0) && (anims[0].morphChannels[0].keyframeCount > 0))
                    totalFrames = anims[0].morphChannels[0].times[anims[0].morphChannels[0].keyframeCount - 1]*60.0f;
            }

            if (totalFrames > 0.0f && frame >= totalFrames)
            {
                frame = fmodf(frame, totalFrames);
                animTimeSec = frame/60.0f;
            }

            // Animation switches reset every mesh to its default weights first,
            // so the new clip fully owns the targets it animates
            UpdateModelMorph(model, anims[0], frame);
        }

        BeginDrawing();
            ClearBackground(RAYWHITE);

            BeginMode3D(camera);
                DrawModel(model, (Vector3){ 0.0f, 0.0f, 0.0f }, 1.0f, GRAY);
                DrawGrid(10, 0.5f);
            EndMode3D();

            DrawText("Morph targets (blend shapes)", 20, 20, 20, DARKGRAY);
            DrawText(demos[demoIndex].label, 20, 46, 16, MAROON);

            DrawText("TAB: switch interpolation   SPACE: manual/animation   R: reset defaults",
                     20, screenHeight - 30, 14, DARKGRAY);

            if (targetCount > 0)
            {
                char info[128] = { 0 };
                snprintf(info, sizeof(info), "target[0]: %s  weight: %.3f",
                         GetMeshMorphTargetName(model.meshes[0], 0) ?
                             GetMeshMorphTargetName(model.meshes[0], 0) : "(unnamed)",
                         GetMeshMorphWeight(model.meshes[0], 0));
                DrawText(info, 20, 72, 15, DARKBLUE);
            }

            if (manualMode)
            {
                DrawText("MANUAL MODE - mouse wheel changes the weight", 20, 96, 14, DARKGREEN);
                DrawRectangle(20, 116, 200, 12, LIGHTGRAY);
                DrawRectangle(20, 116, (int)(manualWeight*200.0f), 12, SKYBLUE);
            }
        EndDrawing();
    }

    UnloadModelAnimations(anims, animCount);
    UnloadModel(model);

    CloseWindow();

    return 0;
}
