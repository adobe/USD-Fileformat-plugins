def pytest_addoption(parser):
    parser.addoption("--extensions", action="store", default=None,
                     help="List of file extensions to test. E.g., --extensions .fbx,.gltf")
