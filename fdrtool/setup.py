
from setuptools import setup, find_packages
import subprocess

# Function to run PyInstaller
def run_pyinstaller():
    subprocess.run(['pyinstaller', '--onefile', 'fdrtool.py'])


# Run PyInstaller and compile protobuf before building the package
run_pyinstaller()

# Define your package and dependencies
setup(
    name='fdrtool',
    version='1.2',
    packages=find_packages(),
    package_data={'fdrtool': ['../fdr_logs_schema_pb2.py']},  # Include the generated Python file
    install_requires=[
        #dependencies here
    ],
    data_files=[('bin', ['dist/fdrtool'])],
)

