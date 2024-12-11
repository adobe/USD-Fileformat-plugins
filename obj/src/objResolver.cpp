/*
Copyright 2023 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#include "objResolver.h"
#include "obj.h"
#include "objImport.h"
#include <pxr/usd/ar/definePackageResolver.h>
#include <fileformatutils/resolver.h>

using namespace PXR_NS;
namespace adobe::usd {

AR_DEFINE_PACKAGE_RESOLVER(ObjResolver, ArPackageResolver);

ObjResolver::ObjResolver()
  : Resolver("ObjResolver")
{
}

void
ObjResolver::readCache(const std::string& filename, std::vector<ImageAsset>& images)
{
    Obj obj;
    readObj(obj, filename, true);
    UsdData usd;
    ImportObjOptions importOptions;
    importOptions.importGeometry = false;
    importOptions.importMaterials = true;
    importOptions.importImages = true;
    importOptions.importPhong = false; // TODO pass this option from user or change resolver
    importObj(importOptions, obj, usd);
    images = std::move(usd.images);
}

}