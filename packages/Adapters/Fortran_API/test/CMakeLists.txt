INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR}/../src)
TRIBITS_ADD_EXECUTABLE_AND_TEST(
  Fortran_API_test
  SOURCES tstFortran_API.F90
  COMM serial mpi
  LINKER_LANGUAGE Fortran
  STANDARD_PASS_OUTPUT
  )
