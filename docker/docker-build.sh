#!/bin/bash
# Optimized Docker build script for JS8Call with caching support

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

# Check if docker-compose is installed
if ! command -v docker-compose &> /dev/null; then
    print_warning "docker-compose not found, trying 'docker compose'"
    COMPOSE_CMD="docker compose"
else
    COMPOSE_CMD="docker-compose"
fi

# Enable BuildKit for better caching
export DOCKER_BUILDKIT=1
export COMPOSE_DOCKER_CLI_BUILD=1

# Create output directory
mkdir -p output

print_status "JS8Call Docker build script with caching support"

# Parse command line arguments
BUILD_TYPE="release"
TARGET="all"
USE_CACHE=true
BUILD_BASE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --dev)
            BUILD_TYPE="dev"
            shift
            ;;
        --base)
            BUILD_BASE=true
            shift
            ;;
        --hamlib-only)
            TARGET="hamlib"
            shift
            ;;
        --cache)
            USE_CACHE=true
            shift
            ;;
        --no-cache)
            USE_CACHE=false
            shift
            ;;
        --clean)
            print_status "Cleaning previous builds..."
            rm -rf output/*
            docker rmi js8call-base:ubuntu-24.04 js8call-hamlib:latest js8call-builder:ubuntu-24.04 js8call-dev:ubuntu-24.04 2>/dev/null || true
            docker volume rm docker_ccache 2>/dev/null || true
            print_status "Clean complete"
            exit 0
            ;;
        --rebuild)
            print_status "Quick rebuild using cached images..."
            TARGET="rebuild"
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --dev          Start development container with shell"
            echo "  --base         Build/rebuild base image with all dependencies"
            echo "  --hamlib-only  Build only Hamlib"
            echo "  --cache        Use Docker build cache (default)"
            echo "  --no-cache     Build without cache"
            echo "  --clean        Clean all images and caches"
            echo "  --rebuild      Quick rebuild using existing base images"
            echo "  --help         Show this help message"
            echo ""
            echo "Examples:"
            echo "  $0                    # Normal build with cache"
            echo "  $0 --base             # Rebuild base image first"
            echo "  $0 --no-cache         # Full rebuild without cache"
            echo "  $0 --rebuild          # Quick rebuild for code changes"
            echo "  $0 --dev              # Development mode with shell"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Build base image if requested or if it doesn't exist
if [ "$BUILD_BASE" = true ] || ! docker images | grep -q "js8call-base.*ubuntu-24.04"; then
    print_status "Building base image with all dependencies..."
    if [ "$USE_CACHE" = false ]; then
        $COMPOSE_CMD build --no-cache js8call-base
    else
        $COMPOSE_CMD build js8call-base
    fi
fi

# Check if Hamlib image exists
if ! docker images | grep -q "js8call-hamlib.*latest"; then
    print_status "Hamlib image not found. Building Hamlib..."
    if [ "$USE_CACHE" = false ]; then
        $COMPOSE_CMD build --no-cache hamlib-builder
    else
        $COMPOSE_CMD build hamlib-builder
    fi
fi

# Build based on target
case $TARGET in
    hamlib)
        print_status "Building Hamlib only..."
        if [ "$USE_CACHE" = false ]; then
            $COMPOSE_CMD build --no-cache hamlib-builder
        else
            $COMPOSE_CMD build hamlib-builder
        fi
        print_status "Hamlib build complete!"
        ;;
    rebuild)
        print_status "Performing quick rebuild..."
        $COMPOSE_CMD build js8call-rebuild

        # Extract artifacts
        print_status "Extracting build artifacts..."
        docker run --rm -v "$(pwd)/output":/output js8call-rebuild:latest \
            sh -c "find /js8call-prefix/build -name '*.deb' -exec cp {} /output/ \;"

        print_info "Quick rebuild complete!"
        ls -la output/
        ;;
    all)
        if [ "$BUILD_TYPE" = "dev" ]; then
            print_status "Starting development container..."
            if [ "$USE_CACHE" = false ]; then
                $COMPOSE_CMD build --no-cache js8call-dev
            else
                $COMPOSE_CMD build js8call-dev
            fi
            print_info "Entering development shell. Source is mounted at /js8call-prefix/src"
            print_info "To build: cd /js8call-prefix/build && cmake ../src && make"
            $COMPOSE_CMD run --rm js8call-dev
        else
            print_status "Building JS8Call..."

            # Show ccache stats if available
            if docker volume ls | grep -q docker_ccache; then
                print_info "Using ccache for faster compilation"
            fi

            # Build using docker-compose
            if [ "$USE_CACHE" = false ]; then
                if ! $COMPOSE_CMD build --no-cache js8call-build; then
                    print_error "Build failed!"
                    exit 1
                fi
            else
                if ! $COMPOSE_CMD build js8call-build; then
                    print_error "Build failed!"
                    exit 1
                fi
            fi

            print_status "Extracting build artifacts..."
            $COMPOSE_CMD run --rm js8call-build

            print_status "Build complete! Output files:"
            ls -la output/

            # Check if we got the expected outputs
            if [ -z "$(ls output/*.deb 2>/dev/null)" ] && [ -z "$(ls output/*.AppImage 2>/dev/null)" ]; then
                print_warning "No output files found. Build may have failed."
            else
                print_status "Success! Build artifacts are in the 'output' directory."
            fi
        fi
        ;;
esac