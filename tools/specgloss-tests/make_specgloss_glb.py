#!/usr/bin/env python3
"""Generate minimal glTF 2.0 binary (.glb) fixtures for the
KHR_materials_pbrSpecularGlossiness loader regression tests.

Standard library only. Run with: python3 -I make_specgloss_glb.py <out_dir>
"""
import json
import struct
import sys
import zlib


def make_png(width, height, pixel_func):
    """Build an 8-bit RGBA PNG from a pixel function (x, y) -> (r, g, b, a)."""
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 (none)
        for x in range(width):
            raw.extend(pixel_func(x, y))

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


# A unit cube: 24 vertices (position + normal + uv), 36 indices
CUBE_POSITIONS = [
    (-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1),
    (-1, -1, -1), (-1, 1, -1), (1, 1, -1), (1, -1, -1),
    (-1, 1, -1), (-1, 1, 1), (1, 1, 1), (1, 1, -1),
    (-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1),
    (1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1),
    (-1, -1, -1), (-1, -1, 1), (-1, 1, 1), (-1, 1, -1),
]
CUBE_NORMALS = [
    (0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1),
    (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, 0, -1),
    (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0),
    (0, -1, 0), (0, -1, 0), (0, -1, 0), (0, -1, 0),
    (1, 0, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0),
    (-1, 0, 0), (-1, 0, 0), (-1, 0, 0), (-1, 0, 0),
]
CUBE_UVS = [(0, 0), (1, 0), (1, 1), (0, 1)] * 6
CUBE_INDICES = [
    0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7, 8, 9, 10, 8, 10, 11,
    12, 13, 14, 12, 14, 15, 16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23,
]


def pad4(data, pad_byte=b"\x00"):
    return data + pad_byte * ((4 - len(data) % 4) % 4)


def build_glb(material, images):
    """images: list of (name, png_bytes). Returns complete .glb bytes."""
    bin_blob = bytearray()
    buffer_views = []
    accessors = []

    def add_view(data, target=None):
        offset = len(bin_blob)
        bin_blob.extend(data)
        # binary glTF buffer views must be 4-byte aligned
        padding = (4 - len(bin_blob) % 4) % 4
        bin_blob.extend(b"\x00" * padding)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        buffer_views.append(view)
        return len(buffer_views) - 1

    def add_accessor(view, comp_type, count, ncomp, mins, maxs):
        accessors.append({
            "bufferView": view, "componentType": comp_type,
            "count": count, "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3"}[ncomp],
            "min": mins, "max": maxs,
        })
        return len(accessors) - 1

    # Indices (unsigned short)
    index_bytes = struct.pack("<%dH" % len(CUBE_INDICES), *CUBE_INDICES)
    iv = add_view(index_bytes, target=34963)
    ia = add_accessor(iv, 5123, len(CUBE_INDICES), 1, [0], [23])

    # Positions
    pos_bytes = struct.pack("<%df" % (len(CUBE_POSITIONS) * 3),
                            *[v for p in CUBE_POSITIONS for v in p])
    pv = add_view(pos_bytes, target=34962)
    pa = add_accessor(pv, 5126, 24, 3, [-1, -1, -1], [1, 1, 1])

    # Normals
    nrm_bytes = struct.pack("<%df" % (len(CUBE_NORMALS) * 3),
                            *[v for n in CUBE_NORMALS for v in n])
    nv = add_view(nrm_bytes, target=34962)
    na = add_accessor(nv, 5126, 24, 3, [-1, -1, -1], [1, 1, 1])

    # Texcoords
    uv_bytes = struct.pack("<%df" % (len(CUBE_UVS) * 2),
                           *[v for u in CUBE_UVS for v in u])
    uv_view = add_view(uv_bytes, target=34962)
    uv_accessor = add_accessor(uv_view, 5126, 24, 2, [0, 0], [1, 1])

    # Images appended after geometry; views reference the same single buffer
    image_entries = []
    for name, png in images:
        view_idx = add_view(png)
        image_entries.append({
            "name": name, "bufferView": view_idx, "mimeType": "image/png",
        })

    textures = [{"source": i} for i in range(len(images))]

    gltf = {
        "asset": {"version": "2.0", "generator": "raylib-specgloss-test"},
        "extensionsUsed": ["KHR_materials_pbrSpecularGlossiness"],
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{
            "name": "Cube",
            "primitives": [{
                "attributes": {
                    "POSITION": pa, "NORMAL": na, "TEXCOORD_0": uv_accessor,
                },
                "indices": ia, "material": 0,
            }],
        }],
        "materials": [material],
        "textures": textures,
        "images": image_entries,
        "accessors": accessors,
        "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(bin_blob)}],
    }

    json_bytes = pad4(json.dumps(gltf, separators=(",", ":")).encode("utf-8"),
                      pad_byte=b" ")
    total = 12 + 8 + len(json_bytes) + 8 + len(pad4(bytes(bin_blob)))
    header = struct.pack("<III", 0x46546c67, 2, total)
    json_chunk = struct.pack("<II", len(json_bytes), 0x4e4f534a) + json_bytes
    bin_padded = pad4(bytes(bin_blob))
    bin_chunk = struct.pack("<II", len(bin_padded), 0x004e4942) + bin_padded
    return header + json_chunk + bin_chunk


def sg_material(extra=None, **sg_fields):
    mat = {
        "name": "SG",
        "extensions": {
            "KHR_materials_pbrSpecularGlossiness": sg_fields,
        },
    }
    if extra:
        mat.update(extra)
    return mat


def main(out_dir):
    # Textures
    # Diffuse: red with an alpha gradient column (left half opaque, right half 50%)
    diffuse_png = make_png(8, 8, lambda x, y: (
        200, 40, 40, 255 if x < 4 else 128))
    # Specular-glossiness: rgb specular tint, alpha glossiness gradient
    sg_png = make_png(8, 8, lambda x, y: (
        240, 220, 160, int(255 * x / 7)))
    # Occlusion: grayscale, dark corner
    ao_png = make_png(8, 8, lambda x, y: (
        (60 if (x < 2 and y < 2) else 230),) * 3 + (255,))
    # Normal-ish: flat +Z
    normal_png = make_png(4, 4, lambda x, y: (128, 128, 255, 255))

    fixtures = {}

    # 1. Pure factors
    fixtures["sg_factors_only.glb"] = sg_material(
        diffuseFactor=[0.2, 0.5, 0.8, 1.0],
        specularFactor=[0.9, 0.7, 0.2],
        glossinessFactor=0.6,
    )

    # 2. Pure textures (factors left at defaults, texture carries everything)
    fixtures["sg_textures_only.glb"] = sg_material(
        diffuseTexture={"index": 0},
        specularGlossinessTexture={"index": 1},
    )

    # 3. Mixed: factors modulate textures, plus normal and occlusion
    fixtures["sg_mixed.glb"] = sg_material(
        diffuseFactor=[1.0, 0.95, 0.9, 1.0],
        specularFactor=[1.0, 1.0, 1.0],
        glossinessFactor=0.8,
        diffuseTexture={"index": 0},
        specularGlossinessTexture={"index": 1},
    )
    fixtures["sg_mixed.glb"]["normalTexture"] = {"index": 3}
    fixtures["sg_mixed.glb"]["occlusionTexture"] = {"index": 2}

    # 4. Transparent: alpha blend mode
    fixtures["sg_transparent.glb"] = sg_material(
        diffuseFactor=[0.2, 0.6, 0.3, 0.4],
        specularFactor=[0.5, 0.5, 0.5],
        glossinessFactor=0.3,
        extra={"alphaMode": "BLEND", "doubleSided": True},
    )

    # 5. Alpha mask
    fixtures["sg_mask.glb"] = sg_material(
        diffuseFactor=[0.8, 0.2, 0.2, 1.0],
        specularFactor=[0.3, 0.3, 0.3],
        glossinessFactor=0.5,
        diffuseTexture={"index": 0},
        extra={"alphaMode": "MASK", "alphaCutoff": 0.55},
    )

    # 6. Out-of-range factors (must be clamped, not crash)
    fixtures["sg_out_of_range.glb"] = sg_material(
        diffuseFactor=[2.0, -1.0, 0.5, 1.5],
        specularFactor=[1.2, 0.0, -0.4],
        glossinessFactor=3.5,
    )

    # 7. Extension enabled but all inner fields missing -> spec defaults (white, gloss 1)
    fixtures["sg_empty_extension.glb"] = sg_material()

    # 8. Broken texture reference: view points at truncated image bytes
    # Handled separately below by reusing a texture whose image fails decoding
    fixtures["sg_bad_image.glb"] = sg_material(
        diffuseFactor=[0.4, 0.4, 0.4, 1.0],
        specularFactor=[0.6, 0.6, 0.6],
        glossinessFactor=0.4,
        diffuseTexture={"index": 0},
        specularGlossinessTexture={"index": 1},
    )

    # 9/10. Same-diffuse pair proving the spec/gloss sampler is actually bound:
    # identical factors, one without the texture (full highlight) and one with a
    # black specular texture (highlight removed). Equal output means a broken sampler.
    black_sg_png = make_png(8, 8, lambda x, y: (0, 0, 0, 128))
    fixtures["sg_pair_notex.glb"] = sg_material(
        diffuseFactor=[0.8, 0.8, 0.8, 1.0],
        specularFactor=[1.0, 1.0, 1.0],
        glossinessFactor=0.5,
    )
    fixtures["sg_pair_blacktex.glb"] = sg_material(
        diffuseFactor=[0.8, 0.8, 0.8, 1.0],
        specularFactor=[1.0, 1.0, 1.0],
        glossinessFactor=0.5,
        specularGlossinessTexture={"index": 0},
    )

    image_sets = {
        "sg_factors_only.glb": [],
        "sg_textures_only.glb": [("diffuse.png", diffuse_png), ("specgloss.png", sg_png)],
        "sg_mixed.glb": [("diffuse.png", diffuse_png), ("specgloss.png", sg_png),
                         ("ao.png", ao_png), ("normal.png", normal_png)],
        "sg_transparent.glb": [],
        "sg_mask.glb": [("diffuse.png", diffuse_png)],
        "sg_out_of_range.glb": [],
        "sg_empty_extension.glb": [],
        "sg_bad_image.glb": [("broken.png", b"\x89PNG\r\n\x1a\nnot-a-real-png"),
                             ("broken2.png", b"garbage")],
        "sg_pair_notex.glb": [],
        "sg_pair_blacktex.glb": [("black_spec.png", black_sg_png)],
    }

    for name, material in fixtures.items():
        glb = build_glb(material, image_sets[name])
        with open(f"{out_dir}/{name}", "wb") as f:
            f.write(glb)
        print(f"wrote {name} ({len(glb)} bytes)")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else ".")
