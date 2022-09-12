set(CPACK_PACKAGE_NAME                      ${PROJECT_NAME})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY       "An FTP Server library for creating embedded FTP Servers")
set(CPACK_PACKAGE_VENDOR                    "Eclipse eCAL")
set(CPACK_PACKAGE_VERSION                   ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR             ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR             ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH             ${PROJECT_VERSION_PATCH})
set(CPACK_PACKAGE_DIRECTORY                 "${CMAKE_CURRENT_BINARY_DIR}/_package")

set(CPACK_PACKAGE_CONTACT                   "florian.reimold@continental-corporation.com")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER         "Florian Reimold <florian.reimold@continental-corporation.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE           "https://github.com/eclipse-ecal/fineftp-server")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS          ON)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS    ON)

set(CPACK_RESOURCE_FILE_LICENSE             "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README              "${CMAKE_CURRENT_LIST_DIR}/README.md")

include(CPack)