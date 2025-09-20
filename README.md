# Horizon PS2 Map Downloader

A tool for downloading and managing Horizon custom maps on PlayStation 2.

## Features

- Download maps directly from our web server
- Download only newly updated maps
- Supports Deadlocked, UYA, and R&C3

## Installation

Download the latest release from the [Releases page](https://github.com/Horizon-Private-Server/horizon-ps2-map-downloader/releases).  
Extract the `.elf` file from the downloaded archive.

To run the ELF on your PlayStation 2:

1. Copy the `.elf` file to a USB drive.
2. Insert the USB drive into your PS2.
3. Launch `uLaunchELF` on your PS2.
4. Navigate to `mass:/` (your USB drive) and select the `.elf` file to run the map downloader.

## Build Requirements

- Git
- Docker (or local PS2SDK install)

## Build

```bash
git clone https://github.com/Horizon-Private-Server/horizon-ps2-map-downloader.git
cd horizon-ps2-map-downloader
docker run -it --rm -v "$PWD\:/src" ps2dev/ps2dev:latest
```

then once inside the docker container run:

```bash
apk add make build-base
cd /src
make
```

## Contributing

Pull requests are welcome.

## License

This project is licensed under the MIT License.