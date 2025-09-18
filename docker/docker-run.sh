#!/bin/bash
# Script to run JS8Call AppImage in Docker with X11 and audio support
#
# Usage: ./docker-run.sh [--rebuild] [--debug] [--help]

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[*]${NC} $1"
}

print_error() {
    echo -e "${RED}[!]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

print_info() {
    echo -e "${BLUE}[i]${NC} $1"
}

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    print_error "Docker is not installed. Please install Docker first."
    exit 1
fi

# Parse command line arguments
REBUILD_IMAGE=false
DEBUG_MODE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --rebuild)
            REBUILD_IMAGE=true
            shift
            ;;
        --debug)
            DEBUG_MODE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --rebuild  Rebuild the runtime image"
            echo "  --debug    Run bash instead of JS8Call for debugging"
            echo "  --help     Show this help message"
            echo ""
            echo "Any additional arguments are passed to JS8Call"
            exit 0
            ;;
        *)
            # Pass through to JS8Call
            break
            ;;
    esac
done

# Rebuild image if requested
if [ "$REBUILD_IMAGE" = true ]; then
    print_status "Removing old runtime image..."
    docker rmi js8call-runtime:ubuntu-24.04 2>/dev/null || true
    print_status "Runtime image will be rebuilt"
fi

# Check if AppImage exists
APPIMAGE_PATH=""
if [ -f "output/js8call-x86_64.AppImage" ]; then
    APPIMAGE_PATH="output/js8call-x86_64.AppImage"
elif [ -f "../output/js8call-x86_64.AppImage" ]; then
    APPIMAGE_PATH="../output/js8call-x86_64.AppImage"
else
    # Try to find it
    APPIMAGE_PATH=$(find . -name "js8call*.AppImage" -type f | head -1)
    if [ -z "$APPIMAGE_PATH" ]; then
        print_error "JS8Call AppImage not found!"
        print_info "Please build it first with: ./docker-build.sh"
        exit 1
    fi
fi

print_status "Found AppImage at: $APPIMAGE_PATH"

# Audio setup - use PulseAudio TCP for better container compatibility
print_info "Setting up PulseAudio TCP connection..."

# Load PulseAudio TCP module on the host
pactl load-module module-native-protocol-tcp port=4713 auth-anonymous=1 >/dev/null 2>&1 || true

# Configure container to use TCP connection
AUDIO_OPTS="-e PULSE_SERVER=tcp:host.docker.internal:4713 --add-host=host.docker.internal:host-gateway"
print_status "Using PulseAudio TCP connection (JS8Call will appear in pavucontrol)"

# Keep the temp variable for cleanup
PULSE_COOKIE_TEMP=""

# Build runtime image if it doesn't exist
if ! docker images | grep -q "js8call-runtime.*ubuntu-24.04"; then
    print_status "Building runtime image..."
    docker build -f Dockerfile.runtime -t js8call-runtime:ubuntu-24.04 .
fi

# Get the absolute path of the AppImage
APPIMAGE_ABS_PATH=$(realpath "$APPIMAGE_PATH")

# Make sure the AppImage is executable on the host
chmod +x "$APPIMAGE_ABS_PATH" 2>/dev/null || true

# Set up X11 forwarding
XSOCK=/tmp/.X11-unix
XAUTH=/tmp/.docker.xauth.$$

# Allow X connections from local Docker containers
print_info "Setting up X11 forwarding..."
xhost +local:docker 2>/dev/null || xhost +local: 2>/dev/null || true

# Create new Xauthority file
rm -f $XAUTH
# Since xhost +local:docker allows access, we don't strictly need xauth
# But we'll create an empty file to avoid warnings
touch $XAUTH
chmod 644 $XAUTH

print_status "Starting JS8Call with X11 and audio forwarding..."

# Create config directory if it doesn't exist
CONFIG_DIR="$(pwd)/config"
if [ ! -d "$CONFIG_DIR" ]; then
    print_info "Creating config directory..."
    mkdir -p "$CONFIG_DIR"
fi

# Create JS8Call.ini file if it doesn't exist
CONFIG_FILE="$CONFIG_DIR/JS8Call.ini"
if [ ! -f "$CONFIG_FILE" ]; then
    touch "$CONFIG_FILE"
fi

# Make config directory writable
chmod 777 "$CONFIG_DIR"
chmod 666 "$CONFIG_FILE" 2>/dev/null || true

# Copy AppImage to a temp location for the container
TEMP_APPIMAGE="/tmp/js8call-${RANDOM}.AppImage"
cp "$APPIMAGE_ABS_PATH" "$TEMP_APPIMAGE"

# Choose command based on debug mode
if [ "$DEBUG_MODE" = true ]; then
    print_info "Running in debug mode - starting bash shell"
else
    print_info "Running JS8Call normally"
fi

# No need for audio group with TCP connection
AUDIO_GROUP_OPT=""

# Run the container
if [ "$DEBUG_MODE" = true ]; then
    docker run --rm -it \
        --name js8call-runtime \
        -e DISPLAY=$DISPLAY \
        -e XAUTHORITY=/tmp/.docker.xauth \
        -e QT_X11_NO_MITSHM=1 \
        -v $XSOCK:$XSOCK:rw \
        -v $XAUTH:/tmp/.docker.xauth:rw \
        -v "$TEMP_APPIMAGE:/tmp/js8call.AppImage:ro" \
        -v "$CONFIG_DIR:/tmp/js8call-config" \
        $AUDIO_OPTS \
        --device /dev/dri \
        $AUDIO_GROUP_OPT \
        --network host \
        --ipc=host \
        --entrypoint /bin/bash \
        js8call-runtime:ubuntu-24.04
else
    docker run --rm -it \
        --name js8call-runtime \
        -e DISPLAY=$DISPLAY \
        -e XAUTHORITY=/tmp/.docker.xauth \
        -e QT_X11_NO_MITSHM=1 \
        -v $XSOCK:$XSOCK:rw \
        -v $XAUTH:/tmp/.docker.xauth:rw \
        -v "$TEMP_APPIMAGE:/tmp/js8call.AppImage:ro" \
        -v "$CONFIG_DIR:/tmp/js8call-config" \
        $AUDIO_OPTS \
        --device /dev/dri \
        $AUDIO_GROUP_OPT \
        --network host \
        --ipc=host \
        js8call-runtime:ubuntu-24.04 "$@"
fi

# Check exit code
EXIT_CODE=$?
if [ $EXIT_CODE -ne 0 ]; then
    print_error "Container exited with code $EXIT_CODE"
fi

# After JS8Call exits, show config location
if [ -f "$CONFIG_FILE" ] && [ -s "$CONFIG_FILE" ]; then
    print_info "JS8Call configuration saved to: $CONFIG_FILE"
    # Also create a symlink at js8call.ini in current directory for easy access
    if [ ! -f "js8call.ini" ]; then
        ln -sf "$CONFIG_FILE" "js8call.ini"
        print_info "Created symlink: js8call.ini -> $CONFIG_FILE"
    fi
else
    print_warning "No configuration was saved (JS8Call may not have created settings yet)"
fi

# Cleanup function
cleanup() {
    rm -f "$TEMP_APPIMAGE"
    rm -f $XAUTH
    [ -n "$PULSE_COOKIE_TEMP" ] && rm -f "$PULSE_COOKIE_TEMP"
    xhost -local:docker 2>/dev/null || true
    # Try to unload the PulseAudio TCP module
    pactl unload-module module-native-protocol-tcp 2>/dev/null || true
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Wait for container to finish
wait
