# USDOBJ

## Supported features

|Feature|Import|Export|
|--|--|--|
|Scene up axis            |⦸|⦸|
|Scene units              |⦸|⦸|
||||
|Node hierarchy           |⦸|⦸|
|Node transform matrix    |⦸|⦸|
|Node TRS                 |⦸|⦸|
|Node animations          |⦸|⦸|
||||
|Cameras                  |⦸|⦸|
||||
|Mesh positions           |✅|✅|
|Mesh normals             |✅|✅|
|Mesh uvs                 |✅|✅|
|Mesh vertex colors       |✅|✅|
|Mesh skinning            |⦸|⦸|
|Mesh blend shapes        |⦸|⦸|
|Mesh instancing          |⦸|⦸|
|Mesh bounding box        |⦸|⦸|
||||
|Nurbs                    |❌|❌|
||||
|Skeletons                |⦸|⦸|
|Skeleton Animations      |⦸|⦸|
||||
|Materials                |✅|✅|





## Translation Notes

**Import:**

The generated USD will keep default units and up axis (1cm, +y).

Allows importing obj from ZBrush with vertex color (#MRGB tag)

**Export:**

Meshes distributed in the node hierarchy in USD will be transformed by their global transform 
during the export, since obj does not support nodes.
Also, the resulting meshes will have units = 1m and up axis = +y.


## File Format Arguments

**Import:**

Example of how to pass a dynamic file format option to export images to a certain location.
This makes the asset paths be pointing to newly generated images in the filesystem.
Then the stage is exported to that same location.
```
from pxr import Usd
stage = Usd.Stage.Open("assets/obj/car/Pony_Cartoon.obj:SDF_FORMAT_ARGS:objAssetsPath=assets-build")
stage.Export("assets-build/car.usda")
```

By default, the plugin imports the diffuse component only, without specularities, but you can force to import the full phong model like this:
```
from pxr import Usd
stage = Usd.Stage.Open("assets/obj/car.obj:SDF_FORMAT_ARGS:objAssetsPath=assets-build&objPhong=true")
stage.Export("assets-build/car.usda")
```
The phong to PBR conversion follows https://docs.microsoft.com/en-us/azure/remote-rendering/reference/material-mapping. Keep in mind it is a lossy conversion.
> Note: currently this only works when also providing objAssetsPath (TODO fix).

## Debug codes
* `FILE_FORMAT_OBJ`: Common debug messages.
* OBJ_PACKAGE_RESOLVER



