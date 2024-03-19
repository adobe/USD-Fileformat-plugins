# USDFBX

## Supported features

|Feature|Import|Export|
|--|--|--|
|Scene up axis            |✅|✅|
|Scene units              |✅|✅|
||||
|Node hierarchy           |✅|✅|
|Node transform matrix    |✅|✅|
|Node TRS                 |❌|❌|
|Node animations          |⚠️|⚠️|
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
|Mesh bounding box        |❌|❌|
||||
|Nurbs                    |❌|❌|
||||
|Skeletons                |✅|✅|
|Skeleton Animations      |✅|⚠️|
||||
|Materials                |✅|✅|




## Translation Notes

**Import:**


|Material Import|
|--|
|PhongSurface → UsdPreviewSurface|
|FbxSurfaceLambert → UsdPreviewSurface|
|Hardware material is not imported|


**Export:**


|Material Export|
|--|
diffuseColor → phongSurface::Diffuse
emissiveColor →  phongSurface::Emissive and  phongSurface::EmissiveFactor = 1
normal → phongSurface::NormalMap
specularColor → phongSurface::Specular (if useSpecularWorkflow = on) (disabled temporarily)
metallic →  phongSurface::Specular (if useSpecularWorkflow = off) (disabled temporarily)
roughness →  1 - phongSurface::Shininess (experimental) (disabled temporarily)
occlusion →  phongSurface::AmbientOcclusion
clearcoat → phongSurface::Reflection
clearcoatRoughness → Not been used.
opacity → phongSurface::TransparentColor
ior → Not been used.
displacement → phongSurface::DisplacementColor

- **OBS: The image files used by the UsdPreviewShader node will be extracted from the USDZ file and saved as PNG files in the same folder as the generated fbx. If the source file is USD the files should also be copied from the USD folder into the FBX folder.**

## File Format Arguments
**Import:**

* `fbxAssetsPath`: Filesystem path where image assets are saved to.
    By default image assets are not copied, but the generated usd file will resolve them from the original file.
    The following saves images to the path `myPath` during `UsdStage::Open` and then exports the stage to that same path.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxAssetsPath=myPath")
    stage->Export("myPath/cube.usd")
    ```

* `fbxPhong`: Forces phong to PBR material conversion.
    By default turned off: the plugin imports the diffuse component only, without specularities.
    The following converts PBR to phong.
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.fbx:SDF_FORMAT_ARGS:fbxPhong=true")
    stage.Export("cube.usd")
    ```
    The phong to PBR conversion follows https://docs.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping. Keep in mind it is a lossy conversion.

**Export:**

* `embedImages` Embed images in the exported fbx file instead of as separate files. Default is `false`.
    The following exports to `fbx` and embeds images:
    ```
    UsdStageRefPtr stage = UsdStage::Open("cube.usd");
    SdfLayer::FileFormatArguments args = { {"embedImages", "true"} };
    stage->Export("cube.fbx", false, args);
    ```

## Debug codes
* `FILE_FORMAT_FBX`: Common debug messages.
* `FBX_PACKAGE_RESOLVER`: Asset resolution debug messages, when resolving images from the original
  FBX file.



## Known Issues

**Import:**
* Animations on non-skeletal nodes is not supported or in some cases is lost.
* Edge information is ignored which can lead to incorrect triangulation of quads
* Multiple animation tracks are not supported
* Issues with layered textures

**Export:**
* Skeletal animation is lost, due to problems with skeletons with negative scaling values.
