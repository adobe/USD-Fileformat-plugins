import argparse
import cv2
import json
import logging
import numpy as np
import os
import platform
import pytest
import subprocess
import sys
import zipfile
from pathlib import Path
from pxr import Usd

DIRECTORY = os.path.dirname(os.path.abspath(__file__))
ASSET_PATH = os.path.join(DIRECTORY, "assets")

# Initialize RENDER_COMMAND based on USDRECORD_ROOT environment variable if its set
if os.getenv('USDRECORD_ROOT'):
    RENDER_COMMAND = os.path.join(os.getenv('USDRECORD_ROOT'), 'usdrecord')
else:
    RENDER_COMMAND = 'usdrecord'

RENDER_OUTPUT_FORMAT = ".jpg"
BASELINE_FOLDERNAME = "baseline"
OUTPUT_FOLDERNAME = "output"
CONVERTED_SUFFIX = "_roundtrip"

def compare_images_with_similarity_threshold(img1_path, img2_path, similarity_threshold=0.95):
    """
    Compare two images and check if they are similar based on a similarity threshold.

    Parameters:
        img1_path (str): File path to the first image.
        img2_path (str): File path to the second image.
        similarity_threshold (float): The threshold for similarity ratio (0 to 1).

    Returns:
        bool: True if images are similar above the given threshold, False otherwise.
    """
    img1 = cv2.imread(img1_path)
    img2 = cv2.imread(img2_path)

    # Check if images are loaded
    if img1 is None or img2 is None:
        raise ValueError("One or both images could not be loaded.")

    # Check if images have the same dimensions
    if img1.shape != img2.shape:
        return False

    # Calculate the absolute difference and then the binary difference
    difference = cv2.absdiff(img1, img2)
    _, binary_difference = cv2.threshold(difference, 0, 255, cv2.THRESH_BINARY)

    # Calculate the percentage of similar pixels
    similar_pixels = np.count_nonzero(binary_difference == 0)
    total_pixels = img1.shape[0] * img1.shape[1] * img1.shape[2]
    similarity_ratio = similar_pixels / total_pixels
    if similarity_ratio < similarity_threshold:
        logging.error(f"Images are not similar enough: {similarity_ratio}")
    return similarity_ratio >= similarity_threshold


def render(file, outputfile):
    """
    Render a file using usdrecord.
    Parameters:
        file (str): File path to the asset to be rendered.
        outputfile (str): File path where the rendered output should be saved.
    """
    full_command = f'{RENDER_COMMAND} "{file}" "{outputfile}"'
    logging.info("Rendering: " + file + " To: " + outputfile)
    os.system(full_command)


def run_usdchecker(file, results_file):
    """
    run the usdchecdker on the file and save the results
    Parameters:
        file (str): File path to the asset to be checked.
        results_file (str): File path where the results should be saved.
    """
    full_command = f'usdchecker "{file}"'
    process = subprocess.Popen(full_command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate()

    # Decode the output and error streams
    stdout_str = stdout.decode("utf-8")
    stderr_str = stderr.decode("utf-8")
    process.terminate()

    # Find the position to start parsing from, prioritizing "Total time"
    total_time_index = stdout_str.find("Total time:")
    if total_time_index != -1:
        newline_after_total_time = stdout_str.find("\n", total_time_index)
        if newline_after_total_time != -1:
            stdout_str = stdout_str[newline_after_total_time + 1:]
    else:  # Only look for "Write layer via Sdf API:" if "Total time" is not found
        write_layer_index = stdout_str.find("Write layer via Sdf API:")
        if write_layer_index != -1:
            newline_after_write_layer = stdout_str.find("\n", write_layer_index)
            if newline_after_write_layer != -1:
                stdout_str = stdout_str[newline_after_write_layer + 1:]

    # List of ignorable errors
    ignorable_errors = [
        "Stage does not specify an upAxis.",
        "Stage does not specify its linear scale in metersPerUnit."
    ]
    filter_out_strings = ["", "Success!\r", "Failed!\r", "Success!", "Failed!"]

    # Combine stdout and stderr, split by newline
    errors = [err for err in (stdout_str + stderr_str).split("\n") if err not in filter_out_strings]

    # Filter out ignorable errors
    non_ignorable_errors = [err for err in errors if all(ignorable not in err for ignorable in ignorable_errors)]

    # Extract relevant parts of the filename
    path = Path(file)
    filename_parts = [path.name]
    for _ in range(2):
        path = path.parent
        if path.name:
            filename_parts.insert(0, path.name)
    truncated_filename = "/".join(filename_parts)
    result = {
        "filename": truncated_filename,
        "pass": all(err == "Success!\r" or err == "" for err in non_ignorable_errors),
        "warnings": [err for err in errors if err not in non_ignorable_errors and err != ""],
        "errors": non_ignorable_errors
    }

    # Read existing results, append new result, and write back
    try:
        with open(results_file, "r") as f:
            existing_results = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        existing_results = []
    existing_results.append(result)
    with open(results_file, "w") as f:
        json.dump(existing_results, f, indent=4)
    return result


def convert(input_path, converted_path):
    """
    Convert an input file to the conerted_path format.
    Parameters:
        input_path (str): Input file path.
        converted_path (str): Output file path.
    Returns:
        bool: True if conversion was successful, False otherwise.
    """
    stage = Usd.Stage.Open(input_path)
    if stage:
        stage.GetRootLayer().Export(converted_path)
        return True
    return False


def process_file(plugin_name, test_file, generate_baseline, test_type):
    """
    Process a specific file for rendering and comparison.
    Parameters:
        plugin_name (str): Plugin name to be processed.
        test_file (str): Specific asset file to process.
        generate_baseline (bool): Whether to generate baseline images.
        test_type (str): "basic" for basic rendering or "roundtrip" for round-trip conversion and rendering.
    Returns:
        str: Name of mismatching file or None.
    """
    input_folder = os.path.join(ASSET_PATH, plugin_name)
    baseline_folder = os.path.join(DIRECTORY, BASELINE_FOLDERNAME, platform.system(), plugin_name)
    if generate_baseline:
        output_folder = os.path.join(DIRECTORY, OUTPUT_FOLDERNAME, BASELINE_FOLDERNAME, platform.system(), plugin_name)
    else:
        output_folder = os.path.join(DIRECTORY, OUTPUT_FOLDERNAME, platform.system(), plugin_name)

    # Assume test_file is the full path
    relative_root = os.path.relpath(os.path.dirname(test_file), input_folder)
    output_path_folder = os.path.join(output_folder, relative_root)
    
    os.makedirs(output_path_folder, exist_ok=True)
    output_path = os.path.join(output_path_folder, os.path.splitext(os.path.basename(test_file))[0] + RENDER_OUTPUT_FORMAT)
    
    if test_type == "basic":
        render(test_file, output_path)
        if not generate_baseline:
            baseline_path = os.path.join(baseline_folder, relative_root, os.path.splitext(os.path.basename(test_file))[0] + RENDER_OUTPUT_FORMAT)
            if not compare_images_with_similarity_threshold(baseline_path, output_path):
                return f"Error with basic converted file: {test_file}"
    elif test_type == "roundtrip":
        if plugin_name == "sbsar":
            return "Skipped: SBSAR rountrip is not supported."
        else:
            file_name, file_extension = os.path.splitext(os.path.basename(test_file))
            converted_path = os.path.join(output_path_folder, f"{file_name}{CONVERTED_SUFFIX}{file_extension}")
            converted_output_path = os.path.join(output_path_folder, f"{file_name}{CONVERTED_SUFFIX}{RENDER_OUTPUT_FORMAT}")

            convert(test_file, converted_path)
            render(converted_path, converted_output_path)

            if not generate_baseline:
                converted_baseline_output_path = os.path.join(output_path_folder, f"{file_name}{CONVERTED_SUFFIX}{RENDER_OUTPUT_FORMAT}")
                if not compare_images_with_similarity_threshold(converted_baseline_output_path, converted_output_path):
                    return f"Error with roundtrip converted file: {test_file}"
    return f"Converted: {test_file}"


def generate_baseline_function(override_file_filter=None):
    """
    Function to generate baseline images using the process_file function.
    Parameters:
        override_file_filter (list): List of file extensions to process.
    """
    file_formats = {
        'fbx': ['.fbx'],
        'gltf': ['.gltf', '.glb'],
        'obj': ['.obj'],
        'ply': ['.ply'],
        'sbsar': ['.usd'],
        'stl': ['.stl']
    }

    for plugin_name, extensions in file_formats.items():
        if override_file_filter:
            extensions = override_file_filter
        input_folder = os.path.join(ASSET_PATH, plugin_name)
        results_file = os.path.join(DIRECTORY, BASELINE_FOLDERNAME, platform.system(), plugin_name, "usd_checker_results.json")
        if os.path.exists(results_file):
            os.remove(results_file)

        for root, _, files in os.walk(input_folder):
            for file in files:
                if any(file.endswith(ext) for ext in extensions):
                    full_file_path = os.path.join(root, file)
                    process_file(plugin_name, full_file_path, test_type="basic", generate_baseline=True)
                    process_file(plugin_name, full_file_path, test_type="roundtrip", generate_baseline=True)
                    if plugin_name != "sbsar":
                        run_usdchecker(full_file_path, results_file)


def cleanup(path, extensions):
    """
    Remove files in a directory and its subdirectories that don't match the given list of extensions.
    Parameters:
        path (str): The root directory to start the cleanup.
        extensions (list): List of file extensions to keep.
    """
    for root, _, files in os.walk(path):
        for file in files:
            if not any(file.endswith(ext) for ext in extensions):
                file_path = os.path.join(root, file)
                os.remove(file_path)


def pytest_generate_tests(metafunc):
    """
    Generate test functions dynamically based on existing asset files.
    """
    file_formats = {
        'fbx': ['.fbx'],
        'gltf': ['.gltf', '.glb'],
        'obj': ['.obj'],
        'ply': ['.ply'],
        'sbsar': ['.usd'],
        'stl': ['.stl']
    }

    config = metafunc.config
    file_filter = config.getoption("--extensions")
    tests_to_run = []
    for plugin_name, extensions in file_formats.items():
        if file_filter:
            extensions = [ext for ext in extensions if ext in file_filter]
        results_file = os.path.join(DIRECTORY, OUTPUT_FOLDERNAME, platform.system(), plugin_name, "usd_checker_results.json")
        if os.path.exists(results_file):
            os.remove(results_file)
        input_folder = os.path.join(ASSET_PATH, plugin_name)
        for root, _, files in os.walk(input_folder):
            for file in files:
                if any(file.endswith(ext) for ext in extensions):
                    full_file_path = os.path.join(root, file)
                    tests_to_run.append((plugin_name, full_file_path))

    if metafunc.function.__name__ == 'test_asset_rendering':
        metafunc.parametrize("plugin_name,filename", tests_to_run)
    if metafunc.function.__name__ == 'test_usd_checker':
        metafunc.parametrize("plugin_name,filename", tests_to_run)


@pytest.mark.fileformat_test
def test_asset_rendering(plugin_name, filename):
    """
    Test asset rendering with basic and round-trip methods.
    
    Parameters:
        plugin_name (str): The name of the plugin to be tested.
        filename (str): The specific asset file to test.
        
    Returns:
        None: Asserts that no mismatching files are found.
    """
    for test_type in ["basic", "roundtrip"]:
        ret = process_file(plugin_name, filename, False, test_type)
        assert not (ret and ret.startswith("Error")), f"File mismatch in {test_type} test for {filename}: {ret}"


@pytest.mark.fileformat_test
def test_usd_checker(plugin_name, filename):
    """
    Test if the asset passes the USD checker after conversion.
    
    Parameters:
        plugin_name (str): The name of the plugin to be tested.
        filename (str): The specific asset file to test.
        
    Returns:
        None: Executes USD checker on the converted file.
    """
    if plugin_name == "sbsar":
        logging.warning("Skipping USD Checker test for SBSAR files.")
        return

    input_folder = os.path.join(ASSET_PATH, plugin_name)
    input_path = os.path.join(input_folder, filename)
    output_folder = os.path.join(DIRECTORY, OUTPUT_FOLDERNAME, platform.system(), plugin_name)
    results_file = os.path.join(DIRECTORY, OUTPUT_FOLDERNAME, platform.system(), plugin_name, "usd_checker_results.json")
    baseline_file = os.path.join(DIRECTORY, BASELINE_FOLDERNAME, platform.system(), plugin_name, "usd_checker_results.json")
    os.makedirs(output_folder, exist_ok=True)
    converted_path = os.path.join(output_folder, os.path.splitext(filename)[0] + "_usdchecked.usd")

    if convert(input_path, converted_path):
        results = run_usdchecker(converted_path, results_file)

        # get the proper baseline to compare with
        try:
            with open(baseline_file, "r") as f:
                baseline_results = [json.loads(line.strip()) for line in f]
        except json.JSONDecodeError:
            logging.error("Invalid JSON in baseline file.")
            return
        baseline_result = next((res for res in baseline_results if res['filename'] == input_path), None)
        if baseline_result is None:
            logging.warning(f"No baseline found for {input_path}.")
            return

        # do the comparison
        current_warnings_count = len(results.get('warnings', []))
        baseline_warnings_count = len(baseline_result.get('warnings', []))
        if current_warnings_count != baseline_warnings_count:
            logging.info(f"Warning count changed: {baseline_warnings_count} -> {current_warnings_count}")
        current_error_count = len(results.get('errors', []))
        baseline_error_count = len(baseline_result.get('errors', []))
        if current_error_count > 0:
            logging.error(f"Errors still exist: {current_error_count}")
        assert current_error_count <= baseline_error_count, "USD Checker errors have increased"


def zip_files(directory, extensions, zip_file_name):
    """
    Zip files in a directory and its subdirectories that match the given list of extensions.
    
    Parameters:
        directory (str): The root directory to start zipping.
        extensions (list): List of file extensions to include in the zip.
        zip_file_name (str): Name of the output zip file.
    """
    with zipfile.ZipFile(zip_file_name, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for root, _, files in os.walk(directory):
            for file in files:
                if any(file.endswith(ext) for ext in extensions):
                    file_path = os.path.join(root, file)
                    zipf.write(file_path, os.path.relpath(file_path, os.path.join(directory, '..')))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Generate baseline images or run tests.')
    parser.add_argument('--generate_baseline', dest='generate_baseline', action='store_true',
                        default=False, help='if True baseline images will be generated')
    parser.add_argument('--extensions', type=str, nargs='+', default=None,
                        help='List of file extensions to test. E.g., --extensions .fbx .gltf')
    args = parser.parse_args()

    if args.generate_baseline:
        generate_baseline_function(args.extensions)
        cleanup(os.path.join(DIRECTORY, BASELINE_FOLDERNAME), [".jpg", ".json"])
        zip_files(os.path.join(DIRECTORY, BASELINE_FOLDERNAME, platform.system()), [".jpg"], platform.system() + "_baseline_images.zip")
    else:
        test_file = os.path.abspath(__file__)
        exit_code = pytest.main([test_file, "-m", "fileformat_test"])
        if exit_code == 0:
            zip_files(os.path.join(DIRECTORY, OUTPUT_FOLDERNAME, platform.system()), ["usd_checker_results.json"], platform.system() + "_usd_checker_results.zip")
        sys.exit(exit_code)
