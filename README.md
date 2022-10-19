# Real-time SPH Fluid Simulation on GPU

## How to build
Install the Vulkan SDK and build the project with your prefered Visual Studio version. Example:
```
Root/Application/Solution - Visual Studio 2022/Project.snl
```

## Project structure
```
Root
│   README.md
│
└─── Application
│   | Source
|   └─── FluidSim.cpp: Main cpp application.
|   └─── Shaders: Contains all the vertex, fragment, and compute shaders.
|   |
|   | Solution - Visual Studio 2017: Solution to build the app.
|   | Solution - Visual Studio 2019: Solution to build the app.
|   | Solution - Visual Studio 2022: Solution to build the app.
│   | Resources: The Forge minimum resources for running the app.
│
└─── The Forge: Middleware for rendering.
|
└─── Tools: The Forge tools.
```

## References
- https://en.wikipedia.org/wiki/Smoothed-particle_hydrodynamics
- http://rlguy.com/sphfluidsim/
- https://github.com/ConfettiFX/The-Forge
- https://github.com/GPUOpen-LibrariesAndSDKs/GPUParticles11/blob/master/gpuparticles11/src/SortLib.cpp
- https://en.wikipedia.org/wiki/Bitonic_sorter
- https://wickedengine.net/2018/05/21/scalabe-gpu-fluid-simulation/
