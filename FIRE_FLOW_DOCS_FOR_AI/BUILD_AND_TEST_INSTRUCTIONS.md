# Building

The most straightforward way to build the EPANET files is by using `CMake` ([https://cmake.org/](https://cmake.org/)). `CMake` is a cross-platform build tool that generates platform native build systems that can be used with your compiler of choice. It uses a generator concept to represent different build tooling. `CMake` automatically detects the platform it is running on and generates the appropriate makefiles for the platform default compiler. Different generators can also be specified.

The project's `CMake` file (`CMakeLists.txt`) is located in its root directory and supports builds for Linux, Mac OS and Windows. To build the EPANET library and its command line executable using `CMake`, first open a console window and navigate to the project's root directory. Then enter the following commands:

```
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

# Testing

Unit tests have been written using the Boost Unit Testing Framework and other Boost libraries. The tests are compiled into individual executables that automatically perform checks on the EPANET toolkit and output libraries.

The CMake build system has been configured with a build option for building tests. When enabled (`-DBUILD_TESTS=ON`) the test executables are built and registered with the CTest test runner, the default value for the test build option is off. The location of Boost can also be defined with `-DBOOST_ROOT="%BOOST_ROOT%"` if required.

To build the test executables for the EPANET library, first open a console window and navigate to the project's root directory. Then enter the following commands:

```
mkdir build
cd build
cmake -DBUILD_TESTS=ON ..
cmake --build . --config Release
cd tests
ctest -C Release --output-on-failure
```
