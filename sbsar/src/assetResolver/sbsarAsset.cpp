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

#include <assetResolver/sbsarAsset.h>

#include <pxr/base/tf/diagnostic.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace adobe::usd::sbsar {

SbsarAsset::SbsarAsset(const std::string& packagePath, const std::string& packagedPathNoExt)
  : mPackagePath(packagePath)
  , mPackagedPathNoExt(packagedPathNoExt)
{}

size_t
SbsarAsset::GetSize() const
{
    TF_CODING_ERROR("SbsarAsset::GetSize not implemented");
    return 0;
}

std::shared_ptr<const char>
SbsarAsset::GetBuffer() const
{
    TF_CODING_ERROR("SbsarAsset::GetBuffer not implemented");
    return nullptr;
}

std::pair<FILE*, size_t>
SbsarAsset::GetFileUnsafe() const
{
    TF_CODING_ERROR("SbsarAsset::GetFileUnsafe not implemented");
    return { nullptr, 0 };
}

size_t
SbsarAsset::Read(void* buffer, size_t count, size_t offset) const
{
    TF_CODING_ERROR("SbsarAsset::Read not implemented");
    return 0;
}

}
