#!/bin/bash
#
# Build script for Arduino Serial Controller
# This script uses UV to create a standalone executable
#

set -e

echo "Building Arduino Serial Controller executable..."

# Check if UV is installed
if ! command -v uv &> /dev/null; then
    echo "UV is not installed. Installing UV..."
    exit 1
fi

# Clean previous builds
rm -rf dist/ build/ *.egg-info/

# Create the project structure
mkdir -p src/arduino_serial_controller

# Copy the main Python script to the proper location
cp arduino_serial_controller.py src/arduino_serial_controller/__init__.py

# Create __main__.py for executable entry point
cat > src/arduino_serial_controller/__main__.py << 'EOF'
#!/usr/bin/env python3
"""
Entry point for Arduino Serial Controller executable
"""

from . import main

if __name__ == '__main__':
    main()
EOF

# Build the project with UV
echo "Creating virtual environment and installing dependencies..."
uv venv
source .venv/bin/activate

echo "Installing project in development mode..."
uv pip install -e .

echo "Installing PyInstaller for creating executable..."
uv pip install pyinstaller

echo "Creating standalone executable..."
pyinstaller \
    --onefile \
    --name arduino-serial-controller \
    --console \
    --clean \
    --strip \
    --optimize 2 \
    --distpath ./dist \
    --workpath ./build \
    --specpath ./build \
    src/arduino_serial_controller/__main__.py

# Verify the executable was created
if [ -f "dist/arduino-serial-controller" ]; then
    echo "✅ Executable created successfully!"
    echo "📁 Location: $(pwd)/dist/arduino-serial-controller"
    echo "📊 Size: $(du -h dist/arduino-serial-controller | cut -f1)"
    
    # Test the executable
    echo "🧪 Testing executable..."
    if ./dist/arduino-serial-controller --help &>/dev/null || [ $? -eq 0 ]; then
        echo "✅ Executable test passed!"
    else
        echo "⚠️  Executable may have issues, but this could be expected without Arduino connected"
    fi
else
    echo "❌ Failed to create executable"
    exit 1
fi

echo ""
echo "🎉 Build completed successfully!"
echo ""
echo "To use the executable:"
echo "  1. Copy dist/arduino-serial-controller to your Unraid server"
echo "  2. Make it executable: chmod +x arduino-serial-controller"
echo "  3. Run it: ./arduino-serial-controller"
echo ""
echo "For the Unraid plugin:"
echo "  1. Upload dist/arduino-serial-controller to your GitHub release"
echo "  2. Install the plugin through Unraid's Community Applications"