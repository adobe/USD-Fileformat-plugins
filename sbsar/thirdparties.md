# Dependencies on thirdparties

## Usd 
The plugin depends on the USD ([https://github.com/PixarAnimationStudios/USD]) 
The bulk of USD is provided with the application it's compiled for but parts of the USD code
base will be in the compiled plugin when distributing it.
USD itself depends on a number of external code bases, the only ones required to build the plugin is:
* tbb [https://github.com/oneapi-src/oneTBB]
* boost [https://github.com/boostorg/boost]
* OpenSubdiv [https://github.com/PixarAnimationStudios/OpenSubdiv]
For more details about third party dependencies in USD, refer to the USD github
The user is expected to provide its own location for their USD build using the cmake parameter **USDSBSAR_USD_BUILD_DIR**

## Substance Engine SDK
The plugin depends on the Adobe internal technology Substance Engine SDK
The user is expected to provide its own location for their USD build using the cmake parameter **substance_DIR**

## Libpng and zlib
The plugin optionally depends on libpng ([https://github.com/glennrp/libpng]) and zlib ([https://github.com/madler/zlib])
In some cases it is already present when buiding but it can be statically linked into the preferred
in case it's desirable using the **BUILD_PNG** flag to cmake when compiling.

Libpng and zlib are referenced as git submodules

## Gtest
The plugin implements its testing using gtest ([https://git.corp.adobe.com:substance-integrations/thirdparty-gtest.git])
The tests are external to the plugin and are not needed in order to run the binary. The binary itself doesn't use gtest.

gtest is referenced as git submodules

