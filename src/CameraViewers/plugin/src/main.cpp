#include "IUnityGraphics.h"

#include <fstream>

extern "C" void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API RegisterPlugin()
{
  std::ofstream file{"pluginoutput.txt", std::ios::out | std::ios::trunc};
  if (file)
  {
	  file << "Hello plugin!" << std::endl;
  }
}