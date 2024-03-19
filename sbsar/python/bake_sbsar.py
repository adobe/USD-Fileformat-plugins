# Copyright 2024 Adobe. All rights reserved.
# This file is licensed to you under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License. You may obtain a copy
# of the License at http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software distributed under
# the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
# OF ANY KIND, either express or implied. See the License for the specific language
# governing permissions and limitations under the License.


from pxr import Usd, UsdGeom, Sdf, Ar, Tf
from pxr import Plug

import sys
import os.path
import ctypes
import os
import argparse
import logging
import usdSbsar

logging.basicConfig(level=logging.INFO)


def bake_sbsar(src_uri, dest_uri, resolution, compression_level):
    res = usdSbsar.bakeSbsar(src_uri, dest_uri,
                             resolution, compression_level)
    print('Result: {}'.format(res))


def main():
    argparser = argparse.ArgumentParser(
        description='Bake sbsar to usd data')
    argparser.add_argument('src_uri',
                           help='The path to open',
                           type=str)
    argparser.add_argument('dest_uri',
                           help='The path to open',
                           type=str)
    argparser.add_argument('-r', '--resolution',
                           help='Resolution to render in pow 2 format',
                           default=9,
                           type=int,
                           required=False)
    argparser.add_argument('-c', '--compression',
                           help='Compression level (between 0 and 9)',
                           default=7,
                           type=int,
                           required=False)
    args = argparser.parse_args(sys.argv[1:])
    resolution = usdSbsar.SbsarBakeResolution.values[args.resolution]
    bake_sbsar(args.src_uri, args.dest_uri, resolution, args.compression)


if __name__ == '__main__':
    main()
