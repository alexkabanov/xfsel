# xfsel

## Usage

Copy to clipboard:

    `xfsel -c file...`

Cut to clipboard:

    `xfsel -y file...`

Paste from clipboard:

    `xfsel -p [directory]`

## Installation

Install from source:

    ```
    mkdir build
    cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=MinSizeRel
    make
    make install
    ```
