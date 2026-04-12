/*
 * Pull in firmware source files for native testing.
 * Using the include pattern so everything shares one translation unit,
 * avoiding the need for a separate library build step.
 *
 * Located in lib/ so PlatformIO links it with every test executable.
 */

#include "../../stubs/test_support.cpp"
#include "../../../../Libraries/HMTLMessaging/ProgramManager.cpp"
#include "../../../../Libraries/HMTLMessaging/HMTLPrograms.cpp"
