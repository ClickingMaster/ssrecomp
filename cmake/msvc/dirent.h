// Stand-in for <dirent.h> on clang-cl: the melonDS core includes it in FATStorage.cpp but
// uses std::filesystem and FatFS instead, and the Windows SDK has no such header.
#pragma once
