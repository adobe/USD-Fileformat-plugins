# USDGLTF

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2311-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/windows-2022-2308-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-14-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2311-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/macOS-13-2308-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

[![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2405-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2311-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml) [![](https://img.shields.io/endpoint?url=https://gist.githubusercontent.com/kwblackstone/264643f3d2acacc5369a0ba70854dfb6/raw/ubuntu-22.04-2308-GLTF.json)](https://github.com/adobe/USD-Fileformat-plugins/actions/workflows/ci.yml)

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
| KHR_draco_mesh_compression |✅|
| KHR_lights_punctual |❌|
| KHR_materials_anisotropy |⚠️|Roundtripping will preserve the data, but proper translation is not there yet.|
| KHR_materials_clearcoat |✅|
| KHR_materials_emissive_strength |✅|
| KHR_materials_ior |✅|
| KHR_materials_iridescence |❌|
| KHR_materials_sheen |✅|
| KHR_materials_specular |✅|
| KHR_materials_transmission |
| KHR_materials_unlit |❌|
| KHR_materials_variants |❌|
| KHR_materials_volume |✅|
| KHR_mesh_quantization |❌|
| KHR_texture_basisu |❌|
| KHR_texture_transform |✅|Written to a UsdTransform2d node|
| KHR_xmp_json_ld |❌|
| EXT_mesh_gpu_instancing |❌|
| EXT_meshopt_compression |❌|
| EXT_texture_webp |✅|
| ADOBE_materials_clearcoat_specular |✅|
| ADOBE_materials_clearcoat_tint |✅|
| KHR_materials_pbrSpecularGlossiness |✅|



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

* `gltfAssetsPath`: Filesystem path where image assets are saved to.
    The default is that image assets are not copied, but the generated usd file will resolve them from the original file.
    The following saves images to the path `myPath` during `UsdStage::Open` and then exports the stage to that same path.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.gltf:SDF_FORMAT_ARGS:gltfAssetsPath=myPath")
    stage->Export("myPath/cube.usd")
    ```

**Export:**

* `embedImages` Embed images, as base64 for `gltf` or as binary data for `glb`. Default is `true`.
    The following exports to `glb` and does not embed images:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.usd");
    SdfLayer::FileFormatArguments args = { {"embedImages", "false"} };
    stage->Export("cube.glb", false, args);
    ```
* `useMaterialExtensions`: Use glTF material extensions. Default is `true`.

## Debug codes
* `FILE_FORMAT_GLTF`: Common debug messages.
* `GLTF_PACKAGE_RESOLVER`: Asset resolution debug messages, when resolving images from the original
  gltf file.
