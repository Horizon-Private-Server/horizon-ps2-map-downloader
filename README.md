# Horizon PS2 Map Downloader

A tool for downloading and managing Horizon custom maps on PlayStation 2.

## Features

- Download maps directly from our web server
- Download only newly updated maps
- Supports Deadlocked, UYA, and R&C3

## Installation



## Build Requirements

- Git
- Docker (or local PS2SDK install)

## Build

```bash
git clone https://github.com/Horizon-Private-Server/horizon-ps2-map-downloader.git
cd horizon-ps2-map-downloader
docker run -it --rm -v "$PWD\:/src" ps2dev/ps2dev:latest
apk add make build-base
make
```

## Contributing

Pull requests are welcome.

## License

This project is licensed under the MIT License.