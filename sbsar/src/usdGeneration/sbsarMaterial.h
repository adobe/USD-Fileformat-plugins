/*
Copyright 2024 Adobe. All rights reserved.
This file is licensed to you under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License. You may obtain a copy
of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under
the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
OF ANY KIND, either express or implied. See the License for the specific language
governing permissions and limitations under the License.
*/
#pragma once

#include "sbsarSymbolMapper.h"
#include <sbsarfileformat.h>

#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>

#include <substance/framework/framework.h>

#include <string>

namespace adobe::usd::sbsar {

//! \brief Add usd material primitive to the given Sdf layer.
//! Depending on the sbsarData.depth, the content of the prim is different.
//! Depth = 0 -> Only variant: Preset and resolution
//! Depth = 1 -> All default parameters of the current graph
//! Depth = 2 -> Asset path and standard material (Depending to the compilation
//! parameter)
//! This system will generate several layers (by depth). This is useful for two reasons:
//! 1 - Control priority of parameters, in the order of: User -> Variant -> default parameters
//! 2 - The Layer 1 and 2 are split because the plugin needs to compose all default
//! parameters. (in SBSARFileFormat::ComposeFieldsForFileFormatArguments) to catch all
//! updates and regenerate asset path.
//! \param sdfData          Sdf data to store the layer in.
//! \param graphName        Name of the current sbsar graph.
//! \param graphDesc        Description of the current sbsar graph.
//! \param packagePath      Path of the sbsar file.
//! \param classPath        Path of the class material.
//! \param sbsarHash        Hash of the sbsar.
//! \param symbolMapper     Symbole mapper to avoid conflict between parameters.
//! \param sbsarData        Options for the sbsar. See SBSAROptions.
//! \return The path of the created prim.
PXR_NS::SdfPath
addMaterialPrim(PXR_NS::SdfAbstractData* sdfData,
                const MappedSymbol& graphName,
                const SubstanceAir::GraphDesc& graphDesc,
                const std::string& packagePath,
                const PXR_NS::SdfPath& classPath,
                size_t sbsarHash,
                SymbolMapper& symbolMapper,
                const SBSAROptions& sbsarData);

//! \brief Add a class prim to the given Sdf layer.
//! The class prim is a global prim with a "class" specifier. It contains
//! attributes that are set once and inherited by all material prims.
//! \param sdfData          Sdf data to store the layer in.
//! \param className        Name of the class.
//! \param classType        Type of the class.
PXR_NS::SdfPath
addClassPrim(PXR_NS::SdfAbstractData* sdfData,
             const PXR_NS::TfToken& className,
             const PXR_NS::TfToken& classType = PXR_NS::TfToken());

} // namespace adobe::usd::sbsar
