# Copies the .tpk that dotnet built (matching PATTERN) to TARGET, run by "make tpk".
file(GLOB tpk ${PATTERN})
if(NOT tpk)
  message(FATAL_ERROR "No .tpk matching ${PATTERN}")
endif()
file(COPY_FILE ${tpk} ${TARGET})
