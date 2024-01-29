import shutil
import os
import argparse

def copy_directory_contents(src, dst):
    if not os.path.exists(dst):
        os.makedirs(dst)
    for item in os.listdir(src):
        s = os.path.join(src, item)
        d = os.path.join(dst, item)
        if os.path.isdir(s):
            shutil.copytree(s, d, dirs_exist_ok=True)  # For Python 3.8 and above
        else:
            shutil.copy2(s, d)

def main(src, dest):
    copy_directory_contents(src, dest)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Copy directories for Jenkins job.')
    parser.add_argument('src', type=str, help='The source directory path')
    parser.add_argument('dest', type=str, help='The destination directory path')
    args = parser.parse_args()

    main(args.src, args.dest)
