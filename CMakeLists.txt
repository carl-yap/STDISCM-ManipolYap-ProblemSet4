# CMakeList.txt : CMake project for ManipolYap_ProblemSet4, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.16)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("ManipolYap_ProblemSet4")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tesseract OCR
set(VCPKG_ROOT "C:/tools/vcpkg/installed/x64-windows")
include_directories(${VCPKG_ROOT}/include)
link_directories(${VCPKG_ROOT}/lib)

# Add source to this project's executable.
add_executable (ManipolYap_ProblemSet4  
	Client.cpp
	Server.cpp
)

target_link_libraries(ManipolYap_ProblemSet4 PRIVATE 
	tesseract55 
	leptonica-1.85.0
)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET ManipolYap_ProblemSet4 PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
