# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.15

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/rambodrahmani/DevOps/ffmpeg-video-player

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug

# Include any dependencies generated for this target.
include tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/depend.make

# Include the progress variables for this target.
include tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/progress.make

# Include the compile flags for this target's objects.
include tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/flags.make

tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/moving_rectangle.c.o: tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/flags.make
tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/moving_rectangle.c.o: ../tutorial02/References/moving-rectangle/moving_rectangle.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/moving_rectangle.c.o"
	cd /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/moving_rectangle.dir/moving_rectangle.c.o   -c /home/rambodrahmani/DevOps/ffmpeg-video-player/tutorial02/References/moving-rectangle/moving_rectangle.c

tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/moving_rectangle.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/moving_rectangle.dir/moving_rectangle.c.i"
	cd /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/rambodrahmani/DevOps/ffmpeg-video-player/tutorial02/References/moving-rectangle/moving_rectangle.c > CMakeFiles/moving_rectangle.dir/moving_rectangle.c.i

tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/moving_rectangle.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/moving_rectangle.dir/moving_rectangle.c.s"
	cd /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle && /usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/rambodrahmani/DevOps/ffmpeg-video-player/tutorial02/References/moving-rectangle/moving_rectangle.c -o CMakeFiles/moving_rectangle.dir/moving_rectangle.c.s

# Object files for target moving_rectangle
moving_rectangle_OBJECTS = \
"CMakeFiles/moving_rectangle.dir/moving_rectangle.c.o"

# External object files for target moving_rectangle
moving_rectangle_EXTERNAL_OBJECTS =

tutorial02/References/moving-rectangle/moving_rectangle: tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/moving_rectangle.c.o
tutorial02/References/moving-rectangle/moving_rectangle: tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/build.make
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libavcodec.so
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libavformat.so
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libavdevice.so
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libavutil.so
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libavfilter.so
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libswscale.so
tutorial02/References/moving-rectangle/moving_rectangle: /usr/lib/libswresample.so
tutorial02/References/moving-rectangle/moving_rectangle: tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable moving_rectangle"
	cd /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle && $(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/moving_rectangle.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/build: tutorial02/References/moving-rectangle/moving_rectangle

.PHONY : tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/build

tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/clean:
	cd /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle && $(CMAKE_COMMAND) -P CMakeFiles/moving_rectangle.dir/cmake_clean.cmake
.PHONY : tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/clean

tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/depend:
	cd /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/rambodrahmani/DevOps/ffmpeg-video-player /home/rambodrahmani/DevOps/ffmpeg-video-player/tutorial02/References/moving-rectangle /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle /home/rambodrahmani/DevOps/ffmpeg-video-player/cmake-build-debug/tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : tutorial02/References/moving-rectangle/CMakeFiles/moving_rectangle.dir/depend

