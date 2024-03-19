# USD_SBSAR
This is a plugin for consuming Substance Archive (SBSAR) files as USD data.

* Note: an issue was discovered with USD v24.03 where values set on procedural parameters do not reach the file format plugin and it cannot generate the correct procedural texture paths.  A solution is being worked on.

## Current status
The design for the plugin is modelled after the USDZ plugin as a
combination of a file format and a package resolver.
The file format component creates USD data for all the materials and
shaders. Textures/Images are however not a USD data type but loaded
as assets. The USD data from the file format generates paths
with parameter strings into the same file and when USD resolves them
they are passed to the package resolver that can generate the
appropriate textures.

## Requirements
- USD v23.08 or further  (Note: an issue was found with v24.03)
- Substance engine v9.1.12 or further

# Supported Features
- Presets
- Output size
- Numeric input values
- Image inputs
- Numeric output values for use in Adobe Standard Materials.

# How to use
To create a USD scene that uses Substance materials you can to reference a `.sbsar` file as sublayer in a USD file.
```usda
    subLayers = [@./path/to/material.sbsar@]
```

The material can be tweaked via variant selections and overrides and bound to geometry:
```usda
override "SbsarGraphName" (
    variants = {
        string preset = "MyPerfectPreset"
        string resolution = "res1024x1024"
    }
)
{
    # Adding custom parameters, note that these are
    # stronger than parameters set through presets

    # Numeric parameters
    float procedural_sbsar:myNumericalParameters = 0.8
    # Image parameters
    asset procedural_sbsar:myImageParameters = @path/to/my/image.png@
}

Mesh "MyMesh"(
    prepend apiSchemas = ["MaterialBindingAPI"]
)
{
    rel material:binding = </SbsarGraphName> (
        bindMaterialAs = "weakerThanDescendants"
    )
}
```

A SBSAR material can also be referenced directly onto a `Material` prim via a reference.
The referenced material can again be tweaked via variant selections and overrides.
```usda
Material "Material" (
    prepend references=@./path/to/material.sbsar@
    variants = {
        string preset = "MyPerfectPreset"
        string resolution = "res1024x1024"
    }
)
{
    # Adding custom parameters, note that these are
    # stronger than parameters set through presets

    # Numeric parameters
    float procedural_sbsar:myNumericalParameters = 0.8
    # Image parameters
    asset procedural_sbsar:myImageParameters = @path/to/image.png@
}
```
There are two variants on each material prim to select the `preset` and the `resolution` of the output image.
The procedural parameters are set as attributes on the Material prim and are stronger than the preset values.

### Environment
In case the SBSAR contains a procedural environment map, the plugin will generate a `DomeLight` prim with a procedural texture, similar to the procedural textures of a material.

Like the material version, the `.sbsar` file can be referenced as a sublayer, which will create a dome light named after the Substance graph.
```usda
(
    subLayers = [@./path/to/environment.sbsar@]
)
```

The SBSAR can also be referenced onto a `DomeLight` prim and tweaked via variant selections and overrides as attributes on the DomeLight prim.
```usda
def DomeLight "myDomeLight" (
    prepend references=@./path/to/environment.sbsar@
    variants = {
        string resolution = "res2048x1024"
    }
)
{
    float procedural_sbsar:myNumericalParameters = 0.8
}
```

### Thumbnails
If the SBSAR contains a thumbnail, the thumbnail path is generated and saved in the `assetInfo` of the material prim.
```usda
def Material "SbsarGraphName" (
    ...
     assetInfo =
      dictionary previews = {
        dictionary thumbnails = {
             dictionary default = {
                 asset defaultImage = @./path/to/material.sbsar[thumbnails/SbsarGraphName.png]@
             }
        }
    }
    ...)
    { ... }
```
The thumbnail path format of the graph can be `./path/sbsar.sbsar[thumbnails/{graphName}.png]`.
You can also specify it with the file name and the `thumbnail.png` (i.e. `./path/sbsar.sbsar[thumbnail.png]`), which returns the thumbnail of the material graph that matches the name of the SBSAR. If no such graph exists the thumbnail of the first graph is returned.


## Sample data
There are samples in the data directory that show how you can interact with Substance materials in USD.

### [simplest.usda](data/simplest.usda)
This sample just shows how to reference a material as a sublayer and assign it toa model.

### [variants.usda](data/variants.usda)
This sample shows how to use variants and attributes to set parameters of materials.

### [direct_reference.usda](data/direct_reference.usda)
This sample shows how you can reference materials directly onto a prim without using a sublayer.

### [variants_image.usda](data/variants_image.usda)
This sample shows how to set an input image.

### [environment.usda](data/environment.usda)
This sample shows how to use a SBSAR to generate an environment map

# Technical Details
The plugin is implemented as a combination of a file format plugin and
a custom package resolver. The reason for this design is that USD doesn't consider
textures to be part of the scene description but are treated as assets.
Assets are external to USD and in order to get procedural behavior they will be
resolved through our custom package resolver to create images on demand.
The file format plugin will load the SBSAR and generate scene description
with asset paths which will get resolved through the package resolver plugin.

## Asset Paths
The scene description contains asset paths looking like this:
```
@./path/to/my/material.sbsar[graphs/SbsarGraphName/images?usage=ambientOcclusion#packageHash=b427747e86441362#params={"$outputsize":\[9,9\],"$randomseed":0,"cardboard_color":\[0.58890700340271,0.46410301327705383,0.3237049877643585\],"tearing":0.7099999785423279}]@
```

When USD's Hydra sees a path like that it will use its asset resolution to forward
it to the custom package resolver for SBSAR which takes over and resolves
the path by calling the Substance engine to generates the texture on demand.
Note that these paths can contain Substance parameters to control the generation.

## Dynamic File Formats
In order to generate scene description that reacts to attribute and metadata changes, the
material must be referenced as a payload. Payload references are poorly supported
in USD editors, so in order to prevent this from turning into an obstacle for users, it is
automatically done through an indirection inside of the plugin generated scene description.
The user can just reference the top level file in a normal way and the dynamic aspects are
supported

## UsdPreviewSurface, ASM
Substance materials in USD come with multiple material networks.
If you look at the output surface tokens on the material prim you can see the different connections.
```
token outputs:surface.connect = </SbsarGraphName/UsdPreviewSurface/ShaderUsdPreviewSurface.outputs:surface>
token outputs:adobe:surface.connect = </SbsarGraphName/ASM/AdobeStandardMaterial.outputs:surface>
```

## Debug codes
The plugin has the following debug information channels which can be enabled via the `TF_DEBUG`
environment variable to see information about what is going on: (see [sbsarDebug.h](src/sbsarDebug.h))
* FILE_FORMAT_SBSAR
* SBSAR_PACKAGE_RESOLVER
* SBSAR_RENDER

To enable debug information execute the following
```
# Windows
# All plugin channels
set TF_DEBUG=SBSAR_*
# A single channel
set TF_DEBUG=SBSAR_RENDER

# Linux/Mac
# All plugin channels
export TF_DEBUG=SBSAR_*
# A single channel
export TF_DEBUG=SBSAR_RENDER
```
