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
#include <memory>
#include <pxr/usd/ar/asset.h>
#include <sbsarEngine/sbsarPackageCache.h>
#include <string>

namespace adobe::usd::sbsar {

//! Asset representing the parameters to render a sbsar texture.
class USDSBSAR_API SbsarAsset final : public PXR_NS::ArAsset
{
public:
    explicit SbsarAsset(const std::string& packagePath, const std::string& packagedPathNoExt);

    const std::string& GetPackagePath() const { return mPackagePath; }
    const std::string& GetPackagedPathNoExt() const { return mPackagedPathNoExt; }

    size_t GetSize() const override;
    std::shared_ptr<const char> GetBuffer() const override;
    size_t Read(void* buffer, size_t count, size_t offset) const override;
    std::pair<FILE*, size_t> GetFileUnsafe() const override;

private:
    std::string mPackagePath;
    std::string mPackagedPathNoExt;
};
}
