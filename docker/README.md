# JS8Call Docker Build System

This directory contains a complete Docker-based build and runtime system for JS8Call.

## Quick Start

### Build JS8Call
```bash
./docker-build.sh
```

### Run JS8Call
```bash
./docker-run.sh
```

## Files

- `Dockerfile` - Main multi-stage build for JS8Call
- `Dockerfile.base` - Base image with all build dependencies
- `Dockerfile.hamlib` - Separate Hamlib build for caching
- `Dockerfile.runtime` - Runtime container with GUI and audio support
- `docker-compose.yml` - Compose configuration for all services
- `docker-build.sh` - Build script with caching support
- `docker-run.sh` - Run script with X11 and audio forwarding
- `docker-build-instructions.md` - Detailed documentation

## Features

✅ Cached builds for faster rebuilds
✅ X11 GUI support
✅ Audio input/output via host PulseAudio (visible in pavucontrol)
✅ Automatic AppImage extraction
✅ Persistent configuration saved to `config/JS8Call.ini`
✅ Automatic config loading on restart

See `docker-build-instructions.md` for detailed documentation.