# Real-time SPH Fluid Simulation on GPU - [Web page](https://aminaliari.github.io/fluid-simulation-webpage)

## How to build
Install the Vulkan SDK and build the project with your preferred Visual Studio version. Example:
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
|   |
|   └─── Shaders: Contains all the vertex, fragment, and compute shaders.
|        └─── ShaderList.fsl: list of all shaders. Used by The Forge.
|        └─── resources.h.fsl: header file that defines all the shader resources (cbuffers, SRVs, UAVs, etc.).
|        └─── helper.h.fsl: header file with various helper functions.
|        └─── basic.vert.fsl: basic vertex shader for drawing one plane.
|        └─── instanced.vert.fsl: vertex shader for instance drawing of multiple planes.
|        └─── sphereInstanced.vert.fsl: vertex shader for instance drawing of all particles. 
|        └─── basic.frag.fsl: basic pixel shader for all objects in the scene.
|        └─── partition.comp.fsl: compute shader to partition all particles.
|        └─── sort.comp.fsl: compute shader for AMD sort.
|        └─── sortStep.comp.fsl: compute shader for AMD step sort.
|        └─── sortInner.comp.fsl: compute shader for AMD inner sort.
|        └─── resetOffset.comp.fsl: compute shader to reset offset buffer.
|        └─── partitionOffset.comp.fsl: compute shader to assign offset buffer.
|        └─── density.comp.fsl: compute shader to calculate density.
|        └─── force.comp.fsl: compute shader to calculate force.
|        └─── simluate.comp.fsl: compute shader to handle collisions and integrate force, velocity, pos, etc.
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

## License
The project's license is MIT. The Forge middleware uses Apache 2.0. However, other third parties used by it use their own respective licenses. Finally, AMD's SortLib license is MIT.
