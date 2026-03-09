# USDGLTF

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2411-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2408-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2311-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2308-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2411-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2408-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2411-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2408-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2311-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2308-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2411-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2408-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2311-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2308-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

## Supported features

|Feature|Import|Export|
|--|--|--|
|Scene up axis            |✅|✅|
|Scene units              |✅|✅|
||||
|Node hierarchy           |✅|✅|
|Node transform matrix    |✅|✅|
|Node TRS                 |✅|⚠️|
|Node animations          |✅|✅|
||||
|Cameras                  |✅|✅|
||||
|Mesh positions           |✅|✅|
|Mesh normals             |✅|✅|
|Mesh uvs                 |✅|✅|
|Mesh vertex colors       |✅|✅|
|Mesh skinning            |✅|✅|
|Mesh blend shapes        |❌|❌|
|Mesh instancing          |✅|✅|
|Mesh bounding box        |❌|✅|
||||
|Nurbs                    |⦸|❌|
||||
|Skeletons                |✅|✅|
|Skeleton Animations      |✅|✅|
||||
|Materials                |✅|✅|
||||
|[Neural Assets (v0.4)](README_NGP.md) |✅|✅|



## Translation Notes

**Import:**

|Material Import|
|--|
UsdPreviewSurface with specular workflow = off
baseColorFactor OR baseColorTexture[rgb] → diffuseColor
emissiveFactor OR emissiveTexture[rgb] → emissiveColor
normalTexture[rgb] → normal
occlusionTextureFactor → occlusion
metallicFactor OR metallicRoughnessTexture[b] → metallic
roughnessFactor OR metallicRoughnessTexture[g] → roughness
baseColorTexture[a] if MODE is not OPAQUE → opacity
alphaCutoff if MODE is MASK → opacityThreshold

During material import, the ASM shading model is used as an intermediate transport layer.

|glTF extension|Support|Notes|
|--|--|--|
| KHR_animation_pointer |❌|
| KHR_draco_mesh_compression |✅|
| KHR_lights_punctual |✅|
| KHR_materials_anisotropy |✅|
| KHR_materials_clearcoat |✅|
| KHR_materials_dispersion |❌|
| KHR_materials_emissive_strength |✅|
| KHR_materials_ior |✅|
| KHR_materials_iridescence |❌|
| KHR_materials_sheen |✅|
| KHR_materials_specular |✅|
| KHR_materials_transmission |
| KHR_materials_unlit |❌|
| KHR_materials_variants |❌|
| KHR_materials_volume |✅|
| KHR_materials_volume_scatter |✅|
| KHR_mesh_quantization |❌|
| KHR_texture_basisu |❌|
| KHR_texture_transform |✅|Written to a UsdTransform2d node|
| KHR_xmp_json_ld |❌|
| EXT_mesh_gpu_instancing |❌|
| EXT_meshopt_compression |❌|
| EXT_texture_webp |✅|
| ADOBE_materials_clearcoat_specular |✅|
| ADOBE_materials_clearcoat_tint |✅|
| EXT_materials_clearcoat_color |✅|
| KHR_materials_coat |✅|
| KHR_materials_pbrSpecularGlossiness |✅|

Anisotropy
- Anisotropy Strength to ASM Level:
  - Formula:
    - Step 1: ASM Level = √√(strength² × (1 - roughness²))
  - Description:
    - Calculates the ASM anisotropy level by squaring the strength, scaling it by the roughness, and then taking the fourth root of the result.

- Anisotropy Rotation to ASM Rotation:
  - Formula:
    - Step 1: normalized_angle = angle / (2 × PI)
    - Step 2: ASM Rotation = normalized_angle - floor(normalized_angle)
  - Description:
    - Normalizes the rotation angle by dividing by 2π and ensures it wraps within the [0, 1] range by subtracting the floor value.

- Image-Based Anisotropy Rotation:
  - Formula:
    - Step 1:  vec = (redChannelValue × 2 - 1, greenChannelValue × 2 - 1)
    - Step 2:  angle = atan2(vec.y, vec.x) + rotation
    - Step 3:  normalized_angle = angle / (2 × PI)
    - Step 4:  ASM Rotation = normalized_angle - floor(normalized_angle)
  - Description:
    - Converts red and green channel values to a vector, calculates the angle with an offset rotation, normalizes the angle, and wraps it within the [0, 1] range.

**Export:**

|Material Export|
|--|
diffuseColor → baseColorFactor OR baseColorTexture[rgb]
emissiveColor → emissiveFactor OR emissiveTexture[rgb]
normal → normalTexture[rgb]
occlusion → occlusionTexture[r]
metallic →  metallicFactor OR metallicRoughnessTexture[b]
roughness →  roughnessFactor OR metallicRoughnessTexture[g]
opacity → baseColorTexture[a]
opacityThreshold → alphaCutoff



Export can optionally make use glTF material extensions.

During material export, the ASM shading model is used as an intermediate transport layer.

If the material extensions are turned off, transmission is transcoded into opacity
to preserve glass and similar translucent materials as best as possible.

Export of node TRS works for animated nodes, but not for static ones at the moment.

Mesh bounding box exported as min and max accessor bounds in glTF.

## File Format Arguments

**Import:**

* `assetsPath`: Filesystem path where image assets are saved to during import. Default is `""`

    By default image textures used by the asset are not copied during import, but are kept in memory and are available
    via an associated `ArResolver` plugin. By specifying a filesystem location via `assetsPath`, the import process will
    copy the image textures to that location and provide asset paths to those locations in the generated USD data. This
    file format argument allows an easy way to export associated images textures to disk when converting an asset to USD.

    This snippet saves image textures to the path at `exportPath` during `Usd.Stage.Open` and then also exports the stage
    to that same location, so that the USD data and the used images a co-located.
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("asset.gltf:SDF_FORMAT_ARGS:assetsPath=exportPath")
    stage.Export("exportPath/asset.usd")
    ```

* `gltfAssetsPath`: Deprecated in favor of `assetsPath`.

* `writeUsdPreviewSurface`: Generate a UsdPreviewSurface based network for each material. Default is `true`

    UsdPreviewSurface and its associated nodes are a universally understood USD material description
    and all application should support them. The PBR capabilities are limited.

* `writeASM`: Generate a ASM (Adobe Standard Material) based network for each material. Default is `true`

    ASM is a standard supported by many Adobe applications with richer support for PBR capabilities.
    It will be superseded by OpenPBR in the near future.

* `writeOpenPBR`: Generate a OpenPBR based material network for each material. Default is `false`

    OpenPBR is a new industry standard that will have wide spread support, but is still in its infancy.
    The material network uses `MaterialX` nodes to express individual operations and has an `OpenPBR` surface,
    which has rich support for PBR oriented materials.

* `preserveExtraMaterialInfo`: Generate shading networks with extra data for transcoding. Default is `true`
    When this is enabled, the generated shading networks might contain extra inputs that are outside of the respective
    material surface schema, that are useful for transcoding purposes. For example, the `OpenPBR` surface does not have
    an `occlusion` input for ambient occlusion, but we might want to express such a signal, if it was present in the
    source asset, so that an exporter can pick-up said signal and use it when generating an output asset.
    When `preserveExtraMaterialInfo` is `false`, the code will not generate these extra fields that are outside of the
    schema, which won't affect renders, but can affect the transcoding abilities.

* `gltfAnimationTracks`: Import multiple animation tracks. Default is `false`

    By default only the first animation track is imported.
    It is only recommended to use this parameter in order to convert from GLTF to another format that supports multiple
    animation tracks, such as FBX. It is not recommended to export a .usd file after importing a file with this parameter
    set, as there is no standard way to encode this information.

    The following allows additional animation tracks to be imported, and adds metadata to USD to encode where each track
    begins and ends. The exporter can then read this metadata to export the tracks properly.
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("animAsset.gltf:SDF_FORMAT_ARGS:gltfAnimationTracks=true")
    stage.Export("animAsset.fbx")
    ```


**Export:**

* `embedImages` Embed images, as base64 for `gltf` or as binary data for `glb`. Default is `true`.

    The following exports to `glb` and does not embed images:
    ```
    from pxr import Usd
    stage = Usd.Stage.Open("cube.usd");
    stage.Export("cube.glb", args={ "embedImages": "false" });
    ```

* `useMaterialExtensions`: Use glTF material extensions. Default is `true`.

## Debug codes
* `FILE_FORMAT_GLTF`: Common debug messages.
* `GLTF_PACKAGE_RESOLVER`: Asset resolution debug messages, when resolving images from the original
  gltf file.
