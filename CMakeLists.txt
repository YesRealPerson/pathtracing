cmake_minimum_required(VERSION 3.18)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
project(Assignment3)

#-------------------------------------------------------------------------------
# Build options
#-------------------------------------------------------------------------------
option(BUILD_DEBUG     "Build with debug settings"    OFF)
option(BUILD_DOCS      "Build documentation"          OFF)


set(BUILD_DEBUG ${BUILD_DEBUG} CACHE BOOL "Build debug" FORCE)

if (BUILD_DEBUG)
  set(CMAKE_BUILD_TYPE Debug)
endif()

#-------------------------------------------------------------------------------
# CUDA
#-------------------------------------------------------------------------------
# Pick GPU architectures. Override with -DCMAKE_CUDA_ARCHITECTURES=86 etc.
if (NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    set(CMAKE_CUDA_ARCHITECTURES native)
  else()
    set(CMAKE_CUDA_ARCHITECTURES 75)
  endif()
endif()

enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# --expt-relaxed-constexpr : lets device code call constexpr host functions
#                            (e.g. std::numeric_limits<double>::infinity()).
# --extended-lambda        : allows __device__ lambdas.
set(PT_CUDA_FLAGS --expt-relaxed-constexpr --extended-lambda)
if (MSVC)
  # Silence the same MSVC warnings CGL disables for host code.
  list(APPEND PT_CUDA_FLAGS -Xcompiler=/wd4838,/wd4305,/wd4244,/wd4267)
endif()

if (BUILD_DEBUG)
  list(APPEND PT_CUDA_FLAGS -G -lineinfo)
else()
  list(APPEND PT_CUDA_FLAGS -lineinfo)
endif()

#-------------------------------------------------------------------------------
# Set target
#-------------------------------------------------------------------------------
# .cu files can be added to these source lists directly.
set(APPLICATION_3_1_SOURCE

    # Scene Object & Structure
    src/scene/sphere.cpp
    src/scene/triangle.cpp
    src/scene/light.cpp
    src/scene/bvh.cpp
    src/scene/bbox.cpp

    # Pathtracer
    src/pathtracer/camera.cpp
    src/pathtracer/bsdf.cpp
    src/pathtracer/pathtracer.cpp
    src/pathtracer/gpu_scene.cu
    src/pathtracer/gpu_render.cu

    # Imgui
    src/imgui/imgui.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/backends/imgui_impl_glfw.cpp
    src/imgui/backends/imgui_impl_opengl2.cpp

    src/application/pathtracer_launcher_gui.cpp
)

set(APPLICATION_3_2_SOURCE
    src/scene/object.cpp

    # Collada Parser
    src/scene/collada/collada.cpp
    src/scene/collada/camera_info.cpp
    src/scene/collada/light_info.cpp
    src/scene/collada/sphere_info.cpp
    src/scene/collada/polymesh_info.cpp
    src/scene/collada/material_info.cpp

    # Dynamic Scene
    src/scene/gl_scene/mesh.cpp
    src/scene/gl_scene/scene.cpp
    src/scene/gl_scene/sphere.cpp

    # MeshEdit
    src/util/halfEdgeMesh.cpp
    src/application/meshEdit.cpp

    src/pathtracer/sampler.cpp
    src/pathtracer/advanced_bsdf.cpp
    src/scene/environment_light.cpp
    src/pathtracer/camera_lens.cpp
    src/pathtracer/raytraced_renderer.cpp

    # misc
    src/util/sphere_drawing.cpp
    src/util/lodepng.cpp

    # Application
    src/application/application.cpp
    src/application/main.cpp
    src/application/visual_debugger.cpp

    src/imgui/imgui.cpp
    src/imgui/imgui_widgets.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/backends/imgui_impl_glfw.cpp
    src/imgui/backends/imgui_impl_opengl2.cpp
)

set(APPLICATION_HEADERS
    # Collada Parser
    src/scene/collada/camera_info.h
    src/scene/collada/collada_info.h
    src/scene/collada/collada.h
    src/scene/collada/light_info.h
    src/scene/collada/material_info.h
    src/scene/collada/polymesh_info.h
    src/scene/collada/sphere_info.h
    # Dynamic Scene
    src/scene/gl_scene/ambient_light.h
    src/scene/gl_scene/area_light.h
    src/scene/gl_scene/directional_light.h
    src/scene/gl_scene/draw_style.h
    src/scene/gl_scene/environment_light.h
    src/scene/gl_scene/material.h
    src/scene/gl_scene/mesh_view.h
    src/scene/gl_scene/mesh.h
    src/scene/gl_scene/point_light.h
    src/scene/gl_scene/scene.h
    src/scene/gl_scene/sphere.h
    src/scene/gl_scene/spot_light.h
    # Scene Object & Structure
    src/scene/aggregate.h
    src/scene/bbox.h
    src/scene/bvh.h
    src/scene/environment_light.h
    src/scene/light.h
    src/scene/object.h
    src/scene/primitive.h
    src/scene/scene.h
    src/scene/sphere.h
    src/scene/triangle.h
    # MeshEdit
    src/util/halfEdgeMesh.h
    src/util/image.h
    src/util/mutablePriorityQueue.h
    src/util/random_util.h
    src/util/work_queue.h
    # Pathtracer
    src/pathtracer/bsdf.h
    src/pathtracer/camera.h
    src/pathtracer/intersection.h
    src/pathtracer/pathtracer.h
    src/pathtracer/ray.h
    src/pathtracer/raytraced_renderer.h
    src/pathtracer/sampler.h
    src/pathtracer/gpu_scene.h
    # UNREVIEWED CLAUDE CODE TO BE DELETED
    src/pathtracer/gpu_intersect.cuh
    src/pathtracer/gpu_render.h
    # END UNREVIEWED CLAUDE CODE
    # misc
    src/util/sphere_drawing.h
    src/util/lodepng.h
    # Application
    src/application/application.h
    src/application/meshEdit.h
    src/application/renderer.h
)

if (WIN32)
    list(APPEND APPLICATION_3_1_SOURCE src/util/win32/getopt.c)
endif()

if (WIN32 OR APPLE)
  add_library(pt31 STATIC ${APPLICATION_3_1_SOURCE} ${APPLICATION_HEADERS})
else()
  add_library(pt31 SHARED ${APPLICATION_3_1_SOURCE} ${APPLICATION_HEADERS})
endif()

target_include_directories(pt31 PUBLIC src)

add_executable(pathtracer ${APPLICATION_3_2_SOURCE} ${APPLICATION_HEADERS})
target_include_directories(pathtracer PUBLIC src)

target_link_libraries(pt31 PUBLIC CGL)
target_link_libraries(pathtracer PUBLIC pt31)

foreach(tgt pt31 pathtracer)
  # Separable compilation lets __device__ functions be defined in one .cu
  # file and called from another.
  set_target_properties(${tgt} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON)
  target_compile_options(${tgt} PRIVATE $<$<COMPILE_LANGUAGE:CUDA>:${PT_CUDA_FLAGS}>)
endforeach()
target_link_libraries(pt31 PUBLIC CUDA::cudart)

set(CGL_INCLUDE_DIRS CGL/include CGL/deps/glew/include CGL/deps/glfw/include ./src/imgui ./src/imgui/backends)

#-------------------------------------------------------------------------------
# Find dependencies
#-------------------------------------------------------------------------------
add_subdirectory(CGL)
target_include_directories(pt31 PUBLIC ${CGL_INCLUDE_DIRS})
target_include_directories(pathtracer PUBLIC ${CGL_INCLUDE_DIRS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CGL_CXX_FLAGS}")

set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)
target_link_libraries(pt31 PUBLIC OpenGL::GL)
target_link_libraries(pt31 PUBLIC OpenGL::GLU)

#-------------------------------------------------------------------------------
# Add subdirectories
#-------------------------------------------------------------------------------

# build documentation
if(BUILD_DOCS)
  find_package(DOXYGEN)
  if(DOXYGEN_FOUND AND BUILD_DOCS)
    add_subdirectory(docs)
  endif()
endif()

# Install settings
set(CMAKE_INSTALL_PREFIX "${Assignment1_SOURCE_DIR}/")
