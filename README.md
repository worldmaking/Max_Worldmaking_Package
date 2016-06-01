
# A package for Max/MSP/Jitter to support computational worldmaking

Currently includes:

- **oculusrift** -- supports the Oculus Rift head-mounted display (developer kit and consumer models, currently Windows only)
- **ws** -- a simple websocket server external for max, making it trivial to interact with browser-based clients (Windows/OSX)

Work in progress (may have limited functionality/stability issues)

- **htcvive** -- supports the HTC Vive head-mounted display (currently Windows only)
- **kinect** -- supports the kinect v1 including multiple devices, skeleton, point cloud etc. (windows only)
- **kinect2** -- supports the kinect v2 (for windows/for xbox versions, windows only)

Much more to come!

---------------

## Installing

Download the package and place into your Max Packages folder. (On Windows, ```My Documents/Max 7/Packages```, on Mac, ```~/Documents/Max 7/Packages```). Restart Max.

Take a look at patchers in the ```help``` and ```examples``` folders to get to know the package.

---------------

## Development

See the ```/source``` folder. You may need to ```git submodules init && git submodules update``` to get all dependencies.

