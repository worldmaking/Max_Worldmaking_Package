
# A package for Max/MSP/Jitter to support computational worldmaking

Currently includes:

- **oculusrift** -- supports the Oculus Rift head-mounted display (developer kit and consumer models, currently Windows only)
- **htcvive** -- supports the HTC Vive head-mounted display (currently Windows only)
- **ws** -- a simple websocket server external for max, making it trivial to interact with browser-based clients (Windows/OSX)

Work in progress (may have limited functionality/stability issues)

- **kinect** -- supports the kinect v1 including multiple devices, skeleton, point cloud etc. (windows only)
- **kinect2** -- supports the kinect v2 (for windows/for xbox versions, windows only)

Much more to come!

---------------

## Installing

Download the package and place into your Max Packages folder. (On Windows, ```My Documents/Max 7/Packages```, on Mac, ```~/Documents/Max 7/Packages```), then restart Max.

Take a look at patchers in the ```help``` and ```examples``` folders to get to know the package.


### Dependencies

- The [oculusrift] object depends upon having the [Oculus Home](http://www.oculus.com/en-us/setup/) software installed & calibrated. It is free.
- The [htcvive] object depends upon having [Steam and SteamVR](http://store.steampowered.com/steamvr) installed & calibrated. It is free.

> If you have trouble loading externals on Windows, you might need to install the [visual studio 2015 redistributable package](https://www.microsoft.com/en-us/download/details.aspx?id=48145). It is free.

#### Hardware requirements for the VR objects ([oculusrift] and [htcvive])

Obviously, you need the respective Oculus Rift or HTC Vive head-mounted display hardware!

Creating a virtual reality world requires high-performance computer graphics -- with the current generation of consumer head-mounted displays, it requires the equivalent of two HD or better resolution screens rendering at 90 frames per second. At present the recommended PC requirements to cover both the Rift and the Vive are:

- **GPU:** NVIDIA GTX 970 / AMD R9 290 equivalent or greater
- **Video Output:** HDMI 1.4 or DisplayPort 1.2 or newer
- **CPU:** Intel i5-4590 / AMD FX 8350 equivalent or greater
- **Memory:** 8GB+ RAM
- **USB:** 3x USB 3.0 ports plus 1x USB 2.0 port
- **OS:** Windows 7 SP1 64 bit or newer (No OSX support at present)

---------------

## Development

See the ```/source``` folder. You may need to ```git submodules init && git submodules update``` to get all dependencies.

