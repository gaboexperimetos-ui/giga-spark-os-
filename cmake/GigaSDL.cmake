# Gigaspark OS - deteccion de SDL compartida
#
# Prefiere SDL3 (3.4.x, la version actual) y cae a SDL2 si el sistema solo trae
# la 2.x, que sigue siendo el paquete por defecto en Debian/Ubuntu LTS.
# simulator/sdl_compat.h adapta la API, asi que el codigo es el mismo.
#
# Define:
#   GIGA_SDL_TARGET  - target a enlazar
#   GIGA_SDL_MAIN    - target de SDLmain si existe (vacio si no)
#   GIGA_SDL_MAJOR   - 3 o 2
#   GIGA_SDL_FOUND   - TRUE si hay alguna
#   GIGA_SDL_EXTRA_INCLUDES - includes extra para la ruta SDL2 (puede ir vacio)
#
# Opcion: -DGIGA_FORCE_SDL2=ON para probar la ruta SDL2 a proposito.

include_guard(GLOBAL)

set(GIGA_SDL_FOUND FALSE)
set(GIGA_SDL_TARGET "")
set(GIGA_SDL_MAIN "")
set(GIGA_SDL_MAJOR 0)

option(GIGA_FORCE_SDL2 "Usar SDL2 aunque haya SDL3 disponible" OFF)

if(NOT GIGA_FORCE_SDL2)
  find_package(SDL3 CONFIG QUIET)
  if(TARGET SDL3::SDL3)
    set(GIGA_SDL_TARGET SDL3::SDL3)
    set(GIGA_SDL_FOUND TRUE)
    set(GIGA_SDL_MAJOR 3)
  elseif(TARGET SDL3::SDL3-static)
    set(GIGA_SDL_TARGET SDL3::SDL3-static)
    set(GIGA_SDL_FOUND TRUE)
    set(GIGA_SDL_MAJOR 3)
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
      if(TARGET PkgConfig::SDL3)
        set(GIGA_SDL_TARGET PkgConfig::SDL3)
        set(GIGA_SDL_FOUND TRUE)
        set(GIGA_SDL_MAJOR 3)
      endif()
    endif()
  endif()
endif()

if(NOT GIGA_SDL_FOUND)
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    set(GIGA_SDL_TARGET SDL2::SDL2)
  elseif(TARGET SDL2::SDL2-static)
    set(GIGA_SDL_TARGET SDL2::SDL2-static)
  endif()
  if(GIGA_SDL_TARGET)
    set(GIGA_SDL_FOUND TRUE)
    set(GIGA_SDL_MAJOR 2)
    if(TARGET SDL2::SDLmain)
      set(GIGA_SDL_MAIN SDL2::SDLmain)
    elseif(TARGET SDL2::SDL2main)
      set(GIGA_SDL_MAIN SDL2::SDL2main)
    endif()
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
      if(TARGET PkgConfig::SDL2)
        set(GIGA_SDL_TARGET PkgConfig::SDL2)
        set(GIGA_SDL_FOUND TRUE)
        set(GIGA_SDL_MAJOR 2)
      endif()
    endif()
  endif()
endif()

if(GIGA_SDL_FOUND)
  message(STATUS "Gigaspark: usando SDL${GIGA_SDL_MAJOR} (${GIGA_SDL_TARGET})")
else()
  message(STATUS "Gigaspark: no se encontro SDL3 ni SDL2")
endif()

# sdl_compat.h incluye <SDL.h> en la ruta SDL2, asi que hace falta el padre de
# include/SDL2 cuando SDL2 se instalo a mano. Se expone como variable para que
# cada target la agregue: los targets importados suelen ser ALIAS y no admiten
# set_property (CMake lo rechaza con "can not be used on an ALIAS target").
set(GIGA_SDL_EXTRA_INCLUDES "")
if(GIGA_SDL_FOUND AND GIGA_SDL_MAJOR EQUAL 2)
  get_target_property(_giga_inc ${GIGA_SDL_TARGET} INTERFACE_INCLUDE_DIRECTORIES)
  if(_giga_inc)
    foreach(_d IN LISTS _giga_inc)
      if(_d MATCHES "/SDL2$")
        get_filename_component(_parent "${_d}" DIRECTORY)
        list(APPEND GIGA_SDL_EXTRA_INCLUDES "${_parent}")
      endif()
    endforeach()
  endif()
  if(GIGA_SDL_EXTRA_INCLUDES)
    list(REMOVE_DUPLICATES GIGA_SDL_EXTRA_INCLUDES)
  endif()
endif()
