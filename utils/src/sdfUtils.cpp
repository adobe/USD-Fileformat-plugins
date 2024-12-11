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
#include <fileformatutils/sdfUtils.h>

#include <fileformatutils/common.h>
#include <fileformatutils/debugCodes.h>

#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/tokens.h> // for UsdTokens->apiSchemas

#include <cassert>

PXR_NAMESPACE_USING_DIRECTIVE

namespace { // anonymous namespace

template<typename T>
void
_appendChild(SdfAbstractData* data,
             const SdfPath& specPath,
             const TfToken& childKey,
             const T& child)
{
    std::vector<T> children;
    // Retrieve the existing children first, if they existed
    SdfAbstractDataTypedValue getter(&children);
    (void)data->Has(specPath, childKey, &getter);
    children.push_back(child);
    data->Set(specPath, childKey, SdfAbstractDataConstTypedValue(&children));
}

template<typename T>
void
_appendListOp(SdfAbstractData* data, const SdfPath& specPath, const TfToken& field, const T& item)
{
    SdfListOp<T> listOp;
    // Retrieve the existing listOp first, if it existed
    SdfAbstractDataTypedValue getter(&listOp);
    (void)data->Has(specPath, field, &getter);
    typename SdfListOp<T>::ItemVector explicitItems = listOp.GetExplicitItems();
    explicitItems.push_back(item);
    listOp.SetExplicitItems(explicitItems);
    data->Set(specPath, field, SdfAbstractDataConstTypedValue(&listOp));
}

template<typename T>
void
_prependListOp(SdfAbstractData* data, const SdfPath& specPath, const TfToken& field, const T& item)
{
    SdfListOp<T> listOp;
    // Retrieve the existing listOp first, if it existed
    SdfAbstractDataTypedValue getter(&listOp);
    (void)data->Has(specPath, field, &getter);
    typename SdfListOp<T>::ItemVector prependedItems = listOp.GetPrependedItems();
    prependedItems.insert(prependedItems.begin(), item);
    listOp.SetPrependedItems(prependedItems);
    data->Set(specPath, field, SdfAbstractDataConstTypedValue(&listOp));
}

} // end anonymous namespace

namespace adobe::usd {

void
createPseudoRootSpec(SdfAbstractData* data)
{
    data->CreateSpec(SdfPath::AbsoluteRootPath(), SdfSpecTypePseudoRoot);
}

void
setLayerMetadata(SdfAbstractData* data, const TfToken& key, const VtValue& value)
{
    // layer metadata is just fields on the pseudo root spec
    data->Set(SdfPath::AbsoluteRootPath(), key, value);
}

SdfPath
createPrimSpec(SdfAbstractData* data,
               const SdfPath& parentPrimPath,
               const TfToken& primName,
               const TfToken& primType,
               SdfSpecifier specifier,
               bool append)
{
    assert(parentPrimPath.IsAbsoluteRootPath() ||
           parentPrimPath.IsPrimOrPrimVariantSelectionPath());
    SdfPath primPath = parentPrimPath.AppendChild(primName);
    data->CreateSpec(primPath, SdfSpecTypePrim);
    data->Set(primPath, SdfFieldKeys->Specifier, SdfAbstractDataConstTypedValue(&specifier));
    if (!primType.IsEmpty()) {
        data->Set(primPath, SdfFieldKeys->TypeName, SdfAbstractDataConstTypedValue(&primType));
    }

    if (append)
        _appendChild(data, parentPrimPath, SdfChildrenKeys->PrimChildren, primName);

    return primPath;
}

// An optimization to add the names of multiple children at once
void
appendToChildList(SdfAbstractData* data,
                  const SdfPath& parentPrimPath,
                  const std::vector<TfToken>& children)
{
    if (children.size() > 0) {
        std::vector<TfToken> currentChildren;
        // Retrieve the existing children first, if they existed
        SdfAbstractDataTypedValue getter(&currentChildren);
        (void)data->Has(parentPrimPath, SdfChildrenKeys->PrimChildren, &getter);
        if (currentChildren.empty()) {
            data->Set(parentPrimPath,
                      SdfChildrenKeys->PrimChildren,
                      SdfAbstractDataConstTypedValue(&children));
        } else {
            currentChildren.reserve(currentChildren.size() + children.size());
            currentChildren.insert(currentChildren.end(), children.begin(), children.end());
            data->Set(parentPrimPath,
                      SdfChildrenKeys->PrimChildren,
                      SdfAbstractDataConstTypedValue(&currentChildren));
        }
    }
}

void
setPrimMetadata(SdfAbstractData* data,
                const SdfPath& primPath,
                const TfToken& key,
                const VtValue& value)
{
    assert(primPath.IsPrimPath());
    // prim metadata is just fields on the prim spec
    data->Set(primPath, key, value);
}

void
addPrimReference(SdfAbstractData* data, const SdfPath& primPath, const SdfReference& reference)
{
    assert(primPath.IsPrimOrPrimVariantSelectionPath());
    _prependListOp(data, primPath, SdfFieldKeys->References, reference);
}

void
addPrimInherit(SdfAbstractData* data, const SdfPath& primPath, const SdfPath& inheritPath)
{
    assert(primPath.IsPrimOrPrimVariantSelectionPath());
    _prependListOp(data, primPath, SdfFieldKeys->InheritPaths, inheritPath);
}

void
addPrimPayload(SdfAbstractData* data, const SdfPath& primPath, const SdfPayload& payload)
{
    // Note, we do create payload arcs on variant selection specs
    assert(primPath.IsPrimOrPrimVariantSelectionPath());
    _prependListOp(data, primPath, SdfFieldKeys->Payload, payload);
}

void
prependApiSchema(SdfAbstractData* data, const SdfPath& primPath, const TfToken& apiName)
{
    assert(primPath.IsPrimPath());
    SdfListOp<TfToken> listOp;
    // Retrieve the existing listOp first, if it existed
    SdfAbstractDataTypedValue getter(&listOp);
    (void)data->Has(primPath, UsdTokens->apiSchemas, &getter);
    typename SdfListOp<TfToken>::ItemVector prependedItems = listOp.GetPrependedItems();
    prependedItems.push_back(apiName);
    listOp.SetPrependedItems(prependedItems);
    data->Set(primPath, UsdTokens->apiSchemas, SdfAbstractDataConstTypedValue(&listOp));
}

SdfPath
createAttributeSpec(SdfAbstractData* data,
                    const SdfPath& primPath,
                    const TfToken& attrName,
                    const SdfValueTypeName& typeName,
                    SdfVariability variability)
{
    assert(primPath.IsPrimOrPrimVariantSelectionPath());
    SdfPath propertyPath = primPath.AppendProperty(attrName);
    data->CreateSpec(propertyPath, SdfSpecTypeAttribute);

    TfToken typeNameToken = typeName.GetAsToken();
    data->Set(propertyPath, SdfFieldKeys->TypeName, SdfAbstractDataConstTypedValue(&typeNameToken));
    if (variability != SdfVariabilityVarying) {
        data->Set(
          propertyPath, SdfFieldKeys->Variability, SdfAbstractDataConstTypedValue(&variability));
    }

    _appendChild(data, primPath, SdfChildrenKeys->PropertyChildren, attrName);

    return propertyPath;
}

void
setAttributeMetadata(SdfAbstractData* data,
                     const SdfPath& propertyPath,
                     const TfToken& key,
                     const VtValue& value)
{
    assert(propertyPath.IsPropertyPath());
    // attribute metadata is just fields on the attribute spec
    data->Set(propertyPath, key, value);
}

void
setAttributeDefaultValue(SdfAbstractData* data, const SdfPath& propertyPath, const VtValue& value)
{
    assert(propertyPath.IsPropertyPath());
    data->Set(propertyPath, SdfFieldKeys->Default, value);
}

void
setAttributeDefaultValue(SdfAbstractData* data,
                         const SdfPath& propertyPath,
                         const SdfAbstractDataConstValue& value)
{
    assert(propertyPath.IsPropertyPath());
    data->Set(propertyPath, SdfFieldKeys->Default, value);
}

void
setAttributeTimeSampledValues(SdfAbstractData* data,
                              const SdfPath& propertyPath,
                              const SdfTimeSampleMap& timeSamples)
{
    assert(propertyPath.IsPropertyPath());
    data->Set(
      propertyPath, SdfFieldKeys->TimeSamples, SdfAbstractDataConstTypedValue(&timeSamples));
}

void
appendAttributeConnection(SdfAbstractData* data, const SdfPath& attrPath, const SdfPath& targetPath)
{
    assert(attrPath.IsPropertyPath());
    _appendChild(data, attrPath, SdfChildrenKeys->ConnectionChildren, targetPath);
    _appendListOp(data, attrPath, SdfFieldKeys->ConnectionPaths, targetPath);

    SdfPath connectionPath = attrPath.AppendTarget(targetPath);
    data->CreateSpec(connectionPath, SdfSpecTypeConnection);
}

SdfPath
createRelationshipSpec(SdfAbstractData* data,
                       const SdfPath& primPath,
                       const TfToken& relName,
                       SdfVariability variability)
{
    assert(primPath.IsPrimPath());
    SdfPath propertyPath = primPath.AppendProperty(relName);
    data->CreateSpec(propertyPath, SdfSpecTypeRelationship);

    if (variability != SdfVariabilityVarying) {
        data->Set(
          propertyPath, SdfFieldKeys->Variability, SdfAbstractDataConstTypedValue(&variability));
    }

    _appendChild(data, primPath, SdfChildrenKeys->PropertyChildren, relName);

    return propertyPath;
}

void
appendRelationshipTarget(SdfAbstractData* data, const SdfPath& relPath, const SdfPath& targetPath)
{
    assert(relPath.IsPropertyPath());
    _appendChild(data, relPath, SdfChildrenKeys->RelationshipTargetChildren, targetPath);
    _appendListOp(data, relPath, SdfFieldKeys->TargetPaths, targetPath);

    SdfPath relTargetPath = relPath.AppendTarget(targetPath);
    data->CreateSpec(relTargetPath, SdfSpecTypeRelationshipTarget);
}

void
prependRelationshipTarget(SdfAbstractData* data, const SdfPath& relPath, const SdfPath& targetPath)
{
    assert(relPath.IsPropertyPath());
    _appendChild(data, relPath, SdfChildrenKeys->RelationshipTargetChildren, targetPath);
    _prependListOp(data, relPath, SdfFieldKeys->TargetPaths, targetPath);

    SdfPath relTargetPath = relPath.AppendTarget(targetPath);
    data->CreateSpec(relTargetPath, SdfSpecTypeRelationshipTarget);
}

SdfPath
createVariantSetSpec(SdfAbstractData* data, const SdfPath& parentPath, const TfToken& variantSet)
{
    assert(parentPath.IsPrimOrPrimVariantSelectionPath());
    SdfPath variantSetPath =
      parentPath.AppendVariantSelection(variantSet.GetString(), std::string());
    data->CreateSpec(variantSetPath, SdfSpecTypeVariantSet);

    _appendChild(data, parentPath, SdfChildrenKeys->VariantSetChildren, variantSet);
    _prependListOp(data, parentPath, SdfFieldKeys->VariantSetNames, variantSet.GetString());

    return variantSetPath;
}

SdfPath
createVariantSpec(SdfAbstractData* data, const SdfPath& variantSetPath, const TfToken& variant)
{
    assert(variantSetPath.IsPrimVariantSelectionPath());
    const auto& [variantSet, selection] = variantSetPath.GetVariantSelection();
    SdfPath variantPath =
      variantSetPath.GetParentPath().AppendVariantSelection(variantSet, variant.GetString());
    data->CreateSpec(variantPath, SdfSpecTypeVariant);

    _appendChild(data, variantSetPath, SdfChildrenKeys->VariantChildren, variant);

    return variantPath;
}

void
addVariantSelection(SdfAbstractData* data,
                    const SdfPath& parentPath,
                    const TfToken& variantSet,
                    const TfToken& variant)
{
    assert(parentPath.IsPrimOrPrimVariantSelectionPath());

    std::map<std::string, std::string> selections;
    SdfAbstractDataTypedValue getter(&selections);
    (void)data->Has(parentPath, SdfFieldKeys->VariantSelection, &getter);
    selections[variantSet.GetString()] = variant.GetString();
    data->Set(
      parentPath, SdfFieldKeys->VariantSelection, SdfAbstractDataConstTypedValue(&selections));
}

}