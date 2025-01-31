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
#pragma once

#include "api.h"

#include <pxr/pxr.h>
#include <pxr/usd/sdf/abstractData.h>
#include <pxr/usd/sdf/data.h>
#include <pxr/usd/sdf/fileFormat.h>

namespace adobe::usd {

/// \ingroup utils_layer
/// \file
/// These helper functions mimic the core Usd API concepts of prims, attributes, connections and
/// relationships variant sets and variants to author directly to the low level SdfAbstractData.
///
/// With these functions it is possible to author anything that could be authored via the Usd APIs,
/// but this way of authoring data only applies in the context of a file format plugin, where a
/// layer is completely generated as a translation from another format. No change notifications are
/// issued when directly manipulating the SdfAbstractData.
///
/// These low level APIs provide no protections against programming errors and do not check for
/// consistency, like many of the higher level Usd APIs do. There is also no schema support, which
/// means for each attribute that is authored, the correct name and type needs to be known and
/// the default value needs to be of matching type.
///
/// The upside of this API is that it is wicked fast and is not affected by SdfChangeBlocks, which
/// can interfer with the reloading of layers loaded via a file format plugin, when using Usd APIs.

// -------------------------------------------------------------------------------------------------
// Layer specs
// -------------------------------------------------------------------------------------------------

/// \ingroup utils_layer
/// Creates the pseudo spec in the SdfAbstractData, which is the root of all other specs
USDFFUTILS_API void
createPseudoRootSpec(PXR_NS::SdfAbstractData* data);

/// \ingroup utils_layer
/// Set metadata on the later
///
/// This has to be called after the creation pseudo root spec, since metadata is stored as fields
/// on the pseudo root
USDFFUTILS_API void
setLayerMetadata(PXR_NS::SdfAbstractData* data,
                 const PXR_NS::TfToken& key,
                 const PXR_NS::VtValue& value);

// -------------------------------------------------------------------------------------------------
// Prim specs
// -------------------------------------------------------------------------------------------------

/// \ingroup utils_layer
/// Create a prim spec
///
/// This will create a prim spec for a new prim named primName under the parent prim spec at path
/// parentPrimPath. The parent spec needs to have been created first.
/// The new prim will be added to the list of children of the parent prim spec only if the append
/// flag is true.
/// Return the path to the new prim spec.
///
/// A prim doesn't need to have a primType, but many have a type like "Xform", "Scope", etc.
/// By default we create a "def" spec, but this could also be an "over" or a "class".
///
/// Warning: calling this multiple times for the same prim will create the spec once, but will add
/// it multiple times to the parent's children list
USDFFUTILS_API PXR_NS::SdfPath
createPrimSpec(PXR_NS::SdfAbstractData* data,
               const PXR_NS::SdfPath& parentPrimPath,
               const PXR_NS::TfToken& primName,
               const PXR_NS::TfToken& primType = PXR_NS::TfToken(),
               PXR_NS::SdfSpecifier specifier = PXR_NS::SdfSpecifier::SdfSpecifierDef,
               bool append = true);

/// \ingroup utils_layer
/// Adds the names of the children to the prim identified by the parentPrimPath
USDFFUTILS_API void
appendToChildList(PXR_NS::SdfAbstractData* data,
                  const PXR_NS::SdfPath& parentPrimPath,
                  const std::vector<PXR_NS::TfToken>& children);

/// \ingroup utils_layer
/// Set metadata on a prim spec
///
/// This can be used to set the active flag and other metadata fields
USDFFUTILS_API void
setPrimMetadata(PXR_NS::SdfAbstractData* data,
                const PXR_NS::SdfPath& primPath,
                const PXR_NS::TfToken& key,
                const PXR_NS::VtValue& value);

/// \ingroup utils_layer
/// Add reference to a prim spec
///
/// Note, the new reference will be added to the prepend references list
USDFFUTILS_API void
addPrimReference(PXR_NS::SdfAbstractData* data,
                 const PXR_NS::SdfPath& primPath,
                 const PXR_NS::SdfReference& reference);

/// \ingroup utils_layer
/// Add inherit to a prim spec
///
/// Note, the new inherit will be added to the prepend inherits list
USDFFUTILS_API void
addPrimInherit(PXR_NS::SdfAbstractData* data,
               const PXR_NS::SdfPath& primPath,
               const PXR_NS::SdfPath& inheritPath);

/// \ingroup utils_layer
/// Add payload to a prim spec
///
/// Note, the new payload will be added to the prepend payloads list
USDFFUTILS_API void
addPrimPayload(PXR_NS::SdfAbstractData* data,
               const PXR_NS::SdfPath& primPath,
               const PXR_NS::SdfPayload& payload);

/// \ingroup utils_layer
/// Prepend an API schema to a prim spec
///
/// This is used to add "MaterialBindingAPI", "SkelBindingAPI", etc. to a prim spec
USDFFUTILS_API void
prependApiSchema(PXR_NS::SdfAbstractData* data,
                 const PXR_NS::SdfPath& primPath,
                 const PXR_NS::TfToken& apiName);

// -------------------------------------------------------------------------------------------------
// Attribute specs
// -------------------------------------------------------------------------------------------------

/// \ingroup utils_layer
/// Create an attribute spec
///
/// This will create a new attribute spec under a prim spec. The prim spec needs to have been
/// created before. The new attribute will be added to the list of properties on the prim.
/// Returns the path to the new attribute spec.
///
/// The variability of the attribute defaults to "varying", which means animateable over time. Some
/// attributes need to be marked as "uniform" if they can't change over time.
///
/// Warning: calling this multiple times for the same attribute will create the spec once, but will
/// add it multiple times to the prim's property list
USDFFUTILS_API PXR_NS::SdfPath
createAttributeSpec(PXR_NS::SdfAbstractData* data,
                    const PXR_NS::SdfPath& primPath,
                    const PXR_NS::TfToken& attrName,
                    const PXR_NS::SdfValueTypeName& typeName,
                    PXR_NS::SdfVariability variability = PXR_NS::SdfVariabilityVarying);

/// \ingroup utils_layer
/// Set metadata on an attribute spec
///
/// This is used, among other things, to set the interpolation mode or elementSize for primvars.
USDFFUTILS_API void
setAttributeMetadata(PXR_NS::SdfAbstractData* data,
                     const PXR_NS::SdfPath& propertyPath,
                     const PXR_NS::TfToken& key,
                     const PXR_NS::VtValue& value);

/// \ingroup utils_layer
/// Set the default value of an attribute
USDFFUTILS_API void
setAttributeDefaultValue(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& propertyPath,
                         const PXR_NS::VtValue& value);

/// \ingroup utils_layer
/// Set the default value of an attribute
USDFFUTILS_API void
setAttributeDefaultValue(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& propertyPath,
                         const PXR_NS::SdfAbstractDataConstValue& value);

/// \ingroup utils_layer
/// Set the default value of an attribute
template<typename T>
void
setAttributeDefaultValue(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& propertyPath,
                         const T& value)
{
    const PXR_NS::SdfAbstractDataConstTypedValue<T> inValue(&value);
    const PXR_NS::SdfAbstractDataConstValue& untypedInValue = inValue;
    setAttributeDefaultValue(data, propertyPath, untypedInValue);
}

/// Set the time sampled values for an animated attribute
///
/// This takes a SdfTimeSampleMap, which completely describes the times and associated values of
/// an animated attribute.
///
/// Note that individual values can also be set via data->SetTimeSample(path, time, value)
USDFFUTILS_API void
setAttributeTimeSampledValues(PXR_NS::SdfAbstractData* data,
                              const PXR_NS::SdfPath& propertyPath,
                              const PXR_NS::SdfTimeSampleMap& timeSamples);

// -------------------------------------------------------------------------------------------------
// Connections spec
// -------------------------------------------------------------------------------------------------

/// \ingroup utils_layer
/// Append a connection to an attribute
///
/// Creates a connection spec under the attribute spec and appends it to the list of connections.
/// This is primarily used to describe connections in shading networks
USDFFUTILS_API void
appendAttributeConnection(PXR_NS::SdfAbstractData* data,
                          const PXR_NS::SdfPath& attrPath,
                          const PXR_NS::SdfPath& targetPath);

// -------------------------------------------------------------------------------------------------
// Relationship spec
// -------------------------------------------------------------------------------------------------

/// \ingroup utils_layer
/// Create an relationship spec
///
/// This will create a new relationship spec on the specified prim spec. The prim spec needs to have
/// been created before. The new relationship is added to the list of properties.
/// Returns the path to the new relationship spec.
/// The default variability for relationships is "uniform".
///
/// Warning: calling this multiple times for the same relationship will create the spec once, but
/// will add it multiple times to the prim's property list
USDFFUTILS_API PXR_NS::SdfPath
createRelationshipSpec(PXR_NS::SdfAbstractData* data,
                       const PXR_NS::SdfPath& primPath,
                       const PXR_NS::TfToken& relName,
                       PXR_NS::SdfVariability variability = PXR_NS::SdfVariabilityUniform);

/// \ingroup utils_layer
/// Append a target to a relationship
///
/// This will create a relationship target spec under the relationship spec and append it to the
/// list of targets.
USDFFUTILS_API void
appendRelationshipTarget(PXR_NS::SdfAbstractData* data,
                         const PXR_NS::SdfPath& relPath,
                         const PXR_NS::SdfPath& targetPath);

/// \ingroup utils_layer
/// Prepend a target to a relationship
///
/// This will create a relationship target spec under the relationship spec and prepend it to the
/// list of targets.
// XXX TODO Can we create a single function with an enum to handle adding, appending, prepending
// to the targets?
USDFFUTILS_API void
prependRelationshipTarget(PXR_NS::SdfAbstractData* data,
                          const PXR_NS::SdfPath& relPath,
                          const PXR_NS::SdfPath& targetPath);

// -------------------------------------------------------------------------------------------------
// VariantSet and Variant spec
// -------------------------------------------------------------------------------------------------

/// \ingroup utils_layer
/// Create a variant set spec
///
/// A variant set spec is the parent of variant specs. They can be created under prim specs and
/// other variant specs.
///
/// Warning: calling this multiple times for the same variant set will create the spec once, but
/// will add it multiple times to the parent variant set list
USDFFUTILS_API PXR_NS::SdfPath
createVariantSetSpec(PXR_NS::SdfAbstractData* data,
                     const PXR_NS::SdfPath& parentPath,
                     const PXR_NS::TfToken& variantSet);

/// \ingroup utils_layer
/// Create a variant spec
///
/// A variant spec is the parent of the actual prims and attributes that change when the variant is
/// active. They can be only be created under a variant set spec.
///
/// Warning: calling this multiple times for the same variant will create the spec once, but will
/// add it multiple times to the parent variant set
USDFFUTILS_API PXR_NS::SdfPath
createVariantSpec(PXR_NS::SdfAbstractData* data,
                  const PXR_NS::SdfPath& variantSetPath,
                  const PXR_NS::TfToken& variant);

/// \ingroup utils_layer
/// Add a variant selection to a prim or variant
///
/// The selection is usually added to the spec that is the parent of the variant set for which the
/// choice is made.
USDFFUTILS_API void
addVariantSelection(PXR_NS::SdfAbstractData* data,
                    const PXR_NS::SdfPath& parentPath,
                    const PXR_NS::TfToken& variantSet,
                    const PXR_NS::TfToken& variant);
}

PXR_NAMESPACE_OPEN_SCOPE

/// \ingroup utils_layer
/// \brief SdfData specialization.
class FileFormatDataBase : public SdfData
{
  public:
    bool writeMaterialX = false;
};

PXR_NAMESPACE_CLOSE_SCOPE
