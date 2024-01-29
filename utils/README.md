# usd-plugin-utils

These utilities for the USD file format plugins define the following:

1. Simple data structures that hold USD data.
2. Functions that read/write said data from/to the USD layer.
3. A custom package resolver interface.
4. Geometry, material and image utilities (filesystem read/write, phong->PBR, caching).
5. Test utilities.

A given file format plugin needs only define 2 things:

1. The file format native reader/writer (possibly implemented by a third party library).
2. Functions that translate between USD data and the file format's own data.
3. Implement the custom package resolver interface to read texture data from the origin file.


Import example:
```
bool UsdObjFileFormat::Read(
    SdfLayer* layer,
    const std::string& resolvedPath,
    bool metadataOnly
) const {
    Obj obj;
    UsdData usd;
    readObj(obj, resolvedPath);
    ImportObjOptions options;
    importObj(options, obj, usd);
    WriteLayerOptions layerOptions;
    writeLayer(layerOptions, usd, layer, debugTag);
    return true;
}
```

Export example:
```
bool UsdObjFileFormat::WriteToFile(
    const SdfLayer& layer,
    const std::string& filename,
    const std::string& comment,
    const FileFormatArguments& args
) const {
    Obj obj;
    UsdData usd;
    ReadLayerOptions layerOptions;
    readLayer(layerOptions, layer, usd, debugTag);
    ExportOptions options;
    exportObj(options, usd, obj);
    writeObj(obj, filename, false);
    return true;
}
```
