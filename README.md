# Polymer
In-development Minecraft client using C++ and Vulkan.

It can only connect to offline Java servers at the moment, but online mode is planned. There's currently no physics, but there's a spectator-like camera for looking around.  

It uses the original assets from a user-provided Minecraft jar. Only basic blocks can be rendered at the moment.

### Screenshots
![Polymer Image](https://i.imgur.com/rAfkvtd.png)

### Running
Only runs on Windows currently.  

- Requires `1.20.1.jar` from your Minecraft installation to be in the working directory. You can find this in `%appdata%/.minecraft/versions/1.20.1/`. This is used to load the textures and block models.
- Requires `blocks-1.20.1.json` that is generated from running Minecraft with a [certain flag](https://wiki.vg/Data_Generators#Generators) or from the [polymer release page](https://github.com/atxi/Polymer/releases).
- Requires `unifont.hex` for font rendering. This can be obtained from the `unifont.zip` resource on the resource server.
- Requires compiled shaders. Get them from the release page or read the shader section below if manually building.
  
Running the exe will connect to localhost with the username 'polymer'. The server must be configured to be in offline mode.  

You can specify the username and server ip by command line.  
`polymer.exe -u username -s 127.0.0.1:25565`

Currently only a spectator camera is implemented for flying around and rendering the world. By default, you will be in the survival gamemode on the server. If you want chunks to load as you move, you need to put yourself in spectator gamemode. You can do this in the server terminal or in game with the command `/gamemode spectator`.

### Building
Currently only compiles on Windows, but other platforms are planned.  
The project is configured to use vcpkg as a dependency manager.  

#### Requirements
- C++ compiler (tested with MSVC 2022 and Clang)
- [CMake](https://cmake.org/)
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/)

#### Instructions
- Open terminal in polymer folder.
- `git submodule update --init`
- `cmake -B build -S . --preset msvc`
- Open the generated `build/polymer.sln` and build in x64 Release mode.
- The final executable will be in the `build/Release` folder.

#### Shaders
The shaders must be compiled with glslc.exe that comes with the vulkan sdk. The `compile_shaders.bat` file will compile them if `%VULKAN_SDK%` is properly set in the environment variables.
