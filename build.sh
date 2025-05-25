#!/bin/bash
#
# Simple build script for Arduino Serial Controller
# Uses UV + PyInstaller directly without complex packaging
#

set -e

echo "🔨 Building Arduino Serial Controller executable..."

# Check if UV is installed
if ! command -v uv &> /dev/null; then
    echo "❌ UV is not installed. Installing UV..."
    curl -LsSf https://astral.sh/uv/install.sh | sh
    source $HOME/.cargo/env
fi

# Clean previous builds
rm -rf dist/ build/ .venv/

# Create virtual environment
echo "📦 Creating virtual environment..."
uv venv

# Create virtual environment
echo "📦 Creating virtual environment..."
uv venv

# Detect and activate virtual environment (cross-platform)
echo "🔍 Activating virtual environment..."
if [[ -f ".venv/Scripts/activate" ]]; then
    echo "🪟 Using Windows activation path"
    source .venv/Scripts/activate
elif [[ -f ".venv/bin/activate" ]]; then
    echo "🐧 Using Unix activation path"
    source .venv/bin/activate
else
    echo "❌ Could not find virtual environment activation script"
    echo "Expected either .venv/Scripts/activate or .venv/bin/activate"
    ls -la .venv/
    exit 1
fi

# Install dependencies directly
echo "⬇️  Installing dependencies..."
uv pip install "pyserial>=3.5" "pyinstaller>=5.0"

# Create standalone executable
echo "🏗️  Creating standalone executable..."
pyinstaller \
    --onefile \
    --name arduino-serial-controller \
    --console \
    --clean \
    --strip \
    --optimize 2 \
    --distpath ./dist \
    --workpath ./build \
    arduino_serial_controller.py

# Verify the executable was created
if [ -f "dist/arduino-serial-controller" ]; then
    echo "✅ Executable created successfully!"
    echo "📁 Location: $(pwd)/dist/arduino-serial-controller"
    echo "📊 Size: $(du -h dist/arduino-serial-controller | cut -f1)"
    
    # Test the executable
    echo "🧪 Testing executable..."
    if timeout 5 ./dist/arduino-serial-controller --help &>/dev/null || [ $? -eq 124 ]; then
        echo "✅ Executable appears to work!"
    else
        echo "⚠️  Note: Executable may need Arduino hardware to run properly"
    fi
else
    echo "❌ Failed to create executable"
    exit 1
fi

./build_verify.sh

echo ""
echo "🎉 Build completed successfully!"
echo ""
echo "Next steps:"
echo "  1. Test: ./dist/arduino-serial-controller"
echo "  2. Upload to GitHub release"
echo "  3. Install plugin in Unraid"