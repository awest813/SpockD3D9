#pragma once

#include <vector>

#include "util_string.h"

namespace dxvk::env {
  
#ifdef _WIN32
  constexpr char PlatformDirSlash = '\\';
#else
  constexpr char PlatformDirSlash = '/';
#endif

  /**
   * \brief Checks whether the host platform is 32-bit
   */
  constexpr bool is32BitHostPlatform() {
    return sizeof(void*) == 4;
  }

  /**
   * \brief Gets environment variable
   * 
   * If the variable is not defined, this will return
   * an empty string. Note that environment variables
   * may be defined with an empty value.
   * \param [in] name Name of the variable
   * \returns Value of the variable
   */
  std::string getEnvVar(const char* name);
  
  /**
   * \brief Checks whether a file name has a given extension
   *
   * \param [in] name File name
   * \param [in] ext Extension to match, in lowercase letters
   * \returns Position of the extension within the file name, or
   *    \c std::string::npos if the file has a different extension
   */
  size_t matchFileExtension(const std::string& name, const char* ext);

  /**
   * \brief Gets the executable name
   * 
   * Returns the base name (not the full path) of the
   * program executable, including the file extension.
   * This function should be used to identify programs.
   * \returns Executable name
   */
  std::string getExeName();
  
  /**
   * \brief Gets the executable name without extension
   *
   * Same as \ref getExeName but without the file extension.
   * \returns Executable name
   */
  std::string getExeBaseName();

  /**
   * \brief Gets full path to executable
   * \returns Path to executable
   */
  std::string getExePath();
  
  /**
   * \brief Sets name of the calling thread
   * \param [in] name Thread name
   */
  void setThreadName(const std::string& name);

  /**
   * \brief Creates a directory
   * 
   * \param [in] path Path to directory
   * \returns \c true on success
   */
  bool createDirectory(const std::string& path);

#if defined(__APPLE__)
  /**
   * \brief Homebrew install prefixes to probe on macOS
   *
   * Returns \c HOMEBREW_PREFIX when set, followed by the default Apple
   * Silicon and Intel Homebrew locations.
   */
  std::vector<std::string> getHomebrewPrefixes();

  /**
   * \brief Prepare dyld and Vulkan loader environment on macOS
   *
   * When \c DYLD_LIBRARY_PATH is narrowed (e.g. to a SpockD3D9 install lib
   * dir), dependent libraries such as SDL2 and libvulkan may not resolve
   * unless Homebrew's lib directory is on the fallback search path. Also
   * points the Vulkan loader at Homebrew's MoltenVK ICD manifest when the
   * user has not already selected one.
   *
   * Safe to call multiple times. Must run before WSI or Vulkan libraries
   * are loaded dynamically.
   */
  void prepareDarwinEnvironment();
#endif
  
}
