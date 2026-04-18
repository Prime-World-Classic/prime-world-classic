#include "../System/systemStdAfx.h"
#include "../System/MainFrame.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

namespace
{
double ReadRunSeconds(int argc, char** argv)
{
  const double defaultSeconds = 2.0;

  for (int i = 1; i + 1 < argc; ++i)
  {
    if (strcmp(argv[i], "--seconds") == 0)
    {
      return atof(argv[i + 1]);
    }
  }

  return defaultSeconds;
}
}

int main(int argc, char** argv)
{
  const double runSeconds = ReadRunSeconds(argc, argv);
  if (!NMainFrame::InitApplication(0, "PrimeWorldLinuxBootstrap", "Prime World Linux Bootstrap", 0, false, 1280, 720, 0))
  {
    fprintf(stderr, "Failed to initialize the native Linux bootstrap window.\n");
    return 1;
  }

  NHPTimer::STime start = 0;
  NHPTimer::GetTime(start);

  while (!NMainFrame::IsExit())
  {
    NMainFrame::PumpMessages();

    NHPTimer::STime now = 0;
    NHPTimer::GetTime(now);
    if (NHPTimer::Time2Seconds(now - start) >= runSeconds)
    {
      break;
    }

    usleep(16 * 1000);
  }

  NMainFrame::ShutdownApplication();
  printf("PrimeWorldLinuxBootstrap finished successfully.\n");
  return 0;
}
