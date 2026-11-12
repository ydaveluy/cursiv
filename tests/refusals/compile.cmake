# Builds the units of the lanes refused at compile, which must not build, and
# gives each lane the diagnostics that name its fixture.
cmake_minimum_required(VERSION 3.30)

set(_keep_going -k)
if(GENERATOR MATCHES "Ninja")
  set(_keep_going -k 0)
endif()
string(REPLACE "," ";" TARGETS "${TARGETS}")
file(REMOVE_RECURSE "${OUT}")
foreach(_target IN LISTS TARGETS)
  file(MAKE_DIRECTORY "${OUT}/${_target}")
  # English diagnostics, whatever the locale.
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C
      "${CMAKE_COMMAND}" --build "${BUILD}" --config "${CONFIG}"
      --target ${_target} -- ${_keep_going}
      RESULT_VARIABLE _result
      OUTPUT_FILE "${OUT}/${_target}.txt" ERROR_FILE "${OUT}/${_target}.txt")
  if(_result EQUAL 0)
    message(FATAL_ERROR "${_target} built: none of its lanes is refused at compile")
  endif()
  execute_process(COMMAND "${LANES}" "${OUT}/${_target}.txt" "${OUT}/${_target}"
      COMMAND_ERROR_IS_FATAL ANY)
endforeach()
