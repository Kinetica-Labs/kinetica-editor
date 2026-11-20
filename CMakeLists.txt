cmake_minimum_required(VERSION 3.22)
project(KineticaEditor VERSION 0.1.0 LANGUAGES C CXX)

# Kinetica Editor: a robotics editor - a robot arm, a drone, a wheeled robot, their joints, motors and
# imperfect sensors - on top of a world-class rigid-body engine (Box3D). The parts:
#   src/     kecore: the robot's world without Qt - math, devices, sensors, scene, the running world, and the
#            contracts plugins implement (docs/contracts), the physics engine's among them (src/physics);
#   plugins/ the implementations of contracts: plugins/physics/box3d, the only place that includes Box3D;
#   editor/  kinetica, the Qt 6 window;
#   tests/   ke_tests: kecore against exact answers;
#   extern/  every git submodule (Box3D).
#   git clone --recurse-submodules git@github.com:Kinetica-Labs/kinetica-editor.git
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j && build/ke_tests
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(KE_TESTS  "Build ke_tests" ON)
option(KE_DOUBLE "ke::Real is double in the whole core (default float)" OFF)

# Box3D as a subproject builds only its library: its samples, tests and validation are top-level only.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/extern/box3d/CMakeLists.txt")
  message(FATAL_ERROR "extern/box3d is empty: git submodule update --init")
endif()
add_subdirectory(extern/box3d)

# ---------------------------------------------------------------------------
# kecore: standard C++17 and the physics engine only - never Qt (it must build and run headless).
# ---------------------------------------------------------------------------
add_library(kecore STATIC
  src/math/Math.h src/math/Real.h src/math/AABB.h
  src/math/ElementaryFunctions.h src/math/ElementaryFunctions.cpp
  src/math/Vector2.h src/math/Vector3.h src/math/Quaternion.h
  src/math/Matrix3x3.h
  src/core/Format.h        src/core/Format.cpp
  src/core/Value.h         src/core/Value.cpp
  src/core/Units.h         src/core/Units.cpp
  src/core/Field.h         src/core/Field.cpp
  src/core/Probe.h         src/core/Probe.cpp
  src/core/Random.h
  src/core/Mesh.h          src/core/Mesh.cpp
  src/core/RuntimePolicy.h src/core/RuntimePolicy.cpp
  src/core/SurfaceTexture.h
  src/spatial/BVH.h        src/spatial/BVH.cpp
  src/devices/PinEdge.h
  src/devices/Encoder.h    src/devices/Encoder.cpp
  src/devices/Signal.h     src/devices/Signal.cpp
  src/devices/Imu.h        src/devices/Imu.cpp
  src/devices/Sonar.h      src/devices/Sonar.cpp
  src/devices/ForceTorque.h src/devices/ForceTorque.cpp
  src/electro/Battery.h    src/electro/Battery.cpp
  src/scene/JointModel.h   src/scene/JointModel.cpp src/scene/Visuals.h
  src/scene/SceneGraph.h   src/scene/SceneGraph.cpp src/scene/SceneHierarchy.cpp src/scene/EntityShapes.cpp
  src/scene/SceneEdits.h   src/scene/SceneEdits.cpp
  src/scene/SceneActions.h src/scene/SceneActions.cpp src/scene/SceneActionsTree.cpp
  src/scene/Assembly.h     src/scene/Assembly.cpp src/scene/AssemblyPresets.cpp
  src/scene/CameraHousing.h src/scene/CameraHousing.cpp
  src/scene/Collider.h     src/scene/Collider.cpp
  src/scene/Kinematics.h   src/scene/Kinematics.cpp
  src/scene/Describe.h     src/scene/Describe.cpp
  src/scene/SensorSpec.h   src/scene/SensorSpec.cpp
  src/scene/SceneOps.h     src/scene/SceneOps.cpp src/scene/SceneOpsActions.cpp src/scene/SceneOpsAssembly.cpp
  src/scene/SceneOpsKinematics.cpp src/scene/SceneOpsControllers.cpp src/scene/SceneOpsAnimation.cpp
  src/scene/Animation.h    src/scene/Animation.cpp
  src/physics/HullPoints.h src/physics/PhysicsSettings.h src/physics/PhysicsWorld.h
  plugins/physics/box3d/Box3dWorld.cpp
  src/sim/Frame.h          src/sim/RobotWorld.h src/sim/RobotWorld.cpp src/sim/RobotControllers.cpp src/sim/RobotPower.cpp
  src/sim/LiveSensors.h    src/sim/LiveSensors.cpp
  src/sim/Channels.h       src/sim/Channels.cpp
  src/sim/Simulation.h     src/sim/Simulation.cpp
)
# ke/Config.h: the options that change the core's types, one header for the library and its programs.
configure_file(src/Config.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/ke/Config.h)
target_include_directories(kecore PUBLIC src ${CMAKE_CURRENT_BINARY_DIR}/generated)
find_package(Threads REQUIRED)
target_link_libraries(kecore PUBLIC Threads::Threads PRIVATE box3d::box3d)
get_target_property(KE_CORE_LIBS kecore LINK_LIBRARIES)
string(REGEX REPLACE "\\$<LINK_ONLY:([^>]*)>" "\\1" KE_CORE_LIBS "${KE_CORE_LIBS}")
if(NOT KE_CORE_LIBS STREQUAL "Threads::Threads;box3d::box3d")
  message(FATAL_ERROR "kecore must depend on the C++ standard library and the physics engine only, got: ${KE_CORE_LIBS}")
endif()
if(MSVC)
  target_compile_options(kecore PRIVATE /W4 /wd4100 /utf-8)
  target_compile_definitions(kecore PUBLIC _CRT_SECURE_NO_WARNINGS) # std::getenv and std::fopen are standard C++, not "unsafe"
else()
  target_compile_options(kecore PRIVATE -Wall -Wextra -Wno-unused-parameter)
  # ke::sin and the others give the same bits in every build: Dekker's exact product inside breaks
  # under a fused multiply-add.
  set_source_files_properties(src/math/ElementaryFunctions.cpp PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()

# ---------------------------------------------------------------------------
# kinetica: the Qt 6 window, a client of kecore. Built when Qt is found (-DCMAKE_PREFIX_PATH=<Qt>/gcc_64).
# ---------------------------------------------------------------------------
# The window links OpenGL as Qt's own libraries do - libGL.so.1, there wherever a GL driver is - not GLVND's
# libOpenGL.so.0, which a clean Ubuntu lacks (the package libopengl0): the one-folder program would not start.
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(Qt6 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Network)
if(Qt6_FOUND)
  qt_standard_project_setup()
  qt_add_executable(kinetica WIN32
    editor/main.cpp
    editor/MainWindow.h      editor/MainWindow.cpp editor/MainWindowStart.cpp editor/MainWindowControls.cpp
    editor/MainWindowToolbar.cpp editor/MainWindowLayout.cpp editor/MainWindowPlay.cpp editor/MainWindowApi.cpp
    editor/MainWindowPlots.cpp editor/MainWindowLab.cpp
    editor/UiWindowSpace.h   editor/KeyPlace.h editor/RuPlural.h
    editor/EditorRuntime.h   editor/EditorRuntime.cpp editor/EditorOpsRuntime.cpp
    editor/AgentInstanceGuard.h editor/AgentInstanceGuard.cpp
    editor/EditorOps.h       editor/EditorOps.cpp editor/EditorOpsCore.cpp editor/EditorOpsAssembly.cpp
    editor/EditorOpsState.cpp editor/EditorOpsInput.cpp editor/EditorOpsSim.cpp editor/EditorOpsImpulse.cpp
    editor/EditorJournal.cpp editor/EditorJournalRestore.cpp
    editor/EditorServer.h    editor/EditorServer.cpp
    editor/ApiConsole.h      editor/ApiConsole.cpp
    editor/SceneBuilder.h    editor/SceneBuilder.cpp editor/SceneBuilderPanels.cpp editor/SceneBuilderObjects.cpp
    editor/SceneBuilderClone.cpp editor/SceneBuilderMulti.cpp editor/SceneBuilderLights.cpp editor/SceneBuilderKeep.cpp
    editor/SceneBuilderSequence.cpp editor/EditorOpsRecord.cpp editor/ControllerKeys.h editor/ControllerKeys.cpp
    editor/PluginHost.h editor/PluginHost.cpp editor/RunHooks.h editor/RunHooks.cpp editor/PluginPanel.h editor/PluginPanel.cpp editor/MainWindowPlugins.cpp editor/PythonRuntime.h editor/Resources.h editor/ArgsSchema.h editor/ArgsSchema.cpp editor/SequencerPanel.h editor/SequencerPanel.cpp
    editor/SequencerViews.h editor/SequencerTimeline.cpp editor/SequencerCurves.cpp
    editor/SceneBuilderSelection.cpp editor/SceneBuilderApi.cpp editor/SceneBuilderCode.cpp
    editor/SceneBuilderColliders.cpp editor/SceneBuilderComponents.cpp editor/SceneBuilderJoints.cpp editor/SceneBuilderActuators.cpp
    editor/SceneBuilderSensors.cpp editor/SceneBuilderDevices.cpp
    editor/SceneBuilderControllers.cpp
    editor/ObjectInspector.h editor/ObjectInspector.cpp
    editor/InspectorWidgets.h editor/InspectorWidgets.cpp
    editor/RoleBar.h         editor/RoleBar.cpp
    editor/Icons.h           editor/Icons.cpp
    editor/ManyPanels.h      editor/ManyPanels.cpp
    editor/ClonePopover.h    editor/ClonePopover.cpp
    editor/ColliderGuides.h  editor/ColliderGuides.cpp
    editor/WorldPanel.h      editor/WorldPanel.cpp
    editor/TexturePanel.h    editor/TexturePanel.cpp editor/TextureBumpPanel.cpp
    editor/MagnetController.h editor/MagnetController.cpp
    editor/PieMenu.h         editor/PieMenu.cpp
    editor/IkSolverView.h    editor/IkSolverView.cpp
    editor/CommandSearch.h   editor/CommandSearch.cpp
    editor/CameraPicker.h    editor/CameraPicker.cpp
    editor/FirstStartHint.h  editor/FirstStartHint.cpp
    editor/SimulationBar.h   editor/SimulationBar.cpp
    editor/ExamplesGallery.h editor/ExamplesGallery.cpp
    editor/PlotPanel.h       editor/PlotPanel.cpp
    editor/PlotChannels.h    editor/PlotChannels.cpp
    editor/PlotSeries.h      editor/PlotSeries.cpp
    editor/ChannelList.h     editor/ChannelList.cpp
    editor/TrackChart.h      editor/TrackChart.cpp
    editor/LabPanel.h        editor/LabPanel.cpp
    editor/LabLayers.h       editor/LabLayers.cpp
    editor/LabCard.h         editor/LabCard.cpp
    editor/ProfilerBar.h     editor/ProfilerBar.cpp
    editor/SimController.h   editor/SimController.cpp
    editor/EditView.h        editor/EditView.cpp
    editor/Viewport.h        editor/Viewport.cpp editor/ViewportInput.cpp editor/ViewportLights.cpp editor/ViewportShadows.cpp
    editor/ViewportPlay.cpp  editor/ViewportHud.cpp editor/ViewportLab.cpp editor/ViewportTextures.cpp editor/ViewportTextureDrop.cpp
    editor/ViewportTools.h   editor/ViewportTools.cpp
    editor/OrbitCamera.h     editor/OrbitCamera.cpp
    editor/ControlScheme.h   editor/ControlScheme.cpp
    editor/NavCube.h         editor/NavCube.cpp
    editor/Shaders.h         editor/Shaders.cpp
    editor/MeshNormals.h     editor/MeshNormals.cpp
    editor/JointGlyphs.h     editor/JointGlyphs.cpp
    editor/SensorGlyphs.h    editor/SensorGlyphs.cpp
    editor/SceneMarkers.h    editor/SceneMarkers.cpp
    editor/Gizmo.h           editor/Gizmo.cpp
    editor/SelfTest.h        editor/SelfTest.cpp editor/SelfTestSupport.h
    editor/SelfTestControls.cpp editor/SelfTestComponents.cpp editor/SelfTestMany.cpp editor/SelfTestLights.cpp
    editor/SelfTestSequencer.cpp editor/SelfTestPlugins.cpp
    editor/SelfTestShadows.cpp editor/SelfTestTextures.cpp editor/SelfTestBump.cpp editor/SelfTestPlay.cpp
    editor/SelfTestLayout.cpp editor/SelfTestLab.cpp editor/SelfTestPlots.cpp editor/SelfTestJournal.cpp
    editor/SelfTestGoal.cpp editor/SelfTestWorld.cpp
    editor/SelfTestShaders.cpp editor/SelfTestCreate.cpp
  )
  qt_add_resources(kinetica shaders PREFIX "/shaders" BASE shaders FILES
    shaders/background.vert shaders/background.frag shaders/outline.frag shaders/mesh.vert shaders/mesh.frag
    shaders/shadow.vert shaders/shadow.frag shaders/shadow-distance.vert shaders/shadow-distance.frag shaders/lines.vert shaders/lines.frag)
  # The application's icon is embedded, so a launch from a terminal has it too.
  qt_add_resources(kinetica app_identity PREFIX "/app" BASE resources FILES
    resources/icons/kinetica-16.png resources/icons/kinetica-24.png resources/icons/kinetica-32.png resources/icons/kinetica-48.png
    resources/icons/kinetica-64.png resources/icons/kinetica-128.png resources/icons/kinetica-256.png)
  if(WIN32) # the program's own icon: what Explorer, the taskbar and a shortcut show
    target_sources(kinetica PRIVATE resources/kinetica.rc)
  endif()
  target_link_libraries(kinetica PRIVATE kecore Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network)
  # examples/, data/ and python/ of the source tree when the program runs from the build (MainWindow::resourceDir).
  target_compile_definitions(kinetica PRIVATE KE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(kinetica PRIVATE /W4 /utf-8)
  else()
    target_compile_options(kinetica PRIVATE -Wall -Wextra)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests: build/ke_tests, exit code = number of failures; KE_TEST=<substring> runs only the matching ones.
# ---------------------------------------------------------------------------
if(KE_TESTS)
  enable_testing()
  add_executable(ke_tests
    tests/main.cpp tests/TestRunner.h tests/Tests.h
    tests/MathTests.cpp
    tests/CoreTests.cpp
    tests/RuntimePolicyTests.cpp
    tests/DeviceTests.cpp
    tests/ElectroTests.cpp
    tests/PhysicsTests.cpp tests/SimTests.cpp
    tests/GraphTests.cpp tests/AblationTests.cpp
    tests/ArrayTests.cpp
    tests/SceneActionTests.cpp
    tests/AssemblyTests.cpp
  )
  target_link_libraries(ke_tests PRIVATE kecore)
  target_compile_definitions(ke_tests PRIVATE KE_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples"
                                               KE_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
  if(MSVC)
    target_compile_options(ke_tests PRIVATE /W4 /wd4100 /utf-8)
  else()
    target_compile_options(ke_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
  endif()
  add_test(NAME ke_tests COMMAND ke_tests)
endif()
