#!/usr/bin/env python3
"""Generate deterministic glTF 2.0 (.glb) test assets for raylib morph target support.

The assets are emitted from scratch (no third-party modules, standard library only)
so the tests are fully reproducible offline. Every generated model also gets a
*.ref.json file with the expected morphed vertex positions/normals at given frames,
computed directly from the glTF 2.0 specification equations:

    morphed(v) = base(v) + sum_i weight_i * delta_i(v)

with LINEAR, STEP and CUBICSPLINE interpolation evaluated exactly as the spec
defines for scalar weights. Run with isolated mode (python3 -I) as required by
the environment policy.
"""

import json
import math
import os
import struct

# ---------------------------------------------------------------------------
# Minimal glTF GLB writer
# ---------------------------------------------------------------------------

class GltfBuilder:
    """Build a single-buffer glTF and serialize it as a binary .glb."""

    def __init__(self):
        self.gltf = {
            "asset": {"version": "2.0", "generator": "raylib-morph-testgen"},
            "scenes": [{"nodes": []}],
            "scene": 0,
            "nodes": [],
            "meshes": [],
            "accessors": [],
            "bufferViews": [],
            "animations": [],
            "skins": [],
            "materials": [],
        }
        self.bin = bytearray()

    # -- low level buffer helpers -------------------------------------------

    def _pad_buffer(self):
        while len(self.bin) % 4 != 0:
            self.bin.append(0)

    def add_view(self, data, target=None):
        self._pad_buffer()
        offset = len(self.bin)
        self.bin.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.gltf["bufferViews"].append(view)
        return len(self.gltf["bufferViews"]) - 1

    def add_accessor(self, view, comp_type, count, kind, mins=None, maxs=None):
        acc = {"bufferView": view, "componentType": comp_type, "count": count, "type": kind}
        if mins is not None:
            acc["min"] = mins
        if maxs is not None:
            acc["max"] = maxs
        self.gltf["accessors"].append(acc)
        return len(self.gltf["accessors"]) - 1

    def add_float_accessor(self, flat_values, components, kind, target=None):
        data = struct.pack("<%df" % len(flat_values), *flat_values)
        view = self.add_view(data, target)
        mins = None
        maxs = None
        if components in (2, 3):
            mins = [float("inf")] * components
            maxs = [float("-inf")] * components
            for i in range(0, len(flat_values), components):
                for c in range(components):
                    mins[c] = min(mins[c], flat_values[i + c])
                    maxs[c] = max(maxs[c], flat_values[i + c])
        return self.add_accessor(view, 5126, len(flat_values) // components, kind, mins, maxs)

    def add_scalar_float_accessor(self, values, target=None):
        data = struct.pack("<%df" % len(values), *values)
        view = self.add_view(data, target)
        return self.add_accessor(view, 5126, len(values), "SCALAR")

    # -- mesh helpers --------------------------------------------------------

    def add_morph_mesh(self, name, base_pos, base_norm, targets, weights=None,
                       base_tangent=None, base_uv=None, target_uv=False):
        """targets: list of dicts with optional keys positions, normals, tangents, texcoords."""
        vertex_count = len(base_pos) // 3
        prim = {"attributes": {}, "targets": []}

        prim["attributes"]["POSITION"] = self.add_float_accessor(
            base_pos, 3, "VEC3", target=34962)
        prim["attributes"]["NORMAL"] = self.add_float_accessor(
            base_norm, 3, "VEC3", target=34962)

        if base_tangent is not None:
            prim["attributes"]["TANGENT"] = self.add_float_accessor(
                base_tangent, 4, "VEC4", target=34962)
        if base_uv is not None:
            prim["attributes"]["TEXCOORD_0"] = self.add_float_accessor(
                base_uv, 2, "VEC2", target=34962)

        target_names = []
        for idx, target in enumerate(targets):
            entry = {}
            if "positions" in target:
                entry["POSITION"] = self.add_float_accessor(
                    target["positions"], 3, "VEC3", target=34962)
            if "normals" in target:
                entry["NORMAL"] = self.add_float_accessor(
                    target["normals"], 3, "VEC3", target=34962)
            if "tangents" in target:
                entry["TANGENT"] = self.add_float_accessor(
                    target["tangents"], 3, "VEC3", target=34962)
            if target_uv and "texcoords" in target:
                entry["TEXCOORD_0"] = self.add_float_accessor(
                    target["texcoords"], 2, "VEC2", target=34962)
            prim["targets"].append(entry)
            target_names.append(target.get("name", "target_%d" % idx))

        mesh = {"name": name, "primitives": [prim]}
        if weights is not None:
            mesh["weights"] = list(weights)
        mesh["extras"] = {"targetNames": target_names}
        # glTF standard target names live at mesh.targetNames
        mesh["targetNames"] = target_names
        self.gltf["meshes"].append(mesh)
        return len(self.gltf["meshes"]) - 1

    def add_node(self, name, mesh_index, weights=None):
        node = {"name": name, "mesh": mesh_index}
        if weights is not None:
            node["weights"] = list(weights)
        self.gltf["nodes"].append(node)
        index = len(self.gltf["nodes"]) - 1
        self.gltf["scenes"][0]["nodes"].append(index)
        return index

    def add_weights_animation(self, name, node_index, times, key_weight_sets,
                              interpolation="LINEAR"):
        """key_weight_sets:
        LINEAR/STEP: list (per time) of weight lists.
        CUBICSPLINE: list (per time) of (in_tangent, value, out_tangent) weight lists.
        """
        input_acc = self.add_scalar_float_accessor(times)

        if interpolation == "CUBICSPLINE":
            flat = []
            for in_t, value, out_t in key_weight_sets:
                flat.extend(in_t)
                flat.extend(value)
                flat.extend(out_t)
        else:
            flat = []
            for weights in key_weight_sets:
                flat.extend(weights)

        output_acc = self.add_scalar_float_accessor(flat)
        sampler = {
            "input": input_acc,
            "output": output_acc,
            "interpolation": interpolation,
        }
        channel = {"sampler": len(self.gltf.get("_samplers", [])), "target": {}}
        # Samplers are stored inline inside the animation, build the animation entry directly
        animation = {
            "name": name,
            "samplers": [sampler],
            "channels": [{
                "sampler": 0,
                "target": {"node": node_index, "path": "weights"},
            }],
        }
        self.gltf["animations"].append(animation)
        return len(self.gltf["animations"]) - 1

    def write_glb(self, path):
        self._pad_buffer()
        self.gltf["buffers"] = [{"byteLength": len(self.bin)}]
        json_bytes = json.dumps(self.gltf, separators=(",", ":")).encode("utf-8")
        while len(json_bytes) % 4 != 0:
            json_bytes += b" "
        total = 12 + 8 + len(json_bytes) + 8 + len(self.bin)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, total))
            f.write(struct.pack("<II", len(json_bytes), 0x4E4F534A))
            f.write(json_bytes)
            f.write(struct.pack("<II", len(self.bin), 0x004E4942))
            f.write(self.bin)


# ---------------------------------------------------------------------------
# Reference evaluation (glTF spec math)
# ---------------------------------------------------------------------------

def lerp(a, b, t):
    return a + (b - a) * t


def ref_weights(times, values, target_count, interpolation, time):
    """Return interpolated weights at `time`, values layout matches the builder."""
    if time <= times[0]:
        idx = 0
        if interpolation == "CUBICSPLINE":
            return values[1 * target_count:2 * target_count]
        return values[0:target_count]
    if time >= times[-1]:
        last = len(times) - 1
        if interpolation == "CUBICSPLINE":
            base = (3 * last + 1) * target_count
            return values[base:base + target_count]
        return values[last * target_count:(last + 1) * target_count]

    idx = 0
    for i in range(len(times) - 1):
        if times[i] <= time < times[i + 1]:
            idx = i
            break

    dt = max(times[idx + 1] - times[idx], 1e-12)
    u = (time - times[idx]) / dt
    u = min(max(u, 0.0), 1.0)

    if interpolation == "STEP":
        return values[idx * target_count:(idx + 1) * target_count]

    if interpolation == "LINEAR":
        a = values[idx * target_count:(idx + 1) * target_count]
        b = values[(idx + 1) * target_count:(idx + 2) * target_count]
        return [lerp(a[t], b[t], u) for t in range(target_count)]

    # CUBICSPLINE, hermite basis with per-second tangents
    u2, u3 = u * u, u * u * u
    h00 = 2 * u3 - 3 * u2 + 1
    h10 = u3 - 2 * u2 + u
    h01 = -2 * u3 + 3 * u2
    h11 = u3 - u2
    in_next = values[(3 * (idx + 1)) * target_count:(3 * idx + 4) * target_count]
    v1 = values[(3 * idx + 1) * target_count:(3 * idx + 2) * target_count]
    out_cur = values[(3 * idx + 2) * target_count:(3 * idx + 3) * target_count]
    v2 = values[(3 * (idx + 1) + 1) * target_count:(3 * (idx + 1) + 2) * target_count]
    result = []
    for t in range(target_count):
        result.append(
            h00 * v1[t]
            + h10 * out_cur[t] * dt
            + h01 * v2[t]
            + h11 * in_next[t] * dt
        )
    return result


def ref_morphed(base, deltas, weights, components=3):
    out = list(base)
    for target_deltas, w in zip(deltas, weights):
        if target_deltas is None or w == 0.0:
            continue
        for i in range(len(out)):
            out[i] += w * target_deltas[i]
    return out


# ---------------------------------------------------------------------------
# Shared geometry: a small triangle strip style quad with 4 vertices
# ---------------------------------------------------------------------------

def quad_geometry():
    pos = [
        -1.0, 0.0, 0.0,
         1.0, 0.0, 0.0,
        -1.0, 1.0, 0.0,
         1.0, 1.0, 0.0,
    ]
    norm = [
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
    ]
    tangent = [
        1.0, 0.0, 0.0, 1.0,
        1.0, 0.0, 0.0, 1.0,
        1.0, 0.0, 0.0, 1.0,
        1.0, 0.0, 0.0, 1.0,
    ]
    uv = [0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0]
    return pos, norm, tangent, uv


def write_ref(path, payload):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)


OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets")
OUT_DIR = os.path.abspath(OUT_DIR)


def asset_path(name):
    return os.path.join(OUT_DIR, name)


# ---------------------------------------------------------------------------
# Asset 1: single morph target, LINEAR weights animation
# ---------------------------------------------------------------------------

def gen_single_linear():
    b = GltfBuilder()
    pos, norm, tangent, uv = quad_geometry()
    delta_pos = [0.0, 1.0, 0.0] * 4
    delta_norm = [0.0, 0.0, 0.0] * 4
    delta_norm = [1.0, 0.0, 0.0] * 4
    delta_tangent = [0.0, 0.0, 0.5] * 4
    # Vec4 per-vertex deltas for the reference: the w handedness sign stays unchanged
    delta_tangent4 = []
    for i in range(4):
        delta_tangent4.extend(delta_tangent[3*i:3*i + 3])
        delta_tangent4.append(0.0)
    targets = [{"name": "lift", "positions": delta_pos, "normals": delta_norm,
                "tangents": delta_tangent}]
    mesh_idx = b.add_morph_mesh("single", pos, norm, targets, weights=[0.25],
                                base_tangent=tangent, base_uv=uv)
    node_idx = b.add_node("single_node", mesh_idx)

    times = [0.0, 1.0, 2.0]
    values_sets = [[0.0], [1.0], [0.5]]
    b.add_weights_animation("lift_anim", node_idx, times, values_sets, "LINEAR")

    # Second animation: constant 0.75 STEP channel, used by the harness for animation switching
    alt_times = [0.0, 1.0]
    alt_sets = [[0.75], [0.75]]
    b.add_weights_animation("lift_anim_alt", node_idx, alt_times, alt_sets, "STEP")

    glb = asset_path("morph_single_linear.glb")
    b.write_glb(glb)

    flat_values = [w for ws in values_sets for w in ws]
    samples = []
    for frame in (0, 30, 60, 90, 120):
        t = frame / 60.0
        w = ref_weights(times, flat_values, 1, "LINEAR", t)[0]
        samples.append({
            "frame": frame,
            "time": t,
            "weights": [w],
            "positions": ref_morphed(pos, [delta_pos], [w]),
            "normals": ref_morphed(norm, [delta_norm], [w]),
            "tangents": ref_morphed(tangent, [delta_tangent4], [w], components=4),
        })
    write_ref(glb + ".ref.json", {
        "asset": "morph_single_linear.glb",
        "targetNames": ["lift"],
        "defaultWeights": [0.25],
        "interpolation": "LINEAR",
        "altAnimation": {"name": "lift_anim_alt", "frame": 0, "weights": [0.75],
                         "positions": ref_morphed(pos, [delta_pos], [0.75])},
        "samples": samples,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 2: multiple targets, mixed deltas, no animation (defaults drive shape)
# ---------------------------------------------------------------------------

def gen_multi_defaults():
    b = GltfBuilder()
    pos, norm, tangent, uv = quad_geometry()
    d1_pos = [0.0, 0.0, 1.0] * 4
    d2_pos = [1.0, 0.0, 0.0] * 4
    d1_norm = [0.0, 1.0, 0.0] * 4
    d1_uv = [0.1, 0.1] * 4
    targets = [
        {"name": "push_z", "positions": d1_pos, "normals": d1_norm, "texcoords": d1_uv},
        {"name": "push_x", "positions": d2_pos},
    ]
    defaults = [0.4, 0.3]
    mesh_idx = b.add_morph_mesh("multi", pos, norm, targets, weights=defaults,
                                base_tangent=tangent, base_uv=uv, target_uv=True)
    b.add_node("multi_node", mesh_idx)

    glb = asset_path("morph_multi_defaults.glb")
    b.write_glb(glb)

    exp_pos = ref_morphed(pos, [d1_pos, d2_pos], defaults)
    exp_norm = ref_morphed(norm, [d1_norm, None], defaults)
    exp_uv = ref_morphed(uv, [d1_uv, None], defaults, components=2)
    write_ref(glb + ".ref.json", {
        "asset": "morph_multi_defaults.glb",
        "targetNames": ["push_z", "push_x"],
        "defaultWeights": defaults,
        "samples": [{
            "frame": 0,
            "weights": defaults,
            "positions": exp_pos,
            "normals": exp_norm,
            "texcoords": exp_uv,
        }],
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 3: STEP interpolation
# ---------------------------------------------------------------------------

def gen_step():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    delta = [0.0, 2.0, 0.0] * 4
    targets = [{"name": "step_lift", "positions": delta}]
    mesh_idx = b.add_morph_mesh("step_mesh", pos, norm, targets)
    node_idx = b.add_node("step_node", mesh_idx)

    times = [0.0, 0.5, 1.0]
    sets = [[0.0], [1.0], [0.0]]
    b.add_weights_animation("step_anim", node_idx, times, sets, "STEP")
    glb = asset_path("morph_step.glb")
    b.write_glb(glb)

    flat = [w for ws in sets for w in ws]
    samples = []
    for frame in (0, 29, 30, 59, 60):
        t = frame / 60.0
        w = ref_weights(times, flat, 1, "STEP", t)[0]
        samples.append({"frame": frame, "time": t, "weights": [w],
                        "positions": ref_morphed(pos, [delta], [w])})
    write_ref(glb + ".ref.json", {
        "asset": "morph_step.glb",
        "targetNames": ["step_lift"],
        "defaultWeights": [0.0],
        "interpolation": "STEP",
        "samples": samples,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 4: CUBICSPLINE interpolation with two targets
# ---------------------------------------------------------------------------

def gen_cubic():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    d1 = [0.0, 1.0, 0.0] * 4
    d2 = [0.0, 0.0, 1.0] * 4
    targets = [{"name": "cubic_y", "positions": d1}, {"name": "cubic_z", "positions": d2}]
    mesh_idx = b.add_morph_mesh("cubic_mesh", pos, norm, targets)
    node_idx = b.add_node("cubic_node", mesh_idx)

    times = [0.0, 1.0, 2.0]
    # Per keyframe: (in tangent, value, out tangent), two components each
    keys = [
        ([0.0, 0.0], [0.0, 0.0], [1.5, 0.5]),
        ([-1.0, 0.0], [1.0, 0.5], [1.0, 1.0]),
        ([-1.5, -0.5], [0.0, 1.0], [0.0, 0.0]),
    ]
    b.add_weights_animation("cubic_anim", node_idx, times, keys, "CUBICSPLINE")
    glb = asset_path("morph_cubicspline.glb")
    b.write_glb(glb)

    flat = []
    for in_t, value, out_t in keys:
        flat.extend(in_t)
        flat.extend(value)
        flat.extend(out_t)
    samples = []
    for frame in (0, 30, 45, 60, 90, 120):
        t = frame / 60.0
        w = ref_weights(times, flat, 2, "CUBICSPLINE", t)
        samples.append({"frame": frame, "time": t, "weights": w,
                        "positions": ref_morphed(pos, [d1, d2], w)})
    write_ref(glb + ".ref.json", {
        "asset": "morph_cubicspline.glb",
        "targetNames": ["cubic_y", "cubic_z"],
        "defaultWeights": [0.0, 0.0],
        "interpolation": "CUBICSPLINE",
        "samples": samples,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 5: skin + morph coexistence (one bone, one morph target)
# ---------------------------------------------------------------------------

def gen_skin_morph():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    delta = [0.0, 1.0, 0.0] * 4
    targets = [{"name": "morph_with_bone", "positions": delta, "normals": [0.0] * 12}]
    mesh_idx = b.add_morph_mesh("skin_morph_mesh", pos, norm, targets)

    # Joint node hierarchy: root joint at origin, identity inverse bind matrix
    joint_node = len(b.gltf["nodes"])
    b.gltf["nodes"].append({"name": "joint", "translation": [0.0, 0.0, 0.0]})
    b.gltf["scenes"][0]["nodes"].append(joint_node)

    mesh_node = len(b.gltf["nodes"])
    b.gltf["nodes"].append({"name": "skin_morph_node", "mesh": mesh_idx, "skin": 0})
    b.gltf["scenes"][0]["nodes"].append(mesh_node)

    # Inverse bind matrices: identity matrix for the single joint
    identity = [1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0]
    ibm_view = b.add_view(struct.pack("<16f", *identity))
    ibm_acc = b.add_accessor(ibm_view, 5126, 1, "MAT4")
    b.gltf["skins"] = [{
        "name": "skin",
        "joints": [joint_node],
        "inverseBindMatrices": ibm_acc,
    }]

    # JOINTS_0 / WEIGHTS_0 on the primitive: every vertex bound fully to joint 0
    prim = b.gltf["meshes"][mesh_idx]["primitives"][0]
    joints = [0, 0, 0, 0] * 4
    weights_skin = [1.0, 0.0, 0.0, 0.0] * 4
    # ubyte joints accessor (5121, VEC4)
    jdata = struct.pack("<%dB" % len(joints), *joints)
    jview = b.add_view(jdata, target=34962)
    jacc = b.add_accessor(jview, 5121, 4, "VEC4")
    prim["attributes"]["JOINTS_0"] = jacc
    wview = b.add_float_accessor(weights_skin, 4, "VEC4", target=34962)
    prim["attributes"]["WEIGHTS_0"] = wview

    # Bone animation: translate joint on Y, sampled on 60 fps grid
    times = [0.0, 1.0]
    tr_values = [0.0, 0.0, 0.0, 0.0, 2.0, 0.0]
    input_acc = b.add_scalar_float_accessor(times)
    output_acc = b.add_float_accessor(tr_values, 3, "VEC3")
    b.gltf["animations"].append({
        "name": "bone_anim",
        "samplers": [{"input": input_acc, "output": output_acc, "interpolation": "LINEAR"}],
        "channels": [{"sampler": 0, "target": {"node": joint_node, "path": "translation"}}],
    })

    # Morph animation on the same model
    w_times = [0.0, 1.0]
    w_sets = [[0.0], [1.0]]
    b.add_weights_animation("morph_anim", mesh_node, w_times, w_sets, "LINEAR")

    glb = asset_path("morph_skin_coexist.glb")
    b.write_glb(glb)

    # Reference: morph first, then skinning with an identity inverse bind and a
    # translating joint => final = morphed + joint translation (weight 1.0)
    samples = []
    for frame in (0, 30, 60):
        t = frame / 60.0
        bone_y = lerp(0.0, 2.0, min(max(t, 0.0), 1.0))
        morph_w = lerp(0.0, 1.0, min(max(t, 0.0), 1.0))
        morphed = ref_morphed(pos, [delta], [morph_w])
        final_pos = [v + (bone_y if i % 3 == 1 else 0.0) for i, v in enumerate(morphed)]
        samples.append({"frame": frame, "time": t, "morphWeight": morph_w,
                        "boneTranslationY": bone_y, "positions": final_pos})
    write_ref(glb + ".ref.json", {
        "asset": "morph_skin_coexist.glb",
        "targetNames": ["morph_with_bone"],
        "defaultWeights": [0.0],
        "samples": samples,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 6: plain model without morph targets (regression guard)
# ---------------------------------------------------------------------------

def gen_plain():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    mesh_idx = b.add_morph_mesh("plain", pos, norm, [])
    # Remove the empty targets array to keep it a strictly plain mesh
    prim = b.gltf["meshes"][mesh_idx]["primitives"][0]
    del prim["targets"]
    del b.gltf["meshes"][mesh_idx]["targetNames"]
    del b.gltf["meshes"][mesh_idx]["extras"]
    b.add_node("plain_node", mesh_idx)
    glb = asset_path("plain_no_morph.glb")
    b.write_glb(glb)
    write_ref(glb + ".ref.json", {
        "asset": "plain_no_morph.glb",
        "targetNames": [],
        "defaultWeights": [],
        "samples": [{"frame": 0, "positions": pos, "normals": norm}],
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 7: over-limit targets (MAX_MESH_MORPH_TARGETS + extra), graceful truncation
# ---------------------------------------------------------------------------

def gen_over_limit():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    targets = []
    for i in range(34):  # 34 > MAX_MESH_MORPH_TARGETS (32)
        d = [float(i + 1) * 0.01, 0.0, 0.0] * 4
        targets.append({"name": "t%02d" % i, "positions": d})
    mesh_idx = b.add_morph_mesh("overlimit", pos, norm, targets)
    b.add_node("overlimit_node", mesh_idx)
    glb = asset_path("morph_over_limit.glb")
    b.write_glb(glb)
    write_ref(glb + ".ref.json", {
        "asset": "morph_over_limit.glb",
        "definedTargets": 34,
        "expectedLoadedTargets": 32,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 8: non-finite weight inside the binary animation buffer.
# cgltf accepts the (binary) NaN float; the raylib loader must clamp it to 0
# and emit a warning instead of propagating NaN into the vertex math.
# ---------------------------------------------------------------------------

def gen_non_finite():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    delta = [0.0, 1.0, 0.0] * 4
    targets = [{"name": "bad_weight", "positions": delta}]
    mesh_idx = b.add_morph_mesh("nonfinite", pos, norm, targets, weights=[0.0])
    node_idx = b.add_node("nonfinite_node", mesh_idx)

    # Build the weights output accessor manually so a NaN can be embedded in
    # the binary buffer (JSON has no portable NaN token, binary floats do)
    times = [0.0, 1.0]
    input_acc = b.add_scalar_float_accessor(times)
    nan_bytes = struct.pack("<f", float("nan"))
    good_bytes = struct.pack("<f", 1.0)
    out_view = b.add_view(nan_bytes + good_bytes)
    output_acc = b.add_accessor(out_view, 5126, 2, "SCALAR")

    b.gltf["animations"].append({
        "name": "nan_weights",
        "samplers": [{
            "input": input_acc,
            "output": output_acc,
            "interpolation": "LINEAR",
        }],
        "channels": [{"sampler": 0, "target": {"node": node_idx, "path": "weights"}}],
    })

    glb = asset_path("morph_non_finite.glb")
    b.write_glb(glb)
    write_ref(glb + ".ref.json", {
        "asset": "morph_non_finite.glb",
        "targetNames": ["bad_weight"],
        "defaultWeights": [0.0],
        "note": "Animation keyframe 0 embeds a binary NaN weight; loader must clamp to 0 and warn",
        # At frame 0 the NaN keyframe must be clamped to 0 => base geometry unchanged
        "samples": [{
            "frame": 0,
            "weights": [0.0],
            "positions": pos,
        }],
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 9: morph target POSITION stored in a sparse accessor (must degrade:
# target loads with no positions and a warning, model stays valid)
# ---------------------------------------------------------------------------

def gen_sparse_target():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    mesh_idx = b.add_morph_mesh("sparse_mesh", pos, norm, [])
    prim = b.gltf["meshes"][mesh_idx]["primitives"][0]

    # Sparse accessor: 4 vec3 slots, only index 1 carries a delta; no base bufferView
    # (sparse indices: u16, sparse values: float vec3)
    indices = struct.pack("<H", 1)
    values = struct.pack("<3f", 0.0, 3.0, 0.0)
    idx_view = b.add_view(indices)
    val_view = b.add_view(values)
    sparse_acc = len(b.gltf["accessors"])
    b.gltf["accessors"].append({
        "componentType": 5126,
        "count": 4,
        "type": "VEC3",
        "min": [0.0, 0.0, 0.0],
        "max": [0.0, 3.0, 0.0],
        "sparse": {
            "count": 1,
            "indices": {"bufferView": idx_view, "componentType": 5123},
            "values": {"bufferView": val_view},
        },
    })

    # Normal target is plain and must still load
    delta_norm = [1.0, 0.0, 0.0] * 4
    norm_acc = b.add_float_accessor(delta_norm, 3, "VEC3", target=34962)
    prim["targets"] = [{"POSITION": sparse_acc, "NORMAL": norm_acc}]
    b.gltf["meshes"][mesh_idx]["targetNames"] = ["sparse_lift"]
    b.gltf["meshes"][mesh_idx]["extras"] = {"targetNames": ["sparse_lift"]}
    b.add_node("sparse_node", mesh_idx)

    glb = asset_path("morph_sparse.glb")
    b.write_glb(glb)
    write_ref(glb + ".ref.json", {
        "asset": "morph_sparse.glb",
        "targetNames": ["sparse_lift"],
        "expectedLoadedTargets": 1,
        "expectPositionsLoaded": False,
        "expectNormalsLoaded": True,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 10: morph target referencing a non-existent accessor index.
# cgltf rejects the whole parse, so LoadModel must return an empty model.
# ---------------------------------------------------------------------------

def gen_missing_accessor():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    mesh_idx = b.add_morph_mesh("missing_mesh", pos, norm, [])
    prim = b.gltf["meshes"][mesh_idx]["primitives"][0]
    prim["targets"] = [{"POSITION": 9999}]
    b.gltf["meshes"][mesh_idx]["targetNames"] = ["broken"]
    b.gltf["meshes"][mesh_idx]["extras"] = {"targetNames": ["broken"]}
    b.add_node("missing_node", mesh_idx)

    glb = asset_path("morph_missing_accessor.glb")
    b.write_glb(glb)
    write_ref(glb + ".ref.json", {
        "asset": "morph_missing_accessor.glb",
        "expectLoadFailure": True,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 11: ONE animation carrying both a bone channel and a weights channel.
# Drives the in-function morph-then-skin order through a single
# UpdateModelAnimation() call.
# ---------------------------------------------------------------------------

def gen_combined():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    delta = [0.0, 1.0, 0.0] * 4
    targets = [{"name": "combined_morph", "positions": delta}]
    mesh_idx = b.add_morph_mesh("combined_mesh", pos, norm, targets)

    joint_node = len(b.gltf["nodes"])
    b.gltf["nodes"].append({"name": "joint", "translation": [0.0, 0.0, 0.0]})
    b.gltf["scenes"][0]["nodes"].append(joint_node)

    mesh_node = len(b.gltf["nodes"])
    b.gltf["nodes"].append({"name": "combined_node", "mesh": mesh_idx, "skin": 0})
    b.gltf["scenes"][0]["nodes"].append(mesh_node)

    identity = [1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0]
    ibm_view = b.add_view(struct.pack("<16f", *identity))
    ibm_acc = b.add_accessor(ibm_view, 5126, 1, "MAT4")
    b.gltf["skins"] = [{"name": "skin", "joints": [joint_node], "inverseBindMatrices": ibm_acc}]

    prim = b.gltf["meshes"][mesh_idx]["primitives"][0]
    joints = [0, 0, 0, 0] * 4
    skin_w = [1.0, 0.0, 0.0, 0.0] * 4
    jdata = struct.pack("<%dB" % len(joints), *joints)
    jview = b.add_view(jdata, target=34962)
    prim["attributes"]["JOINTS_0"] = b.add_accessor(jview, 5121, 4, "VEC4")
    prim["attributes"]["WEIGHTS_0"] = b.add_float_accessor(skin_w, 4, "VEC4", target=34962)

    times = [0.0, 1.0]

    # Bone translation sampler/channel
    tr_input = b.add_scalar_float_accessor(times)
    tr_output = b.add_float_accessor([0.0, 0.0, 0.0, 0.0, 2.0, 0.0], 3, "VEC3")

    # Morph weights sampler/channel
    w_input = b.add_scalar_float_accessor(times)
    w_output = b.add_scalar_float_accessor([0.0, 1.0])

    b.gltf["animations"].append({
        "name": "combined_anim",
        "samplers": [
            {"input": tr_input, "output": tr_output, "interpolation": "LINEAR"},
            {"input": w_input, "output": w_output, "interpolation": "LINEAR"},
        ],
        "channels": [
            {"sampler": 0, "target": {"node": joint_node, "path": "translation"}},
            {"sampler": 1, "target": {"node": mesh_node, "path": "weights"}},
        ],
    })

    glb = asset_path("morph_combined.glb")
    b.write_glb(glb)

    samples = []
    for frame in (0, 30, 60):
        t = frame / 60.0
        u = min(max(t, 0.0), 1.0)
        bone_y = lerp(0.0, 2.0, u)
        morph_w = lerp(0.0, 1.0, u)
        morphed = ref_morphed(pos, [delta], [morph_w])
        final_pos = [v + (bone_y if i % 3 == 1 else 0.0) for i, v in enumerate(morphed)]
        samples.append({"frame": frame, "time": t, "morphWeight": morph_w,
                        "boneTranslationY": bone_y, "positions": final_pos})
    write_ref(glb + ".ref.json", {
        "asset": "morph_combined.glb",
        "targetNames": ["combined_morph"],
        "defaultWeights": [0.0],
        "singleAnimationWithBoneAndMorph": True,
        "samples": samples,
        "tolerance": 1e-4,
    })


# ---------------------------------------------------------------------------
# Asset 12: one node/mesh with TWO triangle primitives, both carrying morph
# targets. A single weights channel must drive both resulting raylib meshes.
# ---------------------------------------------------------------------------

def gen_multi_primitive():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    delta_a = [0.0, 1.0, 0.0] * 4
    delta_b = [0.0, 0.0, 2.0] * 4

    pos_acc = b.add_float_accessor(pos, 3, "VEC3", target=34962)
    norm_acc = b.add_float_accessor(norm, 3, "VEC3", target=34962)
    da_acc = b.add_float_accessor(delta_a, 3, "VEC3", target=34962)
    db_acc = b.add_float_accessor(delta_b, 3, "VEC3", target=34962)

    mesh = {
        "name": "two_prim_mesh",
        "primitives": [
            {"attributes": {"POSITION": pos_acc, "NORMAL": norm_acc},
             "targets": [{"POSITION": da_acc}]},
            {"attributes": {"POSITION": pos_acc, "NORMAL": norm_acc},
             "targets": [{"POSITION": db_acc}]},
        ],
        "targetNames": ["prim_morph"],
        "weights": [0.0],
    }
    b.gltf["meshes"].append(mesh)
    mesh_idx = len(b.gltf["meshes"]) - 1
    node_idx = b.add_node("two_prim_node", mesh_idx)

    times = [0.0, 1.0]
    b.add_weights_animation("two_prim_anim", node_idx, times, [[0.0], [1.0]], "LINEAR")

    glb = asset_path("morph_multi_primitive.glb")
    b.write_glb(glb)

    # At weight 1.0 the first raylib mesh uses delta_a, the second delta_b
    write_ref(glb + ".ref.json", {
        "asset": "morph_multi_primitive.glb",
        "expectedMeshCount": 2,
        "frame": 60,
        "weight": 1.0,
        "mesh0Positions": ref_morphed(pos, [delta_a], [1.0]),
        "mesh1Positions": ref_morphed(pos, [delta_b], [1.0]),
        "tolerance": 1e-4,
    })


def quat_rotate(qx, qy, qz, qw, v):
    """Rotate vector v by quaternion (qx,qy,qz,qw): v' = v + 2*u x (u x v + w*v)."""
    vx, vy, vz = v
    # u x v
    cx = qy * vz - qz * vy
    cy = qz * vx - qx * vz
    cz = qx * vy - qy * vx
    # u x v + w*v
    tx, ty, tz = cx + qw * vx, cy + qw * vy, cz + qw * vz
    # u x t
    dx = qy * tz - qz * ty
    dy = qz * tx - qx * tz
    dz = qx * ty - qy * tx
    return (vx + 2.0 * dx, vy + 2.0 * dy, vz + 2.0 * dz)


def world_point(t, q, s, p):
    """glTF node world transform of a point: translation + scale * rotation(p)."""
    r = quat_rotate(q[0], q[1], q[2], q[3], p)
    return (t[0] + s[0] * r[0], t[1] + s[1] * r[1], t[2] + s[2] * r[2])


def world_vector(q, s, d):
    """Transform of a direction delta: scale * rotation(d), no translation."""
    r = quat_rotate(q[0], q[1], q[2], q[3], d)
    return (s[0] * r[0], s[1] * r[1], s[2] * r[2])


def world_normal(q, s, n):
    """Transform of a normal under uniform scale: rotation(n)/scale."""
    r = quat_rotate(q[0], q[1], q[2], q[3], n)
    return (r[0] / s[0], r[1] / s[1], r[2] / s[2])


# ---------------------------------------------------------------------------
# Asset 13: morph target on a NODE with a non-trivial TRS world transform.
# Verifies delta transformation (rotation/scale for positions, inverse-transpose
# for normals, translation excluded from deltas) matches a reference viewer.
# ---------------------------------------------------------------------------

def gen_transformed_node():
    b = GltfBuilder()
    pos, norm, _, _ = quad_geometry()
    delta_pos = [0.0, 1.0, 0.0] * 4
    delta_norm = [1.0, 0.0, 0.0] * 4
    targets = [{"name": "xform_morph", "positions": delta_pos, "normals": delta_norm}]
    mesh_idx = b.add_morph_mesh("xform_mesh", pos, norm, targets, weights=[0.5])
    node_idx = b.add_node("xform_node", mesh_idx)

    rotz = math.pi / 2.0
    qx, qy, qz, qw = 0.0, 0.0, math.sin(rotz / 2.0), math.cos(rotz / 2.0)
    node = b.gltf["nodes"][node_idx]
    node["translation"] = [1.0, 2.0, 3.0]
    node["rotation"] = [qx, qy, qz, qw]
    node["scale"] = [2.0, 2.0, 2.0]

    glb = asset_path("morph_transformed_node.glb")
    b.write_glb(glb)

    # Reference computed straight from glTF semantics (quaternion rotation),
    # matching the loader: base points get the full T*R*S, position/tangent
    # deltas get R*S (translation excluded), normal deltas use the inverse-transpose
    t = [1.0, 2.0, 3.0]
    q = [qx, qy, qz, qw]
    s = [2.0, 2.0, 2.0]

    w = 0.5
    exp_pos = []
    exp_norm = []
    for i in range(4):
        bp = pos[3 * i:3 * i + 3]
        dp = delta_pos[3 * i:3 * i + 3]
        bn = norm[3 * i:3 * i + 3]
        dn = delta_norm[3 * i:3 * i + 3]

        pbase = world_point(t, q, s, bp)
        pdelta = world_vector(q, s, dp)        # no translation
        nbase = world_normal(q, s, bn)
        ndelta = world_normal(q, s, dn)

        exp_pos += [pbase[k] + w * pdelta[k] for k in range(3)]
        exp_norm += [nbase[k] + w * ndelta[k] for k in range(3)]

    write_ref(glb + ".ref.json", {
        "asset": "morph_transformed_node.glb",
        "targetNames": ["xform_morph"],
        "defaultWeights": [0.5],
        "samples": [{
            "frame": 0,
            "weights": [0.5],
            "positions": exp_pos,
            "normals": exp_norm,
        }],
        "tolerance": 1e-4,
    })


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    gen_multi_defaults()
    gen_step()
    gen_cubic()
    gen_skin_morph()
    gen_plain()
    gen_over_limit()
    gen_non_finite()
    gen_sparse_target()
    gen_missing_accessor()
    gen_combined()
    gen_multi_primitive()
    gen_transformed_node()
    print("Generated morph test assets in %s" % OUT_DIR)
    for name in sorted(os.listdir(OUT_DIR)):
        print(" -", name)


if __name__ == "__main__":
    main()
