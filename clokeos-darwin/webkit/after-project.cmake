# Wird nach project() geladen (CMAKE_PROJECT_INCLUDE): erst dann stehen die Plattform-Vorgaben fest und können überstimmt werden.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dylib;.a")
set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_SHARED_LIBRARY_SUFFIX ".dylib")
set(CMAKE_SHARED_LIBRARY_PREFIX "lib")
